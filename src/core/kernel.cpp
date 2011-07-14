#include "kernel.h"
#include "propertylist.h"
#include "program.h"
#include "memobject.h"
#include "deviceinterface.h"

#include <string>
#include <iostream>
#include <cstring>
#include <cstdlib>

#include <llvm/Support/Casting.h>
#include <llvm/Module.h>
#include <llvm/Type.h>
#include <llvm/DerivedTypes.h>

using namespace Coal;
Kernel::Kernel(Program *program)
: p_program(program), p_references(1), p_local_args(false)
{
    clRetainProgram((cl_program)program); // TODO: Say a kernel is attached to the program (that becomes unalterable)

    null_dep.device = 0;
    null_dep.kernel = 0;
    null_dep.function = 0;
    null_dep.module = 0;
}

Kernel::~Kernel()
{
    clReleaseProgram((cl_program)p_program);

    while (p_device_dependent.size())
    {
        DeviceDependent &dep = p_device_dependent.back();

        delete dep.kernel;

        p_device_dependent.pop_back();
    }
}

void Kernel::reference()
{
    p_references++;
}

bool Kernel::dereference()
{
    p_references--;
    return (p_references == 0);
}

const Kernel::DeviceDependent &Kernel::deviceDependent(DeviceInterface *device) const
{
    for (int i=0; i<p_device_dependent.size(); ++i)
    {
        const DeviceDependent &rs = p_device_dependent[i];

        if (rs.device == device || (!device && p_device_dependent.size() == 1))
            return rs;
    }

    return null_dep;
}

Kernel::DeviceDependent &Kernel::deviceDependent(DeviceInterface *device)
{
    for (int i=0; i<p_device_dependent.size(); ++i)
    {
        DeviceDependent &rs = p_device_dependent[i];

        if (rs.device == device || (!device && p_device_dependent.size() == 1))
            return rs;
    }

    return null_dep;
}

cl_int Kernel::addFunction(DeviceInterface *device, llvm::Function *function,
                           llvm::Module *module)
{
    p_name = function->getNameStr();

    // Add a device dependent
    DeviceDependent dep;

    dep.device = device;
    dep.function = function;
    dep.module = module;

    // Build the arg list of the kernel (or verify it if a previous function
    // was already registered)
    const llvm::FunctionType *f = function->getFunctionType();
    bool append = (p_args.size() == 0);

    if (!append && p_args.size() != f->getNumParams())
        return CL_INVALID_KERNEL_DEFINITION;

    for (int i=0; i<f->getNumParams(); ++i)
    {
        const llvm::Type *arg_type = f->getParamType(i);
        Arg::Kind kind = Arg::Invalid;
        Arg::File file = Arg::Private;
        unsigned short vec_dim = 1;

        if (arg_type->isPointerTy())
        {
            // It's a pointer, dereference it
            const llvm::PointerType *p_type = llvm::cast<llvm::PointerType>(arg_type);

            file = (Arg::File)p_type->getAddressSpace();
            arg_type = p_type->getElementType();

            // If it's a __local argument, we'll have to allocate memory at run time
            if (file == Arg::Local)
                p_local_args = true;

            // Get the name of the type to see if it's something like image2d, etc
            std::string name = module->getTypeName(arg_type);

            if (name == "image2d")
            {
                // TODO: Address space qualifiers for image types, and read_only
                kind = Arg::Image2D;
                file = Arg::Global;
            }
            else if (name == "image3d")
            {
                kind = Arg::Image3D;
                file = Arg::Global;
            }
            else if (name == "sampler")
            {
                // TODO: Sampler
            }
            else
            {
                kind = Arg::Buffer;
            }
        }
        else
        {
            if (arg_type->isVectorTy())
            {
                // It's a vector, we need its element's type
                const llvm::VectorType *v_type = llvm::cast<llvm::VectorType>(arg_type);

                vec_dim = v_type->getNumElements();
                arg_type = v_type->getElementType();
            }

            // Get type kind
            if (arg_type->isFloatTy())
            {
                kind = Arg::Float;
            }
            else if (arg_type->isDoubleTy())
            {
                kind = Arg::Double;
            }
            else if (arg_type->isIntegerTy())
            {
                const llvm::IntegerType *i_type = llvm::cast<llvm::IntegerType>(arg_type);

                if (i_type->getBitWidth() == 8)
                {
                    kind = Arg::Int8;
                }
                else if (i_type->getBitWidth() == 16)
                {
                    kind = Arg::Int16;
                }
                else if (i_type->getBitWidth() == 32)
                {
                    kind = Arg::Int32;
                }
                else if (i_type->getBitWidth() == 64)
                {
                    kind = Arg::Int64;
                }
            }
        }

        // Check if we recognized the type
        if (kind == Arg::Invalid)
            return CL_INVALID_KERNEL_DEFINITION;

        // Create arg
        Arg a(vec_dim, file, kind);

        // If we also have a function registered, check for signature compliance
        if (!append && a != p_args[i])
            return CL_INVALID_KERNEL_DEFINITION;

        // Append arg if needed
        if (append)
            p_args.push_back(a);
    }

    dep.kernel = device->createDeviceKernel(this, dep.function);
    p_device_dependent.push_back(dep);

    return CL_SUCCESS;
}

llvm::Function *Kernel::function(DeviceInterface *device) const
{
    const DeviceDependent &dep = deviceDependent(device);

    return dep.function;
}

cl_int Kernel::setArg(cl_uint index, size_t size, const void *value)
{
    if (index > p_args.size())
        return CL_INVALID_ARG_INDEX;

    Arg &arg = p_args[index];

    // Special case for __local pointers
    if (arg.file() == Arg::Local)
    {
        if (size == 0)
            return CL_INVALID_ARG_SIZE;

        if (value != 0)
            return CL_INVALID_ARG_VALUE;

        arg.setAllocAtKernelRuntime(size);

        return CL_SUCCESS;
    }

    // Check that size corresponds to the arg type
    size_t arg_size = arg.valueSize();

    if (size != arg_size)
        return CL_INVALID_ARG_SIZE;

    // Check for null values
    cl_mem null_mem = 0;

    if (!value)
    {
        switch (arg.kind())
        {
            case Arg::Buffer:
            case Arg::Image2D:
            case Arg::Image3D:
                // Special case buffers : value can be 0 (or point to 0)
                value = &null_mem;

            // TODO samplers
            default:
                return CL_INVALID_ARG_VALUE;
        }
    }

    // Copy the data
    arg.alloc();
    arg.loadData(value);

    return CL_SUCCESS;
}

unsigned int Kernel::numArgs() const
{
    return p_args.size();
}

const Kernel::Arg &Kernel::arg(unsigned int index) const
{
    return p_args.at(index);
}

Program *Kernel::program() const
{
    return p_program;
}

bool Kernel::argsSpecified() const
{
    for (int i=0; i<p_args.size(); ++i)
    {
        if (!p_args[i].defined())
            return false;
    }

    return true;
}

bool Kernel::needsLocalAllocation() const
{
    return p_local_args;
}

DeviceKernel *Kernel::deviceDependentKernel(DeviceInterface *device) const
{
    const DeviceDependent &dep = deviceDependent(device);

    return dep.kernel;
}

cl_int Kernel::info(cl_kernel_info param_name,
                    size_t param_value_size,
                    void *param_value,
                    size_t *param_value_size_ret)
{
    void *value = 0;
    size_t value_length = 0;

    union {
        cl_uint cl_uint_var;
        cl_program cl_program_var;
        cl_context cl_context_var;
    };

    switch (param_name)
    {
        case CL_KERNEL_FUNCTION_NAME:
            MEM_ASSIGN(p_name.size() + 1, p_name.c_str());
            break;

        case CL_KERNEL_NUM_ARGS:
            SIMPLE_ASSIGN(cl_uint, p_args.size());
            break;

        case CL_KERNEL_REFERENCE_COUNT:
            SIMPLE_ASSIGN(cl_uint, p_references);
            break;

        case CL_KERNEL_CONTEXT:
            SIMPLE_ASSIGN(cl_context, p_program->context());
            break;

        case CL_KERNEL_PROGRAM:
            SIMPLE_ASSIGN(cl_program, p_program);
            break;

        default:
            return CL_INVALID_VALUE;
    }

    if (param_value && param_value_size < value_length)
        return CL_INVALID_VALUE;

    if (param_value_size_ret)
        *param_value_size_ret = value_length;

    if (param_value)
        std::memcpy(param_value, value, value_length);

    return CL_SUCCESS;
}

cl_int Kernel::workGroupInfo(DeviceInterface *device,
                             cl_kernel_work_group_info param_name,
                             size_t param_value_size,
                             void *param_value,
                             size_t *param_value_size_ret)
{
    void *value = 0;
    size_t value_length = 0;

    union {
        size_t size_t_var;
        size_t three_size_t[3];
        cl_ulong cl_ulong_var;
    };

    DeviceDependent &dep = deviceDependent(device);

    switch (param_name)
    {
        case CL_KERNEL_WORK_GROUP_SIZE:
            SIMPLE_ASSIGN(size_t, dep.kernel->workGroupSize());
            break;

        case CL_KERNEL_COMPILE_WORK_GROUP_SIZE:
            // TODO: Get this information from the kernel source
            three_size_t[0] = 0;
            three_size_t[1] = 0;
            three_size_t[2] = 0;
            value = &three_size_t;
            value_length = sizeof(three_size_t);
            break;

        case CL_KERNEL_LOCAL_MEM_SIZE:
            SIMPLE_ASSIGN(cl_ulong, dep.kernel->localMemSize());
            break;

        case CL_KERNEL_PRIVATE_MEM_SIZE:
            SIMPLE_ASSIGN(cl_ulong, dep.kernel->privateMemSize());
            break;

        case CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE:
            SIMPLE_ASSIGN(size_t, dep.kernel->preferredWorkGroupSizeMultiple());
            break;

        default:
            return CL_INVALID_VALUE;
    }

    if (param_value && param_value_size < value_length)
        return CL_INVALID_VALUE;

    if (param_value_size_ret)
        *param_value_size_ret = value_length;

    if (param_value)
        std::memcpy(param_value, value, value_length);

    return CL_SUCCESS;
}

/*
 * Kernel::Arg
 */
Kernel::Arg::Arg(unsigned short vec_dim, File file, Kind kind)
: p_vec_dim(vec_dim), p_file(file), p_kind(kind), p_defined(false),
  p_runtime_alloc(0), p_data(0)
{

}

Kernel::Arg::~Arg()
{
    if (p_data)
        std::free(p_data);
}

void Kernel::Arg::alloc()
{
    if (!p_data)
        p_data = std::malloc(p_vec_dim * valueSize());
}

void Kernel::Arg::loadData(const void *data)
{
    std::memcpy(p_data, data, p_vec_dim * valueSize());
    p_defined = true;
}

void Kernel::Arg::setAllocAtKernelRuntime(size_t size)
{
    p_runtime_alloc = size;
    p_defined = true;
}

bool Kernel::Arg::operator!=(const Arg &b)
{
    bool same = (p_vec_dim == b.p_vec_dim) &&
                (p_file == b.p_file) &&
                (p_kind == b.p_kind);

    return !same;
}

size_t Kernel::Arg::valueSize() const
{
    switch (p_kind)
    {
        case Invalid:
            return 0;
        case Int8:
            return 1;
        case Int16:
            return 2;
        case Int32:
            return 4;
        case Int64:
            return 8;
        case Float:
            return sizeof(cl_float);
        case Double:
            return sizeof(double);
        case Buffer:
        case Image2D:
        case Image3D:
            return sizeof(cl_mem);
    }
}

unsigned short Kernel::Arg::vecDim() const
{
    return p_vec_dim;
}

Kernel::Arg::File Kernel::Arg::file() const
{
    return p_file;
}

Kernel::Arg::Kind Kernel::Arg::kind() const
{
    return p_kind;
}

bool Kernel::Arg::defined() const
{
    return p_defined;
}

size_t Kernel::Arg::allocAtKernelRuntime() const
{
    return p_runtime_alloc;
}

const void *Kernel::Arg::value(unsigned short index) const
{
    const char *data = (const char *)p_data;
    unsigned int offset = index * valueSize();

    data += offset;

    return (const void *)data;
}
