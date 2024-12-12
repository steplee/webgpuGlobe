#pragma once

#include "app/app.h"
#include <spdlog/spdlog.h>

namespace wg {
namespace {

    // WebGPU Upload Helpers.
    inline void createVbo_(Buffer& vbo, AppObjects& ao, const uint8_t* ptr, size_t bufSize) {
        WGPUBufferDescriptor desc {
            .nextInChain      = nullptr,
            .label            = "GlobeVbo",
            .usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex,
            .size             = bufSize,
            .mappedAtCreation = true,
        };
        vbo       = ao.device.create(desc);

        void* dst = wgpuBufferGetMappedRange(vbo, 0, bufSize);
        memcpy(dst, ptr, bufSize);

        wgpuBufferUnmap(vbo);
    }
    inline void createVbo_(Buffer& vbo, AppObjects& ao, const std::vector<uint8_t>& vec) {
        createVbo_(vbo, ao, vec.data(), vec.size());
    }
    inline void createIbo_(Buffer& ibo, AppObjects& ao, const uint8_t* ptr, size_t bufSize) {
        WGPUBufferDescriptor desc {
            .nextInChain      = nullptr,
            .label            = "GlobeIbo",
            .usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index,
            .size             = bufSize,
            .mappedAtCreation = true,
        };
        ibo       = ao.device.create(desc);

        void* dst = wgpuBufferGetMappedRange(ibo, 0, bufSize);
        memcpy(dst, ptr, bufSize);

        wgpuBufferUnmap(ibo);
    }
    inline void uploadTex_(Texture& sharedTex, AppObjects& ao, uint32_t textureArrayIndex, const uint8_t* ptr, size_t bufSize, uint32_t w,
                    uint32_t h, uint32_t c) {
        // const WGPUTextureDataLayout& dataLayout, const WGPUExtent3D& writeSize) {

        // spdlog::get("tiffRndr")->trace("upload tex index {} shape {} {} {} bufSize {}", textureArrayIndex, w,h,c, bufSize);
        ao.queue.writeTexture(
            WGPUImageCopyTexture {
                .nextInChain = nullptr,
                .texture     = sharedTex,
                .mipLevel    = 0,
                .origin      = WGPUOrigin3D { 0, 0, textureArrayIndex },
				.aspect = WGPUTextureAspect_All,
        },
            ptr, bufSize,
            WGPUTextureDataLayout {
                .nextInChain  = nullptr,
                .offset       = 0,
                .bytesPerRow  = w * c,
                .rowsPerImage = h,
            },
            WGPUExtent3D { w, h, 1 });
    }

}
}
