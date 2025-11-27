
#include "CustomPasses.hpp"
#include "lifterClass.hpp"
#include "utils.h"
#include <llvm/ADT/DenseMap.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Transforms/IPO/GlobalDCE.h>
#include <llvm/Transforms/IPO/StripDeadPrototypes.h>
#include <llvm/Transforms/Scalar/ADCE.h>
#include <llvm/Transforms/Scalar/DCE.h>
#include <llvm/Transforms/Scalar/EarlyCSE.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Transforms/Scalar/InstCombine.h>
#include <llvm/Transforms/Scalar/PromoteMemToReg.h>
#include <llvm/Transforms/Scalar/SimplifyCFG.h>

using namespace llvm;

// not pathsolver, & probably put this in lifter class, & we would utilize
// templates since geploadpass
MERGEN_LIFTER_DEFINITION_TEMPLATES(void)::run_opts() {
  llvm::PassBuilder passBuilder;

  llvm::LoopAnalysisManager loopAnalysisManager;
  llvm::FunctionAnalysisManager functionAnalysisManager;
  llvm::CGSCCAnalysisManager cGSCCAnalysisManager;
  llvm::ModuleAnalysisManager moduleAnalysisManager;

  passBuilder.registerModuleAnalyses(moduleAnalysisManager);
  passBuilder.registerCGSCCAnalyses(cGSCCAnalysisManager);
  passBuilder.registerFunctionAnalyses(functionAnalysisManager);
  passBuilder.registerLoopAnalyses(loopAnalysisManager);
  passBuilder.crossRegisterProxies(loopAnalysisManager, functionAnalysisManager,
                                   cGSCCAnalysisManager, moduleAnalysisManager);

  llvm::FunctionPassManager earlyCleanup;
  earlyCleanup.addPass(llvm::PromotePass());
  earlyCleanup.addPass(llvm::InstCombinePass());
  earlyCleanup.addPass(llvm::SimplifyCFGPass());
  earlyCleanup.addPass(llvm::EarlyCSEPass(true));
  earlyCleanup.addPass(llvm::DCEPass());

  llvm::FunctionPassManager lateCleanup;
  lateCleanup.addPass(llvm::InstCombinePass());
  lateCleanup.addPass(llvm::SimplifyCFGPass());
  lateCleanup.addPass(llvm::GVN());
  lateCleanup.addPass(llvm::ADCEPass());

  llvm::ModulePassManager modulePassManager;
  modulePassManager.addPass(
      llvm::createModuleToFunctionPassAdaptor(std::move(earlyCleanup)));
  modulePassManager.addPass(GEPLoadPass(fnc->getArg(fnc->arg_size()),
                                        this->fileBase, memoryPolicy));
  modulePassManager.addPass(ReplaceTruncWithLoadPass());
  modulePassManager.addPass(
      PromotePseudoStackPass(fnc->getArg(fnc->arg_size() - 1)));
  modulePassManager.addPass(
      PromotePseudoMemory(fnc->getArg(fnc->arg_size() - 1)));
  modulePassManager.addPass(
      llvm::createModuleToFunctionPassAdaptor(std::move(lateCleanup)));
  modulePassManager.addPass(llvm::GlobalDCEPass());
  modulePassManager.addPass(llvm::StripDeadPrototypesPass());

  llvm::Module* module = this->fnc->getParent();

  // Run the optimisation stack until it stops making visible progress or we
  // reach a small iteration cap. This keeps the cost predictable while still
  // collapsing obvious dead/duplicate constraint blocks.
  size_t previousSize = module->getInstructionCount();
  for (unsigned iteration = 0; iteration < 3; ++iteration) {
    modulePassManager.run(*module, moduleAnalysisManager);

    const size_t currentSize = module->getInstructionCount();
    if (currentSize >= previousSize)
      break;
    previousSize = currentSize;
  }
}
