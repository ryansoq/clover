#include "CL/cl.h"
#include <core/program.h>
#include <core/context.h>

#include <cstdlib>

// Program Object APIs
cl_program
clCreateProgramWithSource(cl_context        context,
                          cl_uint           count,
                          const char **     strings,
                          const size_t *    lengths,
                          cl_int *          errcode_ret)
{
    cl_int dummy_errcode;

    if (!errcode_ret)
        errcode_ret = &dummy_errcode;

    if (!context)
    {
        *errcode_ret = CL_INVALID_CONTEXT;
        return 0;
    }

    if (!count || !strings)
    {
        *errcode_ret = CL_INVALID_VALUE;
        return 0;
    }

    Coal::Program *program = new Coal::Program(context);

    *errcode_ret = CL_SUCCESS;
    *errcode_ret = program->loadSources(count, strings, lengths);

    if (*errcode_ret != CL_SUCCESS)
    {
        delete program;
        return 0;
    }

    return (cl_program)program;
}

cl_program
clCreateProgramWithBinary(cl_context            context,
                          cl_uint               num_devices,
                          const cl_device_id *  device_list,
                          const size_t *        lengths,
                          const unsigned char **binaries,
                          cl_int *              binary_status,
                          cl_int *              errcode_ret)
{
    cl_int dummy_errcode;

    if (!errcode_ret)
        errcode_ret = &dummy_errcode;

    if (!context)
    {
        *errcode_ret = CL_INVALID_CONTEXT;
        return 0;
    }

    if (!num_devices || !device_list || !lengths || !binaries)
    {
        *errcode_ret = CL_INVALID_VALUE;
        return 0;
    }

    // Check the devices for compliance
    cl_uint context_num_devices = 0;
    cl_device_id *context_devices;

    *errcode_ret = context->info(CL_CONTEXT_NUM_DEVICES, sizeof(cl_uint),
                                 &context_num_devices, 0);

    if (*errcode_ret != CL_SUCCESS)
        return 0;

    context_devices =
        (cl_device_id *)std::malloc(context_num_devices * sizeof(cl_device_id));

    *errcode_ret = context->info(CL_CONTEXT_DEVICES,
                                 context_num_devices * sizeof(cl_device_id),
                                 context_devices, 0);

    if (*errcode_ret != CL_SUCCESS)
        return 0;

    for (int i=0; i<num_devices; ++i)
    {
        bool found = false;

        if (!lengths[i] || !binaries[i])
        {
            if (binary_status)
                binary_status[i] = CL_INVALID_VALUE;

            *errcode_ret = CL_INVALID_VALUE;
            return 0;
        }

        for (int j=0; j<context_num_devices; ++j)
        {
            if (device_list[i] == context_devices[j])
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            *errcode_ret = CL_INVALID_DEVICE;
            return 0;
        }
    }

    // Create a program
    Coal::Program *program = new Coal::Program(context);
    *errcode_ret = CL_SUCCESS;

    // Init program
    *errcode_ret = program->loadBinaries(binaries,
                                         lengths, binary_status, num_devices,
                                         (Coal::DeviceInterface * const*)device_list);

    if (*errcode_ret != CL_SUCCESS)
    {
        delete program;
        return 0;
    }

    return (cl_program)program;
}

cl_int
clRetainProgram(cl_program program)
{
    if (!program)
        return CL_INVALID_PROGRAM;

    program->reference();

    return CL_SUCCESS;
}

cl_int
clReleaseProgram(cl_program program)
{
    if (!program)
        return CL_INVALID_PROGRAM;

    if (program->dereference())
        delete program;

    return CL_SUCCESS;
}

cl_int
clBuildProgram(cl_program           program,
               cl_uint              num_devices,
               const cl_device_id * device_list,
               const char *         options,
               void (*pfn_notify)(cl_program program, void * user_data),
               void *               user_data)
{
    if (!program)
        return CL_INVALID_PROGRAM;

    if (!device_list && num_devices > 0)
        return CL_INVALID_VALUE;

    if (!num_devices && device_list)
        return CL_INVALID_VALUE;

    if (!pfn_notify && user_data)
        return CL_INVALID_VALUE;

    // Check the devices for compliance
    if (num_devices)
    {
        cl_uint context_num_devices = 0;
        cl_device_id *context_devices;
        Coal::Context *context = program->context();
        cl_int result;

        result = context->info(CL_CONTEXT_NUM_DEVICES, sizeof(cl_uint),
                                     &context_num_devices, 0);

        if (result != CL_SUCCESS)
            return result;

        context_devices =
            (cl_device_id *)std::malloc(context_num_devices * sizeof(cl_device_id));

        result = context->info(CL_CONTEXT_DEVICES,
                                     context_num_devices * sizeof(cl_device_id),
                                     context_devices, 0);

        if (result != CL_SUCCESS)
            return result;

        for (int i=0; i<num_devices; ++i)
        {
            bool found = false;

            for (int j=0; j<context_num_devices; ++j)
            {
                if (device_list[i] == context_devices[j])
                {
                    found = true;
                    break;
                }
            }

            if (!found)
                return CL_INVALID_DEVICE;
        }
    }

    // We cannot try to build a previously-failed program
    if (program->state() != Coal::Program::Loaded)
        return CL_INVALID_OPERATION;

    // Build program
    return program->build(options, pfn_notify, user_data, num_devices,
                          (Coal::DeviceInterface * const*)device_list);
}

cl_int
clUnloadCompiler(void)
{
    return CL_SUCCESS;
}

cl_int
clGetProgramInfo(cl_program         program,
                 cl_program_info    param_name,
                 size_t             param_value_size,
                 void *             param_value,
                 size_t *           param_value_size_ret)
{
    if (!program)
        return CL_INVALID_PROGRAM;

    return program->info(param_name, param_value_size, param_value,
                         param_value_size_ret);
}

cl_int
clGetProgramBuildInfo(cl_program            program,
                      cl_device_id          device,
                      cl_program_build_info param_name,
                      size_t                param_value_size,
                      void *                param_value,
                      size_t *              param_value_size_ret)
{
    if (!program)
        return CL_INVALID_PROGRAM;

    return program->buildInfo((Coal::DeviceInterface *)device, param_name,
                              param_value_size, param_value,
                              param_value_size_ret);
}
