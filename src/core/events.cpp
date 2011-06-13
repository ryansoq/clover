#include "events.h"
#include "commandqueue.h"
#include "memobject.h"
#include "deviceinterface.h"

using namespace Coal;

/*
 * Read/Write buffers
 */

RWBufferEvent::RWBufferEvent(CommandQueue *parent, 
                                 MemObject *buffer,
                                 size_t offset,
                                 size_t cb,
                                 void *ptr,
                                 EventType type,
                                 cl_uint num_events_in_wait_list, 
                                 const Event **event_wait_list,
                                 cl_int *errcode_ret)
: Event(parent, Queued, num_events_in_wait_list, event_wait_list, errcode_ret),
  p_buffer(buffer), p_offset(offset), p_cb(cb), p_ptr(ptr), p_type(type)
{
    // Correct buffer
    if (!buffer)
    {
        *errcode_ret = CL_INVALID_MEM_OBJECT;
        return;
    }
    
    // Buffer's context must match the CommandQueue one
    Context *ctx = 0;
    *errcode_ret = parent->info(CL_QUEUE_CONTEXT, sizeof(Context *), &ctx, 0);
    
    if (errcode_ret != CL_SUCCESS) return;
    
    if (buffer->context() != ctx)
    {
        *errcode_ret = CL_INVALID_CONTEXT;
        return;
    }
    
    // Check for out-of-bounds reads
    if (!ptr)
    {
        *errcode_ret = CL_INVALID_VALUE;
        return;
    }
    
    if (offset + cb > buffer->size())
    {
        *errcode_ret = CL_INVALID_VALUE;
        return;
    }
    
    // Alignment of SubBuffers
    DeviceInterface *device = 0;
    cl_uint align;
    *errcode_ret = parent->info(CL_QUEUE_DEVICE, sizeof(DeviceInterface *), 
                                &device, 0);
    
    if (errcode_ret != CL_SUCCESS) return;
    
    if (buffer->type() == MemObject::SubBuffer)
    {
        *errcode_ret = device->info(CL_DEVICE_MEM_BASE_ADDR_ALIGN, sizeof(uint),
                                    &align, 0);
        
        if (errcode_ret != CL_SUCCESS) return;
        
        size_t mask = 0;
        
        for (int i=0; i<align; ++i)
            mask = 1 | (mask << 1);
        
        if (((SubBuffer *)buffer)->offset() | mask)
        {
            *errcode_ret = CL_MISALIGNED_SUB_BUFFER_OFFSET;
            return;
        }
    }
    
    // Allocate the buffer for the device
    if (!buffer->allocate(device))
    {
        *errcode_ret = CL_MEM_OBJECT_ALLOCATION_FAILURE;
        return;
    }
}

MemObject *RWBufferEvent::buffer() const
{
    return p_buffer;
}

size_t RWBufferEvent::offset() const
{
    return p_offset;
}

size_t RWBufferEvent::cb() const
{
    return p_cb;
}

void *RWBufferEvent::ptr() const
{
    return p_ptr;
}

Event::EventType RWBufferEvent::type() const
{
    return p_type;
}

/*
 * User event
 */
UserEvent::UserEvent(Context *context, cl_int *errcode_ret)
: Event(0, Submitted, 0, 0, errcode_ret), p_context(context)
{}
        
Event::EventType UserEvent::type() const
{
    return Event::User;
}

Context *UserEvent::context() const
{
    return p_context;
}

void UserEvent::addDependentCommandQueue(CommandQueue *queue)
{
    std::vector<CommandQueue *>::const_iterator it;
    
    for (it = p_dependent_queues.begin(); it != p_dependent_queues.end(); ++it)
        if (*it == queue)
            return;
    
    p_dependent_queues.push_back(queue);
}

void UserEvent::flushQueues()
{
    std::vector<CommandQueue *>::const_iterator it;
    
    for (it = p_dependent_queues.begin(); it != p_dependent_queues.end(); ++it)
        (*it)->pushEventsOnDevice();
}