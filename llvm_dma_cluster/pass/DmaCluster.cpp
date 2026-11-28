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

#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdlib>
#include <functional>
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
  SmallVector<unsigned, 2> Args; // 버퍼 인자 위치 (0부터). Svc 면 레지스터 번호 (1 = r1)
  int64_t Svc = -1;              // >= 0: 함수 호출이 아니라 inline asm "svc #Svc" 가 sink
};

struct Config {
  std::vector<SinkSpec> Sinks;
  std::vector<std::string> TaskCreateApis = {
      "xTaskCreate", "xTaskCreateStatic", "xTaskCreateRestricted",
      "xTaskCreateRestrictedStatic"};
  std::vector<std::string> HeapAllocators = {"pvPortMalloc", "malloc",
                                             "calloc"};
  // 이 함수들 안의 sink 호출은 분석하지 않는다 (의도적으로 남의 버퍼를 넘기는 공격 시험 등).
  // 인자 역추적도 이 함수들의 호출 위치로는 올라가지 않는다. 함수는 noinline 이어야 한다.
  std::set<std::string> ExcludeCallers;
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
  if (const json::Array *A = O->getArray("exclude_callers"))
    for (const json::Value &V : *A)
      if (auto S = V.getAsString())
        C.ExcludeCallers.insert(S->str());
  if (const json::Array *A = O->getArray("sinks"))
    for (const json::Value &V : *A)
      if (const json::Object *S = V.getAsObject()) {
        SinkSpec K;
        if (auto F = S->getString("func"))
          K.Func = F->str();
        if (auto N = S->getInteger("svc"))
          K.Svc = *N;
        if (const json::Array *Ar = S->getArray("args"))
          for (const json::Value &X : *Ar)
            if (auto I = X.getAsInteger())
              K.Args.push_back(static_cast<unsigned>(*I));
        if ((!K.Func.empty() || K.Svc >= 0) && !K.Args.empty())
          C.Sinks.push_back(std::move(K));
      }
  if (const char *O = std::getenv("DMA_CLUSTER_OUT")) // 빌드별 출력 경로 (시험 변형이 본 빌드를 덮지 않게)
    C.Out = O;
  C.Ok = !C.Sinks.empty();
  if (!C.Ok)
    errs() << "[dma-cluster] sink 목록이 비어 있음: pass 비활성\n";
  return C;
}

//===----------------------------------------------------------------------===//
// 유틸
//===----------------------------------------------------------------------===//
// "file:line" + 인라인된 경우 호출 위치 체인 (" <- file:line ...").
// LTO 전 단계(per-TU -O2)에서 래퍼가 인라인되면 안쪽 위치만으로는 어느 태스크 코드인지 알 수 없다.
static std::string locOf(const Instruction &I) {
  const DILocation *L = I.getDebugLoc();
  if (!L)
    return "?";
  std::string R = (L->getFilename() + ":" + Twine(L->getLine())).str();
  for (const DILocation *IA = L->getInlinedAt(); IA; IA = IA->getInlinedAt())
    R += (" <- " + IA->getFilename().rsplit('/').second + ":" + Twine(IA->getLine())).str();
  return R;
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
// 삽입 순서 보존 (포인터 순서 순회 → 1차/2차 링크 JSON 이 달라지는 비결정성 방지).
// SmallPtrSet 의 SmallSize 상한은 32 (LLVM 18 static_assert).
using FnSet = SmallSetVector<Function *, 32>;

static FnSet reach(Function *Entry,
                   const std::map<FunctionType *, std::vector<Function *>> &AT) {
  FnSet Seen;
  SmallVector<Function *, 32> WL{Entry};
  while (!WL.empty()) {
    Function *F = WL.pop_back_val();
    if (!Seen.insert(F) || F->isDeclaration())
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
// 2-1) inline asm SVC sink: "svc #N" / "svc $k" (k = 상수 N 인 "i" 피연산자)
//===----------------------------------------------------------------------===//
// 반환: 이 호출이 svc 이면 그 번호, 아니면 -1. Regs[r] = r<r> 에 들어가는 값 (입력 또는 "+r" 묶음).
static int64_t svcOf(const CallBase &CB, SmallVectorImpl<const Value *> &Regs) {
  const auto *IA = dyn_cast<InlineAsm>(CB.getCalledOperand());
  if (!IA)
    return -1;
  StringRef Str = StringRef(IA->getAsmString()).trim();
  if (!Str.consume_front("svc"))
    return -1;
  Str = Str.trim();
  Regs.assign(16, nullptr);
  SmallVector<std::string, 4> OutReg; // 출력 피연산자 번호 → 레지스터 코드
  SmallVector<const Value *, 8> OpVal; // asm 피연산자 번호($k) → 값 (출력은 nullptr)
  unsigned ArgNo = 0;
  auto regNo = [](StringRef C) -> int {
    unsigned R;
    if (C.consume_front("{r") && C.consume_back("}") && !C.getAsInteger(10, R) && R < 16)
      return static_cast<int>(R);
    return -1;
  };
  for (const InlineAsm::ConstraintInfo &CI : IA->ParseConstraints()) {
    if (CI.Type == InlineAsm::isClobber)
      continue;
    std::string Code = CI.Codes.empty() ? "" : CI.Codes[0];
    if (CI.Type == InlineAsm::isOutput) {
      OutReg.push_back(Code);
      OpVal.push_back(nullptr);
      if (CI.isIndirect)
        ++ArgNo;
      continue;
    }
    if (CI.Type != InlineAsm::isInput || ArgNo >= CB.arg_size())
      continue;
    const Value *V = CB.getArgOperand(ArgNo++);
    OpVal.push_back(V);
    int R = regNo(Code);
    if (CI.hasMatchingInput() || (R < 0 && !Code.empty() && isDigit(Code[0]))) {
      unsigned T; // 묶인 입력 ("0" 또는 "{r0}" 이 출력과 같은 레지스터)
      if (!StringRef(Code).getAsInteger(10, T) && T < OutReg.size())
        R = regNo(OutReg[T]);
    }
    if (R >= 0)
      Regs[R] = V;
  }
  int64_t N = -1;
  if (Str.consume_front("$")) {
    Str.consume_front("{");
    unsigned K;
    if (!Str.consumeInteger(10, K) && K < OpVal.size() && OpVal[K])
      if (auto *CI = dyn_cast<ConstantInt>(OpVal[K]))
        N = CI->getSExtValue();
  } else {
    Str.consume_front("#");
    if (Str.consumeInteger(0, N))
      N = -1;
  }
  return N;
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
  if (auto *PI = dyn_cast<PtrToIntOperator>(V)) // svc 레지스터 인자: (uint32_t)ptr
    return trace(S, PI->getPointerOperand(), Task, R, Site, Depth + 1, Visited);
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
        if (calleeOf(*CB) == P && R.count(CB->getFunction()) &&
            !S.Cfg.ExcludeCallers.count(CB->getFunction()->getName().str())) {
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

    // 1) 태스크 entry 수집
    std::map<std::string, Function *> Tasks; // 이름 → entry
    SmallPtrSet<const CallBase *, 16> CreateCalls;
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
        CreateCalls.insert(CB);
        // MPU_xTaskCreate* 래퍼가 자기 인자를 그대로 넘기는 호출: 래퍼의 호출자에서 처리된다
        if (auto *A = dyn_cast<Argument>(CB->getArgOperand(0)->stripPointerCasts()))
          if (stripMpuPrefix(A->getParent()->getName()) == N)
            continue;
        Function *E = IsRestricted ? entryFromParams(CB->getArgOperand(0))
                                   : asFunction(CB->getArgOperand(0));
        if (E)
          Tasks[E->getName().str()] = E;
        else
          S.diag("task-entry", "warning", "", &I,
                 "태스크 entry 함수를 해석하지 못함 (함수 포인터 변수?)");
      }

    // 간접 호출 대상 후보 (address-taken, 타입별).
    // opaque pointer 에서는 void(ptr) 타입이 태스크 함수·타이머 콜백·HAL 콜백 등 모두 같으므로,
    // 주소가 "태스크 생성 API 에 entry 로 넘겨지는 것" 에만 쓰이는 함수는 후보에서 뺀다
    // (그렇지 않으면 타이머 태스크가 모든 DMA 태스크를 호출할 수 있다고 판정된다).
    auto isCreateArg0 = [&](const User *U, const Value *V) {
      auto *CB = dyn_cast<CallBase>(U);
      return CB && CreateCalls.count(CB) && CB->arg_size() > 0 &&
             CB->getArgOperand(0)->stripPointerCasts() == V;
    };
    std::function<bool(const Value *, unsigned)> onlyEntryUses =
        [&](const Value *V, unsigned Depth) -> bool {
      if (Depth > 8)
        return false;
      for (const User *U : V->users()) {
        if (isCreateArg0(U, V))
          continue;
        if (auto *GV = dyn_cast<GlobalVariable>(U)) { // 초기값 안: GV 가 생성 API 에만 쓰여야 함
          if (GV->getName() == "llvm.used" || GV->getName() == "llvm.compiler.used")
            continue;
          for (const User *W : GV->users())
            if (!isCreateArg0(W, GV) &&
                !(isa<Constant>(W) && onlyEntryUses(W, Depth + 1)))
              return false;
          continue;
        }
        if (isa<Constant>(U) && !isa<GlobalValue>(U)) { // ConstantStruct / ConstantExpr
          if (!onlyEntryUses(U, Depth + 1))
            return false;
          continue;
        }
        if (auto *CB = dyn_cast<CallBase>(U); CB && CB->isCallee(&U->getOperandUse(0)))
          continue; // 직접 호출
        return false; // store, 다른 인자 등 → 주소가 흘러감
      }
      return true;
    };
    std::map<FunctionType *, std::vector<Function *>> AddrTaken;
    for (Function &F : M) {
      if (F.isDeclaration() || !F.hasAddressTaken())
        continue;
      bool IsEntry = false;
      for (auto &[TN, E] : Tasks)
        IsEntry |= (E == &F);
      if (IsEntry && onlyEntryUses(&F, 0))
        continue;
      AddrTaken[F.getFunctionType()].push_back(&F);
    }

    // 2) 태스크별 reachable set, 3) sink 인자 추적
    for (auto &[TName, Entry] : Tasks) {
      FnSet R = reach(Entry, AddrTaken);
      for (Function *F : R) {
        if (F->isDeclaration() || Cfg.ExcludeCallers.count(F->getName().str()))
          continue;
        for (Instruction &I : instructions(*F)) {
          auto *CB = dyn_cast<CallBase>(&I);
          if (!CB)
            continue;
          if (CB->isInlineAsm()) {
            SmallVector<const Value *, 16> Regs;
            int64_t N = svcOf(*CB, Regs);
            for (const SinkSpec &K : Cfg.Sinks)
              if (N >= 0 && K.Svc == N)
                for (unsigned Ri : K.Args)
                  if (Ri < Regs.size() && Regs[Ri]) {
                    SmallPtrSet<const Value *, 16> Visited;
                    trace(S, Regs[Ri], TName, R, *CB, 0, Visited);
                  }
            continue;
          }
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
    // unresolved-load 는 "annotate 로 보완" 이 문서화된 해법이다. 그 태스크(또는 공유) 에 annotate 된
    // DMA 버퍼가 하나라도 있으면 개발자가 보완한 것으로 보고 warning 으로 낮춘다.
    // (안전성: 놓친 버퍼로의 DMA 는 런타임에 TTT/TTAT 가 거부 = fail-closed. 기능 문제일 뿐 보호 우회 아님)
    {
      std::set<std::string> AnnTasks;
      bool AnnShared = false;
      for (auto &[G, B] : S.Bufs)
        if (!B.Forced.empty()) {
          if (StringRef(B.Forced).starts_with("shared:"))
            AnnShared = true;
          else
            AnnTasks.insert(B.Forced);
        }
      for (json::Value &V : S.Diags) {
        json::Object *O = V.getAsObject();
        if (O->getString("kind").value_or("") != "unresolved-load" ||
            O->getString("severity").value_or("") != "error")
          continue;
        std::string T = O->getString("task").value_or("").str();
        if (AnnTasks.count(T) || AnnShared) {
          (*O)["severity"] = "warning";
          (*O)["msg"] = (O->getString("msg").value_or("") +
                         " [annotate(\"dma:...\") 버퍼가 있어 warning 으로 낮춤]").str();
          --S.Errors;
          errs() << "[dma-cluster] (위 unresolved-load, " << T
                 << ") annotate 버퍼가 있어 warning 으로 낮춤\n";
        }
      }
    }
    for (auto &[G, B] : S.Bufs) {
      if (B.Forced.empty())
        continue;
      // annotate 로 지정된 버퍼도 sink 경유 버퍼와 같은 배치 조건을 만족해야 한다
      // (NOLOAD cluster → 초기값이 있으면 조용히 사라짐, 상수/외부 정의는 배치 불가).
      std::string Why;
      if (G->isDeclaration())
        Why = "정의가 LTO 모듈 밖에 있음";
      else if (G->isConstant())
        Why = "상수 (Flash) 는 cluster 에 둘 수 없음";
      else if (G->hasInitializer() && !G->getInitializer()->isNullValue())
        Why = "0 이 아닌 초기값. DMA cluster 는 NOLOAD(0 초기화)";
      else if (G->hasSection() && !G->getSection().starts_with(".dma."))
        Why = ("이미 section 지정: " + G->getSection()).str();
      StringRef F = B.Forced;
      if (Why.empty() && !F.starts_with("shared:") && !Tasks.count(F.str()))
        Why = ("annotate 의 태스크 '" + F + "' 가 태스크 entry 목록에 없음").str();
      if (Why.empty() && F.starts_with("shared:") && F.size() == 7)
        Why = "annotate(\"dma:shared:\") 에 이름이 없음";
      if (!Why.empty()) {
        S.diag("annotated-unplaceable", "error", F, nullptr,
               (G->getName() + ": " + Why).str());
        B.Forced.clear();
        B.AutoOwners.clear(); // 배치하지 않는다
        continue;
      }
      if (!F.starts_with("shared:") && !B.AutoOwners.empty() &&
          (B.AutoOwners.size() != 1 || *B.AutoOwners.begin() != F.str())) {
        std::string J;
        for (const std::string &O : B.AutoOwners)
          J += (J.empty() ? "" : ",") + O;
        S.diag("annotate-override", "warning", F, nullptr,
               (G->getName() + ": 자동 판정 owner {" + J +
                "} 를 annotate 로 덮어씀 → 다른 태스크의 NS-MPU 에는 넣지 않음")
                   .str());
      }
    }

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
    // 버퍼 순서 = 이름 순 (S.Bufs 는 포인터 키 → 실행마다 순서가 다를 수 있음)
    for (auto &[Key, Gs] : Clusters)
      llvm::sort(Gs, [](GlobalVariable *A, GlobalVariable *B) {
        return A->getName() < B->getName();
      });

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
        // 태스크 전용(annotate 포함): owner = 그 태스크 하나. 공유: 실제 사용 태스크들.
        if (Shared)
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
      else if (Owners.empty())
        S.diag("shared-no-owner", "warning", Key, nullptr,
               "공유 cluster 를 쓰는 태스크가 분석되지 않음 → 어느 태스크 NS-MPU 에도 들어가지 않음");
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

    // 진단 순서: 함수 순회 순서와 무관하게 (loc, kind, msg) 순
    std::vector<json::Value> DV(S.Diags.begin(), S.Diags.end());
    auto key = [](const json::Value &V) {
      const json::Object *O = V.getAsObject();
      auto g = [&](StringRef K) { return O->getString(K).value_or("").str(); };
      return g("loc") + "\x1f" + g("kind") + "\x1f" + g("task") + "\x1f" + g("msg");
    };
    llvm::sort(DV, [&](const json::Value &A, const json::Value &B) {
      return key(A) < key(B);
    });
    S.Diags = json::Array(DV);
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
