//===- SecureGuardSelect.cpp - Secure 데이터 중 guard 블록에 옮겨도 되는 것 고르기 -===//
//
// LLVM 18, New Pass Manager plugin.   opt -load-pass-plugin=SecureGuardSelect.so
//                                         -passes=secure-guard-select -disable-output secure.bc
//
// 입력: Secure 이미지 전체의 IR (clang -emit-llvm 으로 만든 뒤 llvm-link 로 합친 것).
//       Secure 실제 빌드는 GCC 그대로 두고, 이 pass 는 "분석"만 한다.
//
// 판정: 전역 변수 G 를 MPCBB guard 블록(NS 주소 범위에 있는 Secure 256 B 블록)으로
//       옮겨도 되는가?
//   (1) 정의가 있고, 상수가 아니며, 0 초기화(.bss)일 것
//   (2) 크기 ≤ 256 B (guard 블록은 서로 떨어져 있어 한 객체가 블록을 넘을 수 없음)
//   (3) "부팅 경로"에서 쓰이지 않을 것 — dma_boot_config() 가 guard 를 Secure 로
//       만들기 전에는 그 블록이 아직 NS 메모리이기 때문.
//       부팅 경로 = SystemInit, 전역 생성자, main 안에서 boot 호출보다 앞(지배되지 않는)
//                   명령이 부르는 모든 함수, boot 함수 자신, 부팅 중 켜질 수 있는
//                   인터럽트 핸들러(*_Handler, *_IRQHandler; 설정으로 예외 지정)
//   (4) 이름에 '.' 이 없을 것 (llvm-link 가 이름 충돌로 바꾼 이름이나 함수 안 static
//       은 GCC 의 섹션 이름 .bss.<name> 과 대응시킬 수 없음)
//   (5) 이미 section 이 지정되지 않았을 것, 제외 목록에 없을 것
//
// 모드: "annotated" (기본) — __attribute__((annotate("guard"))) 붙은 것만 채택,
//                            나머지 안전한 것은 "possible" 로 보고만
//       "auto"            — 안전한 것 전부 채택
// 설정: 환경 변수 SECURE_GUARD_CONFIG=<config/secure_guard_config.json>
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdlib>
#include <map>
#include <set>
#include <string>

using namespace llvm;

namespace {

struct Config {
  std::string Out = "secure_guard_candidates.json";
  std::string BootFn = "dma_boot_config";
  bool Auto = false;
  unsigned MaxSize = 256;
  std::set<std::string> PostBootHandlers; // 부팅이 끝난 뒤에만 켜지는 핸들러
  std::set<std::string> Exclude;          // 어셈블리(.S)에서만 쓰는 전역 등 IR 이 모르는 사용처
};

static Config loadConfig() {
  Config C;
  const char *P = std::getenv("SECURE_GUARD_CONFIG");
  if (!P)
    return C;
  auto Buf = MemoryBuffer::getFile(P);
  if (!Buf)
    return C;
  Expected<json::Value> J = json::parse((*Buf)->getBuffer());
  if (!J) {
    errs() << "[guard-select] JSON 오류: " << toString(J.takeError()) << "\n";
    return C;
  }
  const json::Object *O = J->getAsObject();
  if (!O)
    return C;
  if (auto S = O->getString("out"))
    C.Out = S->str();
  if (auto S = O->getString("boot_fn"))
    C.BootFn = S->str();
  if (auto S = O->getString("mode"))
    C.Auto = (*S == "auto");
  if (auto N = O->getInteger("max_size"))
    C.MaxSize = static_cast<unsigned>(*N);
  auto readSet = [&](StringRef Key, std::set<std::string> &Dst) {
    if (const json::Array *A = O->getArray(Key))
      for (const json::Value &V : *A)
        if (auto S = V.getAsString())
          Dst.insert(S->str());
  };
  if (const char *Ov = std::getenv("SECURE_GUARD_OUT"))
    C.Out = Ov;
  readSet("post_boot_handlers", C.PostBootHandlers);
  readSet("exclude", C.Exclude);
  return C;
}

using FnSet = SmallPtrSet<Function *, 32>; // SmallSize 상한 32 (LLVM 18)

static Function *calleeOf(const CallBase &CB) {
  return dyn_cast<Function>(CB.getCalledOperand()->stripPointerCasts());
}

// 함수 F 에서 시작해 호출될 수 있는 모든 함수 (간접 호출은 같은 타입의 address-taken 함수 전부)
static void reachInto(Function *Root, FnSet &Seen,
                      const std::map<FunctionType *, std::vector<Function *>> &AT) {
  SmallVector<Function *, 32> WL{Root};
  while (!WL.empty()) {
    Function *F = WL.pop_back_val();
    if (!F || !Seen.insert(F).second || F->isDeclaration())
      continue;
    for (Instruction &I : instructions(*F))
      if (auto *CB = dyn_cast<CallBase>(&I)) {
        if (CB->isInlineAsm())
          continue;
        if (Function *C = calleeOf(*CB))
          WL.push_back(C);
        else if (auto It = AT.find(CB->getFunctionType()); It != AT.end())
          for (Function *T : It->second)
            WL.push_back(T);
      }
  }
}

// 명령어 하나가 직접·간접(상수식 안)으로 가리키는 전역 변수
static void globalsIn(const Value *V, std::set<const GlobalVariable *> &Out,
                      SmallPtrSetImpl<const Value *> &Seen) {
  if (!Seen.insert(V).second)
    return;
  if (auto *G = dyn_cast<GlobalVariable>(V)) {
    Out.insert(G);
    return;
  }
  if (auto *C = dyn_cast<Constant>(V))
    for (const Use &U : C->operands())
      globalsIn(U.get(), Out, Seen);
}

static void globalsOfInst(const Instruction &I, std::set<const GlobalVariable *> &Out) {
  SmallPtrSet<const Value *, 16> Seen;
  for (const Use &U : I.operands())
    if (isa<Constant>(U.get()))
      globalsIn(U.get(), Out, Seen);
}

static std::set<std::string> annotated(Module &M) {
  std::set<std::string> R;
  GlobalVariable *A = M.getGlobalVariable("llvm.global.annotations");
  if (!A || !A->hasInitializer())
    return R;
  if (auto *Arr = dyn_cast<ConstantArray>(A->getInitializer()))
    for (const Use &U : Arr->operands())
      if (auto *CS = dyn_cast<ConstantStruct>(U.get()); CS && CS->getNumOperands() >= 2)
        if (auto *G = dyn_cast<GlobalVariable>(CS->getOperand(0)->stripPointerCasts()))
          if (auto *SG = dyn_cast<GlobalVariable>(CS->getOperand(1)->stripPointerCasts());
              SG && SG->hasInitializer())
            if (auto *CDA = dyn_cast<ConstantDataArray>(SG->getInitializer());
                CDA && CDA->isCString() && CDA->getAsCString() == "guard")
              R.insert(G->getName().str());
  return R;
}

struct SecureGuardSelectPass : PassInfoMixin<SecureGuardSelectPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
    const Config Cfg = loadConfig();
    FunctionAnalysisManager &FAM =
        MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

    std::map<FunctionType *, std::vector<Function *>> AT;
    for (Function &F : M)
      if (!F.isDeclaration() && F.hasAddressTaken())
        AT[F.getFunctionType()].push_back(&F);

    // ---- 부팅 경로 함수 집합 ----
    FnSet Boot;
    json::Array Notes;
    auto addRoot = [&](StringRef N) {
      if (Function *F = M.getFunction(N))
        reachInto(F, Boot, AT);
    };
    addRoot("SystemInit");
    addRoot(Cfg.BootFn); // boot 함수 자신도 MPCBB 설정 "전"에 시작한다
    if (GlobalVariable *Ctors = M.getGlobalVariable("llvm.global_ctors"))
      if (Ctors->hasInitializer())
        if (auto *Arr = dyn_cast<ConstantArray>(Ctors->getInitializer()))
          for (const Use &U : Arr->operands())
            if (auto *CS = dyn_cast<ConstantStruct>(U.get()); CS && CS->getNumOperands() >= 2)
              if (auto *F = dyn_cast<Function>(CS->getOperand(1)->stripPointerCasts()))
                reachInto(F, Boot, AT);
    for (Function &F : M) {
      StringRef N = F.getName();
      if ((N.ends_with("_Handler") || N.ends_with("_IRQHandler")) &&
          !Cfg.PostBootHandlers.count(N.str()))
        reachInto(&F, Boot, AT); // 부팅 중 켜질 수 있다고 보수적으로 가정
    }

    std::set<const GlobalVariable *> BootUsed;
    if (Function *Main = M.getFunction("main"); Main && !Main->isDeclaration()) {
      CallBase *BootCall = nullptr;
      for (Instruction &I : instructions(*Main))
        if (auto *CB = dyn_cast<CallBase>(&I))
          if (Function *C = calleeOf(*CB); C && C->getName() == Cfg.BootFn) {
            BootCall = CB;
            break;
          }
      if (!BootCall)
        Notes.push_back("main 안에서 boot 함수 호출을 찾지 못함: main 전체를 부팅 경로로 취급");
      DominatorTree &DT = FAM.getResult<DominatorTreeAnalysis>(*Main);
      for (Instruction &I : instructions(*Main)) {
        // boot 호출에 지배되는 명령 = boot 가 끝난 "뒤"에만 실행. 나머지는 전부 "앞" 으로 본다.
        bool After = BootCall && &I != BootCall && DT.dominates(BootCall, &I);
        if (After)
          continue;
        globalsOfInst(I, BootUsed);
        if (auto *CB = dyn_cast<CallBase>(&I); CB && !CB->isInlineAsm()) {
          if (Function *C = calleeOf(*CB))
            reachInto(C, Boot, AT);
          else if (auto It = AT.find(CB->getFunctionType()); It != AT.end())
            for (Function *T : It->second)
              reachInto(T, Boot, AT);
        }
      }
    } else {
      Notes.push_back("main 이 IR 에 없음");
    }
    for (Function *F : Boot)
      if (!F->isDeclaration())
        for (Instruction &I : instructions(*F))
          globalsOfInst(I, BootUsed);

    // ---- 후보 판정 ----
    const std::set<std::string> Ann = annotated(M);
    const DataLayout &DL = M.getDataLayout();
    json::Array Chosen, Possible, Rejected;
    for (GlobalVariable &G : M.globals()) {
      StringRef N = G.getName();
      if (N.starts_with("llvm.") || N.starts_with("__"))
        continue;
      bool IsAnn = Ann.count(N.str());
      std::string Why;
      uint64_t Sz = G.isDeclaration() ? 0 : DL.getTypeAllocSize(G.getValueType());
      if (G.isDeclaration())
        Why = "선언만 있음";
      else if (G.isConstant())
        Why = "상수 (Flash)";
      else if (!G.getInitializer()->isNullValue())
        Why = "0 이 아닌 초기값 (.data)";
      else if (G.isThreadLocal())
        Why = "thread_local";
      else if (G.hasSection())
        Why = ("이미 section 지정: " + G.getSection()).str();
      else if (N.contains('.'))
        Why = "이름에 '.' (함수 안 static 또는 이름 충돌로 변경됨)";
      else if (Cfg.Exclude.count(N.str()))
        Why = "설정의 exclude 목록";
      else if (Sz == 0 || Sz > Cfg.MaxSize)
        Why = formatv("크기 {0} B > {1} B", Sz, Cfg.MaxSize).str();
      else if (BootUsed.count(&G))
        Why = "부팅 경로(MPCBB 설정 전)에서 사용";

      uint64_t Al = G.getAlign() ? G.getAlign()->value() : DL.getPreferredAlign(&G).value();
      json::Object Rec{{"name", N.str()},
                       {"size", static_cast<int64_t>(Sz)},
                       {"align", static_cast<int64_t>(Al)},
                       {"annotated", IsAnn},
                       {"gcc_section", (".bss." + N).str()}};
      if (!Why.empty()) {
        if (IsAnn) // 표시했는데 안전하지 않음 → 반드시 알려야 함
          errs() << "[guard-select] error: " << N << " 는 annotate(\"guard\") 이지만 "
                 << Why << "\n";
        Rec["reason"] = Why;
        if (IsAnn || Sz <= Cfg.MaxSize)
          Rejected.push_back(std::move(Rec));
        continue;
      }
      if (IsAnn || Cfg.Auto)
        Chosen.push_back(std::move(Rec));
      else
        Possible.push_back(std::move(Rec));
    }

    json::Object Root{{"version", 1},
                      {"mode", Cfg.Auto ? "auto" : "annotated"},
                      {"boot_fn", Cfg.BootFn},
                      {"boot_functions", static_cast<int64_t>(Boot.size())},
                      {"chosen", std::move(Chosen)},
                      {"possible", std::move(Possible)},
                      {"rejected", std::move(Rejected)},
                      {"notes", std::move(Notes)}};
    std::error_code EC;
    raw_fd_ostream OS(Cfg.Out, EC);
    if (EC)
      errs() << "[guard-select] 출력 실패: " << Cfg.Out << "\n";
    else
      OS << formatv("{0:2}", json::Value(std::move(Root))) << "\n";
    return PreservedAnalyses::all(); // 분석만 한다 (IR 변경 없음)
  }
  static bool isRequired() { return true; }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "SecureGuardSelect", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name != "secure-guard-select")
                    return false;
                  MPM.addPass(SecureGuardSelectPass());
                  return true;
                });
          }};
}
