#include "resources.h"
#include "app/shader.h"

#include "entity/globe/webgpu_utils.hpp"
#include "shader.hpp"
#include "shader_cast.hpp"

#include <wgpu/wgpu.h>
#include <unistd.h>

namespace wg {
    namespace gearth {

        GpuResources::GpuResources(AppObjects& ao, const GlobeOptions& opts)
            : ao(ao) {

            freeTileInds.resize(MAX_TILES);
            for (int i = 0; i < MAX_TILES; i++) freeTileInds[i] = i;

            // ------------------------------------------------------------------------------------------------------------------------------------------
            //     Texture & Sampler
            // ------------------------------------------------------------------------------------------------------------------------------------------

            sampler       = ao.device.create(WGPUSamplerDescriptor {
                      .nextInChain  = nullptr,
                      .label        = "GearthRendererDesc",
                      .addressModeU = WGPUAddressMode_ClampToEdge,
                      .addressModeV = WGPUAddressMode_ClampToEdge,
                      .addressModeW = WGPUAddressMode_ClampToEdge,
                      .magFilter    = WGPUFilterMode_Linear,
                      .minFilter    = WGPUFilterMode_Linear,
                      .mipmapFilter = WGPUMipmapFilterMode_Nearest,
                      .lodMinClamp  = 0,
                      .lodMaxClamp  = 32,
                // .compare       = WGPUCompareFunction_Less,
                      .compare       = WGPUCompareFunction_Undefined,
                      .maxAnisotropy = 1,
            });

            sharedTex     = ao.device.create(WGPUTextureDescriptor {
                    .nextInChain     = nullptr,
                    .label           = "GearthGlobeTextureArray",
                    .usage           = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding,
                    .dimension       = WGPUTextureDimension_2D,
                    .size            = WGPUExtent3D { 256, 256, MAX_TILES },
                    .format          = WGPUTextureFormat_RGBA8Unorm,
                    .mipLevelCount   = 1,
                    .sampleCount     = 1,
                    .viewFormatCount = 0,
                    .viewFormats     = 0
            });

            sharedTexView = sharedTex.createView(WGPUTextureViewDescriptor {
                .nextInChain = nullptr,
                .label       = "GearthRenderer_sharedTexView",
                .format      = WGPUTextureFormat_RGBA8Unorm,
                // .dimension       = WGPUTextureViewDimension_2D,
                .dimension       = WGPUTextureViewDimension_2DArray,
                .baseMipLevel    = 0,
                .mipLevelCount   = 1,
                .baseArrayLayer  = 0,
                .arrayLayerCount = MAX_TILES,
                .aspect          = WGPUTextureAspect_All,
            });

            createMainPipeline();
            createCastPipeline();
        }

        void GpuResources::createMainPipeline() {

				extraTileDataBufSize = roundUp<256>(sizeof(ExtraTileData));
				spdlog::get("wg")->info("creating deferredCastMvpBuf of size {}", extraTileDataBufSize);
				WGPUBufferDescriptor desc {
					.nextInChain      = nullptr,
						.label            = "ExtraTileDataBuf_Gearth",
						.usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform,
						.size             = (uint32_t)extraTileDataBufSize,
						.mappedAtCreation = false,
				};
				extraTileDataBuf = ao.device.create(desc);

            // ------------------------------------------------------------------------------------------------------------------------------------------
            //     BindGroupLayout & BindGroup
            // ------------------------------------------------------------------------------------------------------------------------------------------

            WGPUBindGroupLayoutEntry sharedTexLayoutEntries[3] = {
                {
                 .nextInChain    = nullptr,
                 .binding        = 0,
                 .visibility     = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
                 .buffer         = WGPUBufferBindingLayout { .nextInChain = nullptr, .type = WGPUBufferBindingType_Undefined },
                 .sampler        = { .nextInChain = nullptr, .type = WGPUSamplerBindingType_Undefined },
                 .texture        = { .nextInChain   = nullptr,
                 .sampleType    = WGPUTextureSampleType_Float,
                 .viewDimension = WGPUTextureViewDimension_2DArray,
                 .multisampled  = false },
                 .storageTexture = { .nextInChain = nullptr, .access = WGPUStorageTextureAccess_Undefined },
                 },
                {
                 .nextInChain    = nullptr,
                 .binding        = 1,
                 .visibility     = WGPUShaderStage_Fragment,
                 .buffer         = WGPUBufferBindingLayout { .nextInChain = nullptr, .type = WGPUBufferBindingType_Undefined },
                 .sampler        = { .nextInChain = nullptr, .type = WGPUSamplerBindingType_Filtering },
                 .texture        = { .nextInChain = nullptr, .sampleType = WGPUTextureSampleType_Undefined },
                 .storageTexture = { .nextInChain = nullptr, .access = WGPUStorageTextureAccess_Undefined },
                 },
                {
                 .nextInChain    = nullptr,
                 .binding        = 2,
                 .visibility     = WGPUShaderStage_Vertex,
                 .buffer         = WGPUBufferBindingLayout { .nextInChain      = nullptr,
					.type             = WGPUBufferBindingType_Uniform,
					.hasDynamicOffset = false,
					.minBindingSize   = 0 },
                 .sampler        = { .nextInChain = nullptr, .type = WGPUSamplerBindingType_Undefined },
                 .texture        = { .nextInChain = nullptr, .sampleType = WGPUTextureSampleType_Undefined },
                 .storageTexture = { .nextInChain = nullptr, .access = WGPUStorageTextureAccess_Undefined },
                 },
            };
            sharedBindGroupLayout              = ao.device.create(WGPUBindGroupLayoutDescriptor {
                             .nextInChain = nullptr, .label = "GearthRendererSharedBGL", .entryCount = 3, .entries = sharedTexLayoutEntries });

            WGPUBindGroupEntry groupEntries[3] = {
                { .nextInChain = nullptr,
                 .binding     = 0,
                 .buffer      = 0,
                 .offset      = 0,
                 .size        = 0,
                 .sampler     = nullptr,
                 .textureView = sharedTexView                                                                                    },
                { .nextInChain = nullptr, .binding = 1, .buffer = 0, .offset = 0, .size = 0, .sampler = sampler, .textureView = 0 },
                { .nextInChain = nullptr,
                 .binding     = 2,
                 .buffer      = extraTileDataBuf.ptr,
                 .offset      = 0,
                 .size        = (uint64_t)extraTileDataBufSize,
                 .sampler     = nullptr,
                 .textureView = nullptr                                                                                    },
            };
            sharedBindGroup = ao.device.create(WGPUBindGroupDescriptor { .nextInChain = nullptr,
                                                                         .label       = "GearthRendererSharedBG",
                                                                         .layout      = sharedBindGroupLayout,
                                                                         .entryCount  = 3,
                                                                         .entries     = groupEntries });

            // ------------------------------------------------------------------------------------------------------------------------------------------
            //     Shader
            // ------------------------------------------------------------------------------------------------------------------------------------------

            ShaderModule shader { create_shader(ao.device, shaderSource, "gearthRendererShader") };

            // ------------------------------------------------------------------------------------------------------------------------------------------
            //     RenderPipeline & Layout
            // ------------------------------------------------------------------------------------------------------------------------------------------

            WGPUBindGroupLayout bgls[2] = {
                ao.getSceneBindGroupLayout().ptr,
                sharedBindGroupLayout.ptr,
            };
            mainPipelineAndLayout.layout      = ao.device.create(WGPUPipelineLayoutDescriptor {
                     .nextInChain          = nullptr,
                     .label                = "gearthRenderer",
                     .bindGroupLayoutCount = 2,
                     .bindGroupLayouts     = bgls,
            });

            WGPUVertexAttribute attributes[3] = {
                WGPUVertexAttribute {
                                     .format         = WGPUVertexFormat_Uint8x4,
                                     .offset         = 0 * sizeof(uint8_t),
                                     .shaderLocation = 0,
                                     },
                WGPUVertexAttribute {
                                     .format         = WGPUVertexFormat_Uint16x2,
                                     .offset         = 4 * sizeof(uint8_t),
                                     .shaderLocation = 1,
                                     },
                WGPUVertexAttribute {
                                     .format         = WGPUVertexFormat_Uint8x4,
                                     .offset         = 8 * sizeof(uint8_t),
                                     .shaderLocation = 2,
                                     },
            };
            WGPUVertexBufferLayout vbl {
                .arrayStride    = (4 + 4 + 4) * sizeof(uint8_t),
                .stepMode       = WGPUVertexStepMode_Vertex,
                .attributeCount = 3,
                .attributes     = attributes,
            };
            auto vertexState       = WGPUVertexState_Default(shader, vbl);

            auto primState         = WGPUPrimitiveState_Default();
            auto multisampleState  = WGPUMultisampleState_Default();
            auto blend             = WGPUBlendState_Default();
            auto cst               = WGPUColorTargetState_Default(ao, blend);
            auto fragmentState     = WGPUFragmentState_Default(shader, cst);
            auto depthStencilState = WGPUDepthStencilState_Default(ao);

            WGPURenderPipelineDescriptor rpDesc { .nextInChain  = nullptr,
                                                  .label        = "GearthRenderer",
                                                  .layout       = mainPipelineAndLayout.layout,
                                                  .vertex       = vertexState,
                                                  .primitive    = primState,
                                                  .depthStencil = ao.surfaceDepthStencilFormat == WGPUTextureFormat_Undefined
                                                                      ? nullptr
                                                                      : &depthStencilState,
                                                  .multisample  = multisampleState,
                                                  .fragment     = &fragmentState };
            mainPipelineAndLayout.pipeline = ao.device.create(rpDesc);
        }

        void GpuResources::createCastPipeline() {

            // ------------------------------------------------------------------------------------------------------------------------------------------
            //     Shader
            // ------------------------------------------------------------------------------------------------------------------------------------------

            ShaderModule shader { create_shader(ao.device, shaderSourceCast, "gearthRendererCastShader") };

            // ------------------------------------------------------------------------------------------------------------------------------------------
            //     BindGroupLayout
            // ------------------------------------------------------------------------------------------------------------------------------------------

			castGpuResources.create(ao);

            // ------------------------------------------------------------------------------------------------------------------------------------------
            //     RenderPipeline & Layout
            // ------------------------------------------------------------------------------------------------------------------------------------------

            WGPUBindGroupLayout bgls[3] = {
                ao.getSceneBindGroupLayout().ptr,
                sharedBindGroupLayout.ptr,
                // castBindGroupLayout.ptr,
                castGpuResources.bindGroupLayout.ptr,
            };
            castPipelineAndLayout.layout      = ao.device.create(WGPUPipelineLayoutDescriptor {
                     .nextInChain          = nullptr,
                     .label                = "gearthRendererCast",
                     .bindGroupLayoutCount = 3,
                     .bindGroupLayouts     = bgls,
            });

            WGPUVertexAttribute attributes[3] = {
                WGPUVertexAttribute {
                                     .format         = WGPUVertexFormat_Uint8x4,
                                     .offset         = 0 * sizeof(uint8_t),
                                     .shaderLocation = 0,
                                     },
                WGPUVertexAttribute {
                                     .format         = WGPUVertexFormat_Uint16x2,
                                     .offset         = 4 * sizeof(uint8_t),
                                     .shaderLocation = 1,
                                     },
                WGPUVertexAttribute {
                                     .format         = WGPUVertexFormat_Uint8x4,
                                     .offset         = 8 * sizeof(uint8_t),
                                     .shaderLocation = 2,
                                     },
            };
            WGPUVertexBufferLayout vbl {
                .arrayStride    = (4 + 4 + 4) * sizeof(uint8_t),
                .stepMode       = WGPUVertexStepMode_Vertex,
                .attributeCount = 3,
                .attributes     = attributes,
            };
            auto vertexState       = WGPUVertexState_Default(shader, vbl);

            auto primState         = WGPUPrimitiveState_Default();
            auto multisampleState  = WGPUMultisampleState_Default();
            auto blend             = WGPUBlendState_Default();
            auto cst               = WGPUColorTargetState_Default(ao, blend);
            auto fragmentState     = WGPUFragmentState_Default(shader, cst);
            auto depthStencilState = WGPUDepthStencilState_Default(ao);

            WGPURenderPipelineDescriptor rpDesc { .nextInChain  = nullptr,
                                                  .label        = "GearthRendererCast",
                                                  .layout       = castPipelineAndLayout.layout,
                                                  .vertex       = vertexState,
                                                  .primitive    = primState,
                                                  .depthStencil = ao.surfaceDepthStencilFormat == WGPUTextureFormat_Undefined
                                                                      ? nullptr
                                                                      : &depthStencilState,
                                                  .multisample  = multisampleState,
                                                  .fragment     = &fragmentState };
            castPipelineAndLayout.pipeline = ao.device.create(rpDesc);
        }

        void GpuResources::updateCastBindGroupAndResources(const CastUpdate& castUpdate) {
			castGpuResources.updateCastBindGroupAndResources(ao, castUpdate);
			std::atomic<bool> done = false;
			wgpuQueueOnSubmittedWorkDone(ao.queue, [](WGPUQueueWorkDoneStatus status, void *data) {
				// SPDLOG_INFO("LOOPS DONE");
				reinterpret_cast<std::atomic<bool>*>(data)->store(true);
			}, &done);
			int loops = 0;
			while (true) {
				wgpuDevicePoll(ao.device, true, 0);
				loops++;
				// SPDLOG_INFO("LOOPS {}", loops);
				if (done) break;
				usleep(1);
			}
        }

    }
}
