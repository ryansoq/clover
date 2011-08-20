/*
 * Copyright (c) 2011, Denis Steckelmacher <steckdenis@yahoo.fr>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the copyright holder nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file cpu/sampler.cpp
 * \brief OpenCL C image access functions
 *
 * It is recommended to compile this file using Clang as it supports the
 * \c __builtin_shufflevector() built-in function, providing SSE or
 * NEON-accelerated code.
 */

#include "../memobject.h"
#include "kernel.h"
#include "buffer.h"
#include "builtins.h"

#include <cstdlib>
#include <immintrin.h>

using namespace Coal;

/*
 * Macros or functions used to accelerate the functions
 */
#ifndef __has_builtin
    #define __has_builtin(x) 0
#endif

static void slow_shuffle4(uint32_t *rs, uint32_t *a, uint32_t *b,
                          int x, int y, int z, int w)
{
    rs[0] = (x < 4 ? a[x] : b[x - 4]);
    rs[1] = (y < 4 ? a[y] : b[y - 4]);
    rs[2] = (z < 4 ? a[z] : b[z - 4]);
    rs[3] = (w < 4 ? a[w] : b[w - 4]);
}

static void slow_convert_to_format4f(float *data, cl_channel_type type)
{
    // Convert always the four components of source to target
    if (type == CL_FLOAT)
        return;

    // NOTE: We can read and write at the same time in data because
    //       we always begin wy reading 4 bytes (float) and never write
    //       more than 4 bytes, so no data is corrupted
    for (unsigned int i=0; i<3; ++i)
    {
        switch (type)
        {
            case CL_SNORM_INT8:
                ((int8_t *)data)[i] = data[i] * 128.0f;
                break;
            case CL_SNORM_INT16:
                ((int16_t *)data)[i] = data[i] * 32767.0f;
                break;
            case CL_UNORM_INT8:
                ((uint8_t *)data)[i] = data[i] * 256.0f;
                break;
            case CL_UNORM_INT16:
                ((uint16_t *)data)[i] = data[i] * 65535.0f;
                break;
        }
    }
}

static void slow_convert_to_format4i(int *data, cl_channel_type type)
{
    // Convert always the four components of source to target
    if (type == CL_SIGNED_INT32)
        return;

    for (unsigned int i=0; i<3; ++i)
    {
        switch (type)
        {
            case CL_SIGNED_INT8:
                ((int8_t *)data)[i] = data[i];
                break;
            case CL_SIGNED_INT16:
                ((int16_t *)data)[i] = data[i];
                break;
        }
    }
}

static void slow_convert_to_format4ui(uint32_t *data, cl_channel_type type)
{
    // Convert always the four components of source to target
    if (type == CL_UNSIGNED_INT32)
        return;

    for (unsigned int i=0; i<3; ++i)
    {
        switch (type)
        {
            case CL_UNSIGNED_INT8:
                ((uint8_t *)data)[i] = data[i];
                break;
            case CL_UNSIGNED_INT16:
                ((uint16_t *)data)[i] = data[i];
                break;
        }
    }
}

#if __has_builtin(__builtin_shufflevector)
    #define shuffle4(rs, a, b, x, y, z, w) \
        *(__v4sf *)rs = __builtin_shufflevector(*(__v4sf *)a, *(__v4sf *)b, \
                                                x, y, z, w)
#else
    #define shuffle4(rs, a, b, x, y, z, w) \
        slow_shuffle4(rs, a, b, x, y, z, w)
#endif

    #define convert_to_format4f(data, type) \
        slow_convert_to_format4f(data, type)

    #define convert_to_format4i(data, type) \
        slow_convert_to_format4i(data, type)

    #define convert_to_format4ui(data, type) \
        slow_convert_to_format4ui(data, type)

static void swizzle(uint32_t *target, uint32_t *source,
                    cl_channel_order order, bool reading, uint32_t t_max)
{
    uint32_t special[4] = {0, t_max, 0, 0 };

    if (reading)
    {
        switch (order)
        {
            case CL_R:
            case CL_Rx:
                // target = {source->x, 0, 0, t_max}
                shuffle4(target, source, special, 0, 4, 4, 5);
                break;
            case CL_A:
                // target = {0, 0, 0, source->x}
                shuffle4(target, source, special, 4, 4, 4, 0);
                break;
            case CL_INTENSITY:
                // target = {source->x, source->x, source->x, source->x}
                shuffle4(target, source, source, 0, 0, 0, 0);
                break;
            case CL_LUMINANCE:
                // target = {source->x, source->x, source->x, t_max}
                shuffle4(target, source, special, 0, 0, 0, 5);
                break;
            case CL_RG:
            case CL_RGx:
                // target = {source->x, source->y, 0, t_max}
                shuffle4(target, source, special, 0, 1, 4, 5);
                break;
            case CL_RA:
                // target = {source->x, 0, 0, source->y}
                shuffle4(target, source, special, 0, 4, 4, 1);
                break;
            case CL_RGB:
            case CL_RGBx:
            case CL_RGBA:
                // Nothing to do, already the good order
                std::memcpy(target, source, 16);
                break;
            case CL_ARGB:
                // target = {source->y, source->z, source->w, source->x}
                shuffle4(target, source, source, 1, 2, 3, 0);
                break;
            case CL_BGRA:
                // target = {source->z, source->y, source->x, source->w}
                shuffle4(target, source, source, 2, 1, 0, 3);
                break;
        }
    }
    else
    {
        switch (order)
        {
            case CL_A:
                // target = {source->w, undef, undef, undef}
                shuffle4(target, source, source, 3, 3, 3, 3);
                break;
            case CL_RA:
                // target = {source->x, source->w, undef, undef}
                shuffle4(target, source, source, 0, 3, 3, 3);
                break;
            case CL_ARGB:
                // target = {source->w, source->x, source->y, source->z}
                shuffle4(target, source, source, 3, 0, 1, 2);
                break;
            case CL_BGRA:
                // target = {source->z, source->y, source->x, source->w}
                shuffle4(target, source, source, 2, 1, 0, 3);
                break;
            default:
                std::memcpy(target, source, 16);
        }
    }
}

/*
 * Actual implementation of the built-ins
 */

void *CPUKernelWorkGroup::getImageData(Image2D *image, int x, int y, int z) const
{
    CPUBuffer *buffer =
        (CPUBuffer *)image->deviceBuffer((DeviceInterface *)p_kernel->device());

    return imageData((unsigned char *)buffer->data(),
                     x, y, z,
                     image->row_pitch(),
                     image->slice_pitch(),
                     image->pixel_size());
}

void CPUKernelWorkGroup::writeImage(Image2D *image, int x, int y, int z,
                                    float *color) const
{
    float converted[4];

    // Swizzle to the correct order (float, int and uint are 32-bit, so the
    // type has no importance
    swizzle((uint32_t *)converted, (uint32_t *)color,
            image->format().image_channel_order, false, 0);

    // Convert color to the correct format
    convert_to_format4f(converted, image->format().image_channel_data_type);

    // Get a pointer in the image where to write the data
    void *target = getImageData(image, x, y, z);

    // Copy the converted data to the image
    std::memcpy(target, converted, image->pixel_size());
}

void CPUKernelWorkGroup::writeImage(Image2D *image, int x, int y, int z,
                                    int32_t *color) const
{
    int32_t converted[4];

    // Swizzle to the correct order (float, int and uint are 32-bit, so the
    // type has no importance
    swizzle((uint32_t *)converted, (uint32_t *)color,
            image->format().image_channel_order, false, 0);

    // Convert color to the correct format
    convert_to_format4i(converted, image->format().image_channel_data_type);

    // Get a pointer in the image where to write the data
    void *target = getImageData(image, x, y, z);

    // Copy the converted data to the image
    std::memcpy(target, converted, image->pixel_size());
}

void CPUKernelWorkGroup::writeImage(Image2D *image, int x, int y, int z,
                                    uint32_t *color) const
{
    uint32_t converted[4];

    // Swizzle to the correct order (float, int and uint are 32-bit, so the
    // type has no importance
    swizzle((uint32_t *)converted, (uint32_t *)color,
            image->format().image_channel_order, false, 0);

    // Convert color to the correct format
    convert_to_format4ui(converted, image->format().image_channel_data_type);

    // Get a pointer in the image where to write the data
    void *target = getImageData(image, x, y, z);

    // Copy the converted data to the image
    std::memcpy(target, converted, image->pixel_size());
}