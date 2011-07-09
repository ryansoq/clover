#ifndef __KERNEL_H__
#define __KERNEL_H__

#include <CL/cl.h>

#include <vector>

namespace llvm
{
    class Function;
    class Module;
}

namespace Coal
{

class Program;
class DeviceInterface;

class Kernel
{
    public:
        Kernel(Program *program);
        ~Kernel();

        void reference();
        bool dereference();

        cl_int addFunction(DeviceInterface *device, llvm::Function *function,
                           llvm::Module *module);
        llvm::Function *function(DeviceInterface *device) const;

        Program *program() const;

        struct Arg
        {
            unsigned short vec_dim;

            enum Kind
            {
                Invalid,
                Int8,
                Int16,
                Int32,
                Int64,
                Float,
                Double,
                Buffer,
                Image2D,
                Image3D
                // TODO: Sampler
            } kind;
            union
            {
                #define TYPE_VAL(type) type type##_val
                TYPE_VAL(uint8_t);
                TYPE_VAL(uint16_t);
                TYPE_VAL(uint32_t);
                TYPE_VAL(uint64_t);
                TYPE_VAL(cl_float);
                TYPE_VAL(double);
                TYPE_VAL(cl_mem);
                #undef TYPE_VAL
            };

            inline bool operator !=(const Arg &b)
            {
                return (kind != b.kind) || (vec_dim != b.vec_dim);
            }
        };

    private:
        Program *p_program;
        unsigned int p_references;

        struct DeviceDependent
        {
            DeviceInterface *device;
            llvm::Function *function;
            llvm::Module *module;
        };

        std::vector<DeviceDependent> p_device_dependent;
        std::vector<Arg> p_args;
        const DeviceDependent &deviceDependent(DeviceInterface *device) const;
        DeviceDependent &deviceDependent(DeviceInterface *device);
};

}

class _cl_kernel : public Coal::Kernel
{};

#endif
