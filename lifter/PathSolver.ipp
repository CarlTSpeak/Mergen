#pragma once

#include "CommonDisassembler.hpp"
#include "PathSolver.h"
#include "lifterClass.hpp"
#include "utils.h"

#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Analysis/MemorySSA.h>
#include <llvm/Analysis/MemorySSAUpdater.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/Casting.h>

static std::atomic<std::uint64_t> gSolveCalls{0};
static std::atomic<std::uint64_t> gSolveConstInt{0};
static std::atomic<std::uint64_t> gSolveConstraint{0};
static std::atomic<std::uint64_t> gSolvePv0{0};
static std::atomic<std::uint64_t> gSolvePv1{0};
static std::atomic<std::uint64_t> gSolvePv2{0};

MERGEN_LIFTER_DEFINITION_TEMPLATES(PATH_info)
::solvePath(llvm::Function* function, uint64_t& dest,
            llvm::Value* simplifyValue) {
  
  ++gSolveCalls;
  PATH_info result = PATH_unsolved;

  // --- Module range helpers (unchanged in spirit) ---
  uint64_t moduleMin = 0;
  uint64_t moduleMax = 0;

  auto initModuleBounds = [&]() {
    if (moduleMax != 0)
      return;

    uint64_t base = this->file.imageBase;
    uint64_t maxEnd = base;

    for (const auto& sec : this->file.sections_v) {
      uint64_t start = base + sec.virtual_address;
      uint64_t end = start + sec.virtual_size;
      if (end > maxEnd)
        maxEnd = end;
    }

    moduleMin = base;
    moduleMax = maxEnd;
  };

  auto isValidTarget = [&](uint64_t target) -> bool {
    initModuleBounds();
    return target >= moduleMin && target < moduleMax;
  };

  // Helper: schedule a block for a concrete target.
  auto scheduleTargetBB = [&](uint64_t target,
                              const std::string& label) -> BBInfo {
    auto* bb = getOrCreateBB(target, label);
    BBInfo info(target, bb);

    // We only schedule here. No CFG mutation.
    addUnvisitedAddr(info);
    return info;
  };

  // --- 1) Trivial constant: immediate solve ---
  if (auto* constInt = llvm::dyn_cast<llvm::ConstantInt>(simplifyValue)) {
    ++gSolveConstInt;
    dest = constInt->getZExtValue();
    result = PATH_solved;
    run = 0;

    if (!isValidTarget(dest)) {
      std::cerr << "[solvePath] constant dest out of range: 0x" << std::hex
                << dest << std::dec << "\n";
      return result;
    }

    scheduleTargetBB(dest, "bb_solved_const");
    return result;
  }

  // --- 2) Try constraint machinery ---
  {
    PATH_info solved = getConstraintVal(function, simplifyValue, dest);
    if (solved == PATH_solved) {
      ++gSolveConstraint;
      run = 0;

      if (!isValidTarget(dest)) {
        std::cerr << "[solvePath] constraint dest out of range: 0x" << std::hex
                  << dest << std::dec << "\n";
        return solved;
      }

      scheduleTargetBB(dest, "bb_solved");
      return solved;
    }
  }

  // --- 3) Unsolved: enumerate possible values ---
  printvalue(simplifyValue);
  run = 0;

  auto pvset = computePossibleValues(simplifyValue);
  std::vector<llvm::APInt> pv(pvset.begin(), pvset.end());



  std::cerr << "[solvePath] pv values:";
  for (const auto& v : pv) {
    std::cerr << " 0x" << std::hex << v.getZExtValue() << std::dec;
  }
  std::cerr << "\n";

  if (pv.empty()) {
    // Nothing we can do.
    ++gSolvePv0;
    return result;
  }

  // 3a) Exactly one possible value: treat as concrete, but keep PATH_unsolved.
  if (pv.size() == 1) {
    ++gSolvePv1;
    auto value = pv[0];
    uint64_t t = value.getZExtValue();
    printvalue2(value);

    if (!isValidTarget(t)) {
      std::cerr << "[solvePath] single pv target out of range: 0x" << std::hex
                << t << std::dec << "\n";
      return result;
    }

    scheduleTargetBB(t, "bb_single");
    return result;
  }

  // 3b) Exactly two possible values: we’ll leave CFG emission to caller,
  // but we still figure out the two targets and schedule them.
  if (pv.size() == 2) {
    ++gSolvePv2;
    auto firstCase = pv[0];
    auto secondCase = pv[1];

    printvalueforce2(pv.size());
    printvalue2(firstCase);
    printvalue2(secondCase);

    uint64_t trueTarget = firstCase.getZExtValue();
    uint64_t falseTarget = secondCase.getZExtValue();

    if (!isValidTarget(trueTarget) || !isValidTarget(falseTarget)) {
      std::cerr << "[solvePath] branch targets out of range: true=0x"
                << std::hex << trueTarget << " false=0x" << falseTarget
                << std::dec << "\n";
      return result;
    }

    // Schedule both as potential successors. Caller decides how to wire CFG.
    scheduleTargetBB(trueTarget, "bb_true");
    scheduleTargetBB(falseTarget, "bb_false");

    // For now, we still consider PATH_unsolved: we haven't collapsed the
    // condition, just enumerated.
    return result;
  }

 
  return result;
}
