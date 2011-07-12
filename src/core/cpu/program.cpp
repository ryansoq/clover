#include "program.h"
#include "device.h"

#include "../program.h"

#include <llvm/PassManager.h>
#include <llvm/Analysis/Passes.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/IPO.h>

using namespace Coal;

CPUProgram::CPUProgram(CPUDevice *device, Program *program)
: DeviceProgram(), p_device(device), p_program(program)
{

}

CPUProgram::~CPUProgram()
{

}

bool CPUProgram::linkStdLib() const
{
    return true;
}

void CPUProgram::createOptimizationPasses(llvm::PassManager *manager, bool optimize)
{
    if (optimize)
    {
        /*
         * Inspired by code from "The LLVM Compiler Infrastructure"
         */
        manager->add(llvm::createDeadArgEliminationPass());
        manager->add(llvm::createInstructionCombiningPass());
        manager->add(llvm::createFunctionInliningPass());
        manager->add(llvm::createPruneEHPass());   // Remove dead EH info.
        manager->add(llvm::createGlobalOptimizerPass());
        manager->add(llvm::createGlobalDCEPass()); // Remove dead functions.
        manager->add(llvm::createArgumentPromotionPass());
        manager->add(llvm::createInstructionCombiningPass());
        manager->add(llvm::createJumpThreadingPass());
        manager->add(llvm::createScalarReplAggregatesPass());
        manager->add(llvm::createFunctionAttrsPass()); // Add nocapture.
        manager->add(llvm::createGlobalsModRefPass()); // IP alias analysis.
        manager->add(llvm::createLICMPass());      // Hoist loop invariants.
        manager->add(llvm::createGVNPass());       // Remove redundancies.
        manager->add(llvm::createMemCpyOptPass()); // Remove dead memcpys.
        manager->add(llvm::createDeadStoreEliminationPass());
        manager->add(llvm::createInstructionCombiningPass());
        manager->add(llvm::createJumpThreadingPass());
        manager->add(llvm::createCFGSimplificationPass());
    }
}

bool CPUProgram::build(const llvm::Module *module)
{
    // Nothing to build
    return true;
}