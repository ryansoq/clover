#ifndef __CPU_KERNEL_H__
#define __CPU_KERNEL_H__

#include "../deviceinterface.h"

#include <llvm/ExecutionEngine/GenericValue.h>
#include <vector>
#include <string>
#include <pthread.h>

namespace llvm
{
    class Function;
}

namespace Coal
{

class CPUDevice;
class Kernel;
class KernelEvent;

class CPUKernel : public DeviceKernel
{
    public:
        CPUKernel(CPUDevice *device, Kernel *kernel, llvm::Function *function);
        ~CPUKernel();

        size_t workGroupSize() const;
        cl_ulong localMemSize() const;
        cl_ulong privateMemSize() const;
        size_t preferredWorkGroupSizeMultiple() const;
        size_t guessWorkGroupSize(cl_uint num_dims, cl_uint dim,
                                  size_t global_work_size) const;

        Kernel *kernel() const;
        CPUDevice *device() const;

        llvm::Function *function() const;
        llvm::Function *callFunction(std::vector<void *> &freeLocal);

    private:
        CPUDevice *p_device;
        Kernel *p_kernel;
        llvm::Function *p_function, *p_call_function;
        pthread_mutex_t p_call_function_mutex;
};

class CPUKernelEvent;

class CPUKernelWorkGroup
{
    public:
        CPUKernelWorkGroup(CPUKernel *kernel, KernelEvent *event,
                           CPUKernelEvent *cpu_event,
                           const size_t *work_group_index);
        ~CPUKernelWorkGroup();

        bool run();

        // Native functions
        size_t getGlobalId(cl_uint dimindx) const;
        cl_uint getWorkDim() const;
        size_t getGlobalSize(cl_uint dimindx) const;
        size_t getLocalSize(cl_uint dimindx) const;
        size_t getLocalID(cl_uint dimindx) const;
        size_t getNumGroups(cl_uint dimindx) const;
        size_t getGroupID(cl_uint dimindx) const;
        size_t getGlobalOffset(cl_uint dimindx) const;

        void builtinNotFound(const std::string &name) const;

    private:
        CPUKernel *p_kernel;
        CPUKernelEvent *p_cpu_event;
        KernelEvent *p_event;
        size_t *p_index, *p_current, *p_maxs;
        size_t p_table_sizes;
};

class CPUKernelEvent
{
    public:
        CPUKernelEvent(CPUDevice *device, KernelEvent *event);
        ~CPUKernelEvent();

        bool reserve();  /*!< The next Work Group that will execute will be the last. Locks the event */
        bool finished(); /*!< All the work groups have finished */
        CPUKernelWorkGroup *takeInstance(); /*!< Must be called exactly one time after reserve(). Unlocks the event */

        void workGroupFinished();

    private:
        CPUDevice *p_device;
        KernelEvent *p_event;
        size_t *p_current_work_group, *p_max_work_groups;
        size_t p_table_sizes;
        size_t p_current_wg, p_finished_wg, p_num_wg;
        pthread_mutex_t p_mutex;
};

}

void setThreadLocalWorkGroup(Coal::CPUKernelWorkGroup *current);
void *getBuiltin(const std::string &name);

#endif
