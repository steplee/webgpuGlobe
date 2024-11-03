
#include "wrappers.hpp"
#include "app.h"

#include <spdlog/spdlog.h>
#include <unistd.h>

namespace wg {

    Instance Instance::create(const WGPUInstanceDescriptor& desc) {
        return { wgpuCreateInstance(&desc) };
    }

    Adapter Instance::requestAdapter(const WGPURequestAdapterOptions& options) {
        WGPUAdapter adapter        = nullptr;
        volatile bool requestEnded = false;

        auto onAdapterRequestEnded
            = [&adapter, &requestEnded](WGPURequestAdapterStatus status, WGPUAdapter _adapter, char const* message) {
                  if (status == WGPURequestAdapterStatus_Success) {
                      adapter = _adapter;
                  } else {
                      spdlog::get("wg")->critical("Could not get WebGPU adapter: {}", message);
                  }
                  requestEnded = true;
              };

        {

            // C-style callbacks (func pointer + userdata*) are not compatible with C++ lambdas, but easily made so.
            auto cb = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, char const* message, WGPU_NULLABLE void* userdata) {
                auto after = reinterpret_cast<decltype(onAdapterRequestEnded)*>(userdata);
                (*after)(static_cast<WGPURequestAdapterStatus>(status), adapter, message);
            };
            wgpuInstanceRequestAdapter(ptr, &options, cb, reinterpret_cast<void*>(&onAdapterRequestEnded));

#if __EMSCRIPTEN__
            while (!requestEnded) { emscripten_sleep(100); }
#else
            while (!requestEnded) { usleep(100); }
#endif
        }

        assert(requestEnded);
        return { adapter };
    }

    /*
    std::unique_ptr<WGPUAdapterRequestDeviceCallback> Adapter::requestDevice(WGPUDeviceDescriptor& desc,
    WGPUAdapterRequestDeviceCallback&& callback) { auto handle = std::make_unique<WGPUAdapterRequestDeviceCallback>(callback);

            static auto cCallback = [](WGPURequestDeviceStatus status, WGPUDevice device, char const * message, WGPU_NULLABLE void *
    userdata) -> void { WGPUAdapterRequestDeviceCallback& callback = *reinterpret_cast<WGPUAdapterRequestDeviceCallback*>(userdata);
                    callback(static_cast<WGPURequestDeviceStatus>(status), device, message, userdata);
            };
            wgpuAdapterRequestDevice(ptr, &desc, cCallback, reinterpret_cast<void*>(handle.get()));

            return handle;
    }
    */

    Device Adapter::requestDevice(const WGPUDeviceDescriptor& desc) {
        WGPUDevice device         = nullptr;
        bool requestEnded         = false;

        auto onDeviceRequestEnded = [&device, &requestEnded](WGPURequestDeviceStatus status, WGPUDevice _device, char const* message) {
            if (status == WGPURequestDeviceStatus_Success) {
                device = _device;
            } else {
                spdlog::get("wg")->critical("Could not get WebGPU device: {}", message);
            }
            requestEnded = true;
        };

        {
            // auto h = requestDevice(desc, std::move(onDeviceRequestEnded));

            // C-style callbacks (func pointer + userdata*) are not compatible with C++ lambdas, but easily made so.
            auto cb = [](WGPURequestDeviceStatus status, WGPUDevice device, char const* message, WGPU_NULLABLE void* userdata) {
                auto after = reinterpret_cast<decltype(onDeviceRequestEnded)*>(userdata);
                (*after)(static_cast<WGPURequestDeviceStatus>(status), device, message);
            };
            wgpuAdapterRequestDevice(ptr, &desc, cb, reinterpret_cast<void*>(&onDeviceRequestEnded));

#if __EMSCRIPTEN__
            while (!requestEnded) { emscripten_sleep(100); }
#endif
        }

        assert(requestEnded);
        return { device };
    }

    RenderPassEncoder CommandEncoder::beginRenderPassForSurface(const AppObjects& ao, FrameData& frameData) {

        WGPURenderPassColorAttachment colorAttach {
            .nextInChain   = nullptr,
            .view          = frameData.surfaceTexView,
            .depthSlice    = ~0u,
            .resolveTarget = nullptr,
            .loadOp        = WGPULoadOp_Clear,
            .storeOp       = WGPUStoreOp_Store,
            .clearValue    = WGPUColor { .0f, .0f, .0f, .99f }
        };

        WGPURenderPassDepthStencilAttachment depthStencilAttach { .view              = frameData.surfaceDepthStencilView,
                                                                  .depthLoadOp       = WGPULoadOp_Clear,
                                                                  .depthStoreOp      = WGPUStoreOp_Store,
                                                                  .depthClearValue   = 1.0f,
                                                                  .depthReadOnly     = false,
                                                                  .stencilLoadOp     = WGPULoadOp_Clear,
                                                                  .stencilStoreOp    = WGPUStoreOp_Store,
                                                                  .stencilClearValue = 0,
                                                                  .stencilReadOnly   = true };

        WGPURenderPassDescriptor desc {
            .nextInChain            = nullptr,
            .label                  = "passForSurface",
            .colorAttachmentCount   = 1,
            .colorAttachments       = &colorAttach,
            .depthStencilAttachment = frameData.surfaceDepthStencilView == nullptr ? nullptr : &depthStencilAttach
            // .occlusionQuerySet = nullptr,
            // .timestampWrites = nullptr,
        };
        return beginRenderPass(desc);
    }

    WGPURequiredLimits defaultRequiredLimits() {

        WGPURequiredLimits requiredLimits;
        requiredLimits.nextInChain                                      = nullptr;
        requiredLimits.limits.maxTextureDimension1D                     = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxTextureDimension2D                     = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxTextureDimension3D                     = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxTextureArrayLayers                     = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxBindGroups                             = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxBindGroupsPlusVertexBuffers            = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxBindingsPerBindGroup                   = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxDynamicUniformBuffersPerPipelineLayout = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxDynamicStorageBuffersPerPipelineLayout = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxSampledTexturesPerShaderStage          = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxSamplersPerShaderStage                 = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxStorageBuffersPerShaderStage           = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxStorageTexturesPerShaderStage          = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxUniformBuffersPerShaderStage           = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxUniformBufferBindingSize               = WGPU_LIMIT_U64_UNDEFINED;
        requiredLimits.limits.maxStorageBufferBindingSize               = WGPU_LIMIT_U64_UNDEFINED;
        requiredLimits.limits.minUniformBufferOffsetAlignment           = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.minStorageBufferOffsetAlignment           = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxVertexBuffers                          = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxBufferSize                             = WGPU_LIMIT_U64_UNDEFINED;
        requiredLimits.limits.maxVertexAttributes                       = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxVertexBufferArrayStride                = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxInterStageShaderComponents             = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxInterStageShaderVariables              = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxColorAttachments                       = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxColorAttachmentBytesPerSample          = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxComputeWorkgroupStorageSize            = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxComputeInvocationsPerWorkgroup         = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxComputeWorkgroupSizeX                  = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxComputeWorkgroupSizeY                  = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxComputeWorkgroupSizeZ                  = WGPU_LIMIT_U32_UNDEFINED;
        requiredLimits.limits.maxComputeWorkgroupsPerDimension          = WGPU_LIMIT_U32_UNDEFINED;
        return requiredLimits;
    }

    WGPUStencilFaceState WGPUStencilFaceState_Default() {
        return WGPUStencilFaceState {
            WGPUCompareFunction_Less,
            WGPUStencilOperation_Keep,
            WGPUStencilOperation_Keep,
            WGPUStencilOperation_Keep,
        };
    }

	WGPUPrimitiveState WGPUPrimitiveState_Default() {
            return WGPUPrimitiveState {
                .nextInChain      = nullptr,
                .topology         = WGPUPrimitiveTopology_TriangleList,
                .stripIndexFormat = WGPUIndexFormat_Undefined,
                // .frontFace        = WGPUFrontFace_CCW,
                .frontFace        = WGPUFrontFace_CW,
                .cullMode         = WGPUCullMode_Back,
            };
	}
	WGPUBlendState WGPUBlendState_Default() {
		return WGPUBlendState {
				.color = WGPUBlendComponent {
					.operation = WGPUBlendOperation_Add,
					.srcFactor = WGPUBlendFactor_SrcAlpha,
					.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha,
				},
				.alpha = WGPUBlendComponent {
					.operation = WGPUBlendOperation_Add,
					.srcFactor = WGPUBlendFactor_Zero,
					.dstFactor = WGPUBlendFactor_One,
				}
			};
	}
	WGPUColorTargetState WGPUColorTargetState_Default(const AppObjects& ao, WGPUBlendState& blend) {
            return WGPUColorTargetState {
                .nextInChain = nullptr,
                .format      = ao.surfaceColorFormat,
                .blend       = &blend,
                .writeMask   = WGPUColorWriteMask_All,
            };
	}
	WGPUFragmentState WGPUFragmentState_Default(ShaderModule& shader, WGPUColorTargetState& cts, const char *entry) {
            return WGPUFragmentState { .nextInChain   = nullptr,
                                              .module        = shader,
                                              .entryPoint    = entry,
                                              .constantCount = 0,
                                              .constants     = nullptr,
                                              .targetCount   = 1,
                                              .targets       = &cts };
	}

	WGPUVertexState WGPUVertexState_Default(ShaderModule& shader, WGPUVertexBufferLayout& vbl, const char *entry) {
            return WGPUVertexState { .nextInChain   = nullptr,
                                          .module        = shader,
                                          .entryPoint    = "vs_main",
                                          .constantCount = 0,
                                          .constants     = nullptr,
                                          .bufferCount   = 1,
                                          .buffers       = &vbl };
	}

	WGPUMultisampleState WGPUMultisampleState_Default() {
            return WGPUMultisampleState { .nextInChain = nullptr, .count = 1, .mask = ~0u, .alphaToCoverageEnabled = false };
	}

    WGPUDepthStencilState WGPUDepthStencilState_Default(AppObjects& ao) {
            return WGPUDepthStencilState {
                .nextInChain         = nullptr,
                .format              = ao.surfaceDepthStencilFormat,
                .depthWriteEnabled   = true,
                .depthCompare        = WGPUCompareFunction_Less,
                .stencilFront        = WGPUStencilFaceState_Default(),
                .stencilBack         = WGPUStencilFaceState_Default(),
                .stencilReadMask     = 0,
                .stencilWriteMask    = 0,
                .depthBias           = 0,
                .depthBiasSlopeScale = 0.f,
                .depthBiasClamp      = 0.f,
            };
	}

}
