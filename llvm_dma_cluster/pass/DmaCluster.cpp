//===- DmaCluster.cpp - DMA buffer discovery & cluster section assignment -===//
//
// LLVM 18, New Pass Manager plugin.
//  - lld Full LTO 앞단(FullLinkTimeOptimizationEarlyEP)에서 자동 실행
//  - opt 단독 실행: opt -load-pass-plugin=DmaCluster.so -passes=dma-cluster
//
// 하는 일
//  1) FreeRTOS 태스크 entry 함수 수집 (xTaskCreate*, xTaskCreateRestricted*,
//     MPU_ 접두사 포함)
//  2) 태스크별 reachable function set 계산 (direct call + 간접 호출은
//     address-taken & 같은 함수 타입으로 conservative 연결)
//  3) DMA sink 호출(SECURE_dma_m2m_start 등)의 버퍼 인자를 전역 변수까지
//     역추적 (GEP/cast, PHI/select, 인자 → 호출자, 전역 포인터 변수의 store)
//  4) 버퍼별 owner task 집합 결정
//       - 1개  → section ".dma.t.<task>"   (태스크 전용 cluster)
//       - 2개+ → section ".dma.s.<k>"      (공유 cluster, owner 집합별 하나)
//     __attribute__((annotate("dma:<task>"))) / annotate("dma:shared:<name>")
//     는 자동 판정보다 우선한다.
//  5) 결과를 JSON(dma_layout.json)으로 기록 → gen_layout.py 가 링커 스크립트,
//     manifest, NS-MPU region 헤더를 생성
//
// 설정: 환경 변수 DMA_CLUSTER_CONFIG=<config/dma_config.json>
//       DMA_CLUSTER_STRICT=1 이면 error 진단이 있을 때 링크 실패
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace llvm;

namespace {

//===----------------------------------------------------------------------===//
// 설정
//===----------------------------------------------------------------------===//
struct SinkSpec {
  std::string Func;              // 예: "SECURE_dma_m2m_start"
  SmallVector<unsigned, 2> Args; // 버퍼 인자 위치 (0부터)
};

struct Config {
  std::vector<SinkSpec> Sinks;
  std::vector<std::string> TaskCreateApis = {
      "xTaskCreate", "xTaskCreateStatic", "xTaskCreateRestricted",
      "xTaskCreateRestrictedStatic"};
  std::vector<std::string> HeapAllocators = {"pvPortMalloc", "malloc",
                                             "calloc"};
  std::string Out = "dma_layout.json";
  bool Ok = false;
};

static StringRef stripMpuPrefix(StringRef N) {
  N.consume_front("MPU_"); // FreeRTOS MPU wrapper 이름
  return N;
}

static Config loadConfig() {
  Config C;
  const char *Path = std::getenv("DMA_CLUSTER_CONFIG");
  if (!Path) {
    errs() << "[dma-cluster] DMA_CLUSTER_CONFIG 미설정: pass 비활성\n";
    return C;
  }
  auto Buf = MemoryBuffer::getFile(Path);
  if (!Buf) {
    errs() << "[dma-cluster] 설정 파일을 읽을 수 없음: " << Path << "\n";
    return C;
  }
  Expected<json::Value> J = json::parse((*Buf)->getBuffer());
  if (!J) {
    errs() << "[dma-cluster] JSON 오류: " << toString(J.takeError()) << "\n";
    return C;
  }
  const json::Object *O = J->getAsObject();
  if (!O)
    return C;
  if (auto S = O->getString("out"))
    C.Out = S->str();
  if (const json::Array *A = O->getArray("task_create_apis")) {
    C.TaskCreateApis.clear();
    for (const json::Value &V : *A)
      if (auto S = V.getAsString())
        C.TaskCreateApis.push_back(S->str());
  }
  if (const json::Array *A = O->getArray("heap_allocators")) {
    C.HeapAllocators.clear();
    for (const json::Value &V : *A)
      if (auto S = V.getAsString())
        C.HeapAllocators.push_back(S->str());
  }
  if (const json::Array *A = O->getArray("sinks"))
    for (const json::Value &V : *A)
      if (const json::Object *S = V.getAsObject()) {
        SinkSpec K;
        if (auto F = S->getString("func"))
          K.Func = F->str();
        if (const json::Array *Ar = S->getArray("args"))
          for (const json::Value &X : *Ar)
            if (auto I = X.getAsInteger())
              K.Args.push_back(static_cast<unsigned>(*I));
        if (!K.Func.empty() && !K.Args.empty())
          C.Sinks.push_back(std::move(K));
      }
  C.Ok = !C.Sinks.empty();
  if (!C.Ok)
    errs() << "[dma-cluster] sink 목록이 비어 있음: pass 비활성\n";
  return C;
}

//===----------------------------------------------------------------------===//
// 유틸
//===----------------------------------------------------------------------===//
static std::string locOf(const Instruction &I) {
  if (const DILocation *L = I.getDebugLoc())
    return (L->getFilename() + ":" + Twine(L->getLine())).str();
  return "?";
}

static Function *asFunction(Value *V) {
  return V ? dyn_cast<Function>(V->stripPointerCasts()) : nullptr;
}

static Function *calleeOf(const CallBase &CB) {
  return asFunction(CB.getCalledOperand());
}

//===----------------------------------------------------------------------===//
// 1) 태스크 entry 함수
//===----------------------------------------------------------------------===//
// TaskParameters_t 의 첫 필드(pvTaskCode)가 entry 함수다.
static Function *entryFromParams(Value *P) {
  P = P->stripPointerCasts();
  // (a) static const TaskParameters_t xParams = { vTask, ... };
  if (auto *GV = dyn_cast<GlobalVariable>(P)) {
    if (GV->hasInitializer())
      if (auto *CS = dyn_cast<ConstantStruct>(GV->getInitializer()))
        return asFunction(CS->getOperand(0));
    return nullptr;
  }
  // (b) 지역 TaskParameters_t: 필드 0 store 또는 상수 전역에서 memcpy
  if (auto *AI = dyn_cast<AllocaInst>(P)) {
    for (User *U : AI->users()) {
      if (auto *SI = dyn_cast<StoreInst>(U))
        if (SI->getPointerOperand() == AI)
          if (Function *F = asFunction(SI->getValueOperand()))
            return F;
      if (auto *GEP = dyn_cast<GetElementPtrInst>(U))
        if (GEP->hasAllZeroIndices())
          for (User *W : GEP->users())
            if (auto *SI = dyn_cast<StoreInst>(W))
              if (SI->getPointerOperand() == GEP)
                if (Function *F = asFunction(SI->getValueOperand()))
                  return F;
      if (auto *MC = dyn_cast<MemCpyInst>(U))
        if (MC->getDest()->stripPointerCasts() == AI)
          if (Function *F = entryFromParams(MC->getSource()))
            return F;
    }
  }
  return nullptr;
}

//===----------------------------------------------------------------------===//
// 결과 저장
//===----------------------------------------------------------------------===//
struct BufInfo {
  std::set<std::string> AutoOwners; // 자동 판정
  std::string Forced;               // annotate 결과 ("" = 없음)
  std::set<std::string> Sites;      // sink 호출 위치
};

struct State {
  const Config &Cfg;
  Module &M;
  std::map<GlobalVariable *, BufInfo> Bufs;
  json::Array Diags;
  unsigned Errors = 0;

  State(const Config &C, Module &Mod) : Cfg(C), M(Mod) {}

  void diag(StringRef Kind, StringRef Sev, StringRef Task, const Instruction *I,
            StringRef Msg) {
    json::Object D{{"kind", Kind.str()},
                   {"severity", Sev.str()},
                   {"task", Task.str()},
                   {"func", I ? I->getFunction()->getName().str() : ""},
                   {"loc", I ? locOf(*I) : ""},
                   {"msg", Msg.str()}};
    Diags.push_back(std::move(D));
    if (Sev == "error")
      ++Errors;
    errs() << "[dma-cluster] " << Sev << " " << Kind << " (" << Task << ") "
           << (I ? locOf(*I) : "") << ": " << Msg << "\n";
  }
};

//===----------------------------------------------------------------------===//
// 2) reachable function set
//===----------------------------------------------------------------------===//
using FnSet = SmallPtrSet<Function *, 64>;

static FnSet reach(Function *Entry,
                   const std::map<FunctionType *, std::vector<Function *>> &AT) {
  FnSet Seen;
  SmallVector<Function *, 32> WL{Entry};
  while (!WL.empty()) {
    Function *F = WL.pop_back_val();
    if (!Seen.insert(F).second || F->isDeclaration())
      continue;
    for (Instruction &I : instructions(*F)) {
      auto *CB = dyn_cast<CallBase>(&I);
      if (!CB || CB->isInlineAsm())
        continue;
      if (Function *C = calleeOf(*CB)) {
        WL.push_back(C);
      } else { // 간접 호출: 같은 타입의 address-taken 함수 전부 (conservative)
        auto It = AT.find(CB->getFunctionType());
        if (It != AT.end())
          for (Function *T : It->second)
            WL.push_back(T);
      }
    }
  }
  return Seen;
}

//===----------------------------------------------------------------------===//
// 3) 버퍼 인자 역추적
//===----------------------------------------------------------------------===//
static bool isHeapCall(const Value *V, const Config &C) {
  if (auto *CB = dyn_cast<CallBase>(V))
    if (Function *F = calleeOf(*CB))
      for (const std::string &N : C.HeapAllocators)
        if (stripMpuPrefix(F->getName()) == N)
          return true;
  return false;
}

static void recordGlobal(State &S, GlobalVariable &G, StringRef Task,
                         const CallBase &Site) {
  if (G.isConstant()) // Flash 상수: DMA source 로만 쓰임 → 배치 대상 아님
    return;
  if (G.isDeclaration()) {
    S.diag("extern-buffer", "error", Task, &Site,
           (G.getName() + ": 정의가 LTO 모듈 밖에 있음 (비-LTO 오브젝트?)")
               .str());
    return;
  }
  if (G.hasInitializer() && !G.getInitializer()->isNullValue()) {
    S.diag("initialized-buffer", "error", Task, &Site,
           (G.getName() + ": 0 이 아닌 초기값. DMA cluster 는 NOLOAD(0 초기화)")
               .str());
    return;
  }
  BufInfo &B = S.Bufs[&G];
  B.AutoOwners.insert(Task.str());
  B.Sites.insert(locOf(Site));
}

static void trace(State &S, const Value *V, StringRef Task, const FnSet &R,
                  const CallBase &Site, unsigned Depth,
                  SmallPtrSetImpl<const Value *> &Visited) {
  if (Depth > 16 || !Visited.insert(V).second)
    return;
  const Value *O = getUnderlyingObject(V, /*MaxLookup=*/0); // GEP/cast 제거

  if (auto *G = dyn_cast<GlobalVariable>(O)) {
    // 전역 "포인터 변수"를 거쳐 넘긴 경우는 아래 LoadInst 에서 처리된다.
    if (G->getValueType()->isStructTy() && V != O)
      S.diag("struct-field", "warning", Task, &Site,
             (G->getName() + ": 구조체 일부가 DMA 버퍼 → 구조체 전체를 배치")
                 .str());
    recordGlobal(S, *const_cast<GlobalVariable *>(G), Task, Site);
    return;
  }
  if (auto *A = dyn_cast<Argument>(O)) { // 호출자로 거슬러 올라감 (context = Task)
    Function *P = const_cast<Function *>(A->getParent());
    bool Any = false;
    for (User *U : P->users())
      if (auto *CB = dyn_cast<CallBase>(U))
        if (calleeOf(*CB) == P && R.contains(CB->getFunction())) {
          Any = true;
          trace(S, CB->getArgOperand(A->getArgNo()), Task, R, *CB, Depth + 1,
                Visited);
        }
    if (!Any)
      S.diag("unresolved-arg", "warning", Task, &Site,
             (P->getName() + " 의 인자: 이 태스크 안의 직접 호출자 없음 "
                             "(간접 호출?) → annotate 필요")
                 .str());
    return;
  }
  if (auto *PN = dyn_cast<PHINode>(O)) {
    for (const Value *In : PN->incoming_values())
      trace(S, In, Task, R, Site, Depth + 1, Visited);
    return;
  }
  if (auto *SI = dyn_cast<SelectInst>(O)) {
    trace(S, SI->getTrueValue(), Task, R, Site, Depth + 1, Visited);
    trace(S, SI->getFalseValue(), Task, R, Site, Depth + 1, Visited);
    return;
  }
  if (auto *LI = dyn_cast<LoadInst>(O)) {
    // 전역 포인터 변수 p 에서 읽은 값: p 의 초기값과 p 로의 모든 store 값을 추적
    // (flow-insensitive over-approximation → owner 가 넓어질 수 있음)
    const Value *Ptr = getUnderlyingObject(LI->getPointerOperand(), 0);
    if (auto *PG = dyn_cast<GlobalVariable>(Ptr)) {
      S.diag("pointer-var", "warning", Task, &Site,
             (PG->getName() + " 포인터 변수 경유: 모든 store 값을 후보로 사용")
                 .str());
      if (PG->hasInitializer() && !PG->getInitializer()->isNullValue())
        trace(S, PG->getInitializer(), Task, R, Site, Depth + 1, Visited);
      for (const User *U : PG->users())
        if (auto *St = dyn_cast<StoreInst>(U))
          if (getUnderlyingObject(St->getPointerOperand(), 0) == PG)
            trace(S, St->getValueOperand(), Task, R, Site, Depth + 1, Visited);
      return;
    }
    S.diag("unresolved-load", "error", Task, &Site,
           "지역/힙 포인터에서 읽은 버퍼 주소: 정적 해석 불가 → annotate 또는 "
           "SVF 단계 필요");
    return;
  }
  if (isa<AllocaInst>(O)) {
    S.diag("stack-buffer", "error", Task, &Site,
           "지역(스택) 배열을 DMA 에 전달: static 승격 또는 annotate 필요");
    return;
  }
  if (isHeapCall(O, S.Cfg)) {
    S.diag("heap-buffer", "error", Task, &Site,
           "힙 버퍼를 DMA 에 전달: cluster 내 DMA pool 로 치환 필요");
    return;
  }
  if (isa<ConstantPointerNull>(O))
    return;
  S.diag("unresolved", "error", Task, &Site, "버퍼 출처를 해석할 수 없음");
}

//===----------------------------------------------------------------------===//
// 4) annotate("dma:...") 우선 적용
//===----------------------------------------------------------------------===//
static void readAnnotations(State &S) {
  GlobalVariable *A = S.M.getGlobalVariable("llvm.global.annotations");
  if (!A || !A->hasInitializer())
    return;
  auto *Arr = dyn_cast<ConstantArray>(A->getInitializer());
  if (!Arr)
    return;
  for (const Use &U : Arr->operands()) {
    auto *CS = dyn_cast<ConstantStruct>(U.get());
    if (!CS || CS->getNumOperands() < 2)
      continue;
    auto *G = dyn_cast<GlobalVariable>(CS->getOperand(0)->stripPointerCasts());
    auto *StrGV =
        dyn_cast<GlobalVariable>(CS->getOperand(1)->stripPointerCasts());
    if (!G || !StrGV || !StrGV->hasInitializer())
      continue;
    auto *CDA = dyn_cast<ConstantDataArray>(StrGV->getInitializer());
    if (!CDA || !CDA->isCString())
      continue;
    StringRef Tag = CDA->getAsCString();
    if (!Tag.consume_front("dma:"))
      continue;
    S.Bufs[G].Forced = Tag.str(); // "<task>" 또는 "shared:<name>"
  }
}

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//
struct DmaClusterPass : PassInfoMixin<DmaClusterPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    static const Config Cfg = loadConfig();
    if (!Cfg.Ok)
      return PreservedAnalyses::all();
    State S(Cfg, M);

    // 간접 호출 대상 후보 (address-taken, 타입별)
    std::map<FunctionType *, std::vector<Function *>> AddrTaken;
    for (Function &F : M)
      if (!F.isDeclaration() && F.hasAddressTaken())
        AddrTaken[F.getFunctionType()].push_back(&F);

    // 1) 태스크 entry 수집
    std::map<std::string, Function *> Tasks; // 이름 → entry
    for (Function &F : M)
      for (Instruction &I : instructions(F)) {
        auto *CB = dyn_cast<CallBase>(&I);
        if (!CB)
          continue;
        Function *C = calleeOf(*CB);
        if (!C)
          continue;
        StringRef N = stripMpuPrefix(C->getName());
        bool IsCreate = false, IsRestricted = false;
        for (const std::string &Api : Cfg.TaskCreateApis)
          if (N == Api) {
            IsCreate = true;
            IsRestricted = StringRef(Api).contains("Restricted");
          }
        if (!IsCreate || CB->arg_size() == 0)
          continue;
        Function *E = IsRestricted ? entryFromParams(CB->getArgOperand(0))
                                   : asFunction(CB->getArgOperand(0));
        if (E)
          Tasks[E->getName().str()] = E;
        else
          S.diag("task-entry", "warning", "", &I,
                 "태스크 entry 함수를 해석하지 못함 (함수 포인터 변수?)");
      }

    // 2) 태스크별 reachable set, 3) sink 인자 추적
    for (auto &[TName, Entry] : Tasks) {
      FnSet R = reach(Entry, AddrTaken);
      for (Function *F : R) {
        if (F->isDeclaration())
          continue;
        for (Instruction &I : instructions(*F)) {
          auto *CB = dyn_cast<CallBase>(&I);
          if (!CB)
            continue;
          Function *C = calleeOf(*CB);
          if (!C)
            continue;
          StringRef N = stripMpuPrefix(C->getName());
          for (const SinkSpec &K : Cfg.Sinks) {
            if (N != K.Func)
              continue;
            for (unsigned Ai : K.Args) {
              if (Ai >= CB->arg_size())
                continue;
              SmallPtrSet<const Value *, 16> Visited;
              trace(S, CB->getArgOperand(Ai), TName, R, *CB, 0, Visited);
            }
          }
        }
      }
    }

    // 4) annotate 우선
    readAnnotations(S);

    // 5) cluster 결정과 section 부여
    //    key: "t:<task>" (전용) / "s:<owner1,owner2,..>" 또는 "s:<name>" (공유)
    std::map<std::string, std::vector<GlobalVariable *>> Clusters;
    for (auto &[G, B] : S.Bufs) {
      std::string Key;
      if (!B.Forced.empty()) {
        StringRef F = B.Forced;
        Key = F.consume_front("shared:") ? "s:" + F.str() : "t:" + F.str();
      } else if (B.AutoOwners.size() == 1) {
        Key = "t:" + *B.AutoOwners.begin();
      } else if (!B.AutoOwners.empty()) {
        std::string J;
        for (const std::string &O : B.AutoOwners)
          J += (J.empty() ? "" : ",") + O;
        Key = "s:" + J;
      } else {
        continue; // annotate 만 있고 사용처 없는 경우가 아니면 도달하지 않음
      }
      Clusters[Key].push_back(G);
    }

    const DataLayout &DL = M.getDataLayout();
    json::Array JClusters;
    unsigned SharedIdx = 0;
    bool Changed = false;
    for (auto &[Key, Gs] : Clusters) { // std::map → 결정적 순서
      bool Shared = StringRef(Key).starts_with("s:");
      std::string Id = Shared ? ("s" + std::to_string(SharedIdx++))
                              : ("t." + Key.substr(2));
      std::string Sec = Shared ? (".dma.s." + std::to_string(SharedIdx - 1))
                               : (".dma.t." + Key.substr(2));
      json::Array JBufs, JOwners;
      std::set<std::string> Owners;
      uint64_t Bound = 0;
      for (GlobalVariable *G : Gs) {
        if (G->hasCommonLinkage()) // common 심볼은 section 을 가질 수 없음
          G->setLinkage(GlobalValue::ExternalLinkage);
        G->setSection(Sec);
        Changed = true;
        uint64_t Sz = DL.getTypeAllocSize(G->getValueType());
        uint64_t Al = G->getAlign() ? G->getAlign()->value()
                                    : DL.getPreferredAlign(G).value();
        Bound += Sz + (Al > 1 ? Al - 1 : 0); // 정렬 패딩 상한 포함
        const BufInfo &B = S.Bufs[G];
        for (const std::string &O : B.AutoOwners)
          Owners.insert(O);
        json::Array Sites;
        for (const std::string &L : B.Sites)
          Sites.push_back(L);
        JBufs.push_back(json::Object{{"name", G->getName().str()},
                                     {"size", static_cast<int64_t>(Sz)},
                                     {"align", static_cast<int64_t>(Al)},
                                     {"forced", B.Forced},
                                     {"sites", std::move(Sites)}});
      }
      if (!Shared)
        Owners.insert(Key.substr(2));
      for (const std::string &O : Owners)
        JOwners.push_back(O);
      JClusters.push_back(json::Object{{"id", Id},
                                       {"section", Sec},
                                       {"kind", Shared ? "shared" : "task"},
                                       {"owners", std::move(JOwners)},
                                       {"bytes_bound", static_cast<int64_t>(Bound)},
                                       {"buffers", std::move(JBufs)}});
    }

    json::Object JTasks;
    for (auto &[TName, Entry] : Tasks)
      JTasks[TName] = json::Object{{"entry", Entry->getName().str()}};

    json::Object Root{{"version", 1},
                      {"clusters", std::move(JClusters)},
                      {"tasks", std::move(JTasks)},
                      {"diagnostics", std::move(S.Diags)},
                      {"errors", static_cast<int64_t>(S.Errors)}};
    std::error_code EC;
    raw_fd_ostream OS(Cfg.Out, EC);
    if (EC)
      errs() << "[dma-cluster] 출력 파일 열기 실패: " << Cfg.Out << "\n";
    else
      OS << formatv("{0:2}", json::Value(std::move(Root))) << "\n";

    if (S.Errors && std::getenv("DMA_CLUSTER_STRICT"))
      report_fatal_error("dma-cluster: error 진단 존재 (dma_layout.json 참고)");

    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }
  static bool isRequired() { return true; } // -O0 / optnone 에서도 실행
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "DmaCluster", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            // opt 단독: -passes=dma-cluster
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name != "dma-cluster")
                    return false;
                  MPM.addPass(DmaClusterPass());
                  return true;
                });
            // lld Full LTO: 최적화(인라이닝) 전에 실행 → call site 가 원형대로 남음
            PB.registerFullLinkTimeOptimizationEarlyEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel) {
                  MPM.addPass(DmaClusterPass());
                });
          }};
}
