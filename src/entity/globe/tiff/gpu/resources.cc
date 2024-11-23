#include "resources.h"
#include "app/shader.h"

#include "entity/globe/webgpu_utils.hpp"
#include "shader.hpp"
#include "shader_cast.hpp"

namespace wg {
    namespace tiff {

        GpuResources::GpuResources(AppObjects& ao, const GlobeOptions& opts)
            : ao(ao) {

            freeTileInds.resize(MAX_TILES);
            for (int i = 0; i < MAX_TILES; i++) freeTileInds[i] = i;

            // ------------------------------------------------------------------------------------------------------------------------------------------
            //     Texture & Sampler
            // ------------------------------------------------------------------------------------------------------------------------------------------

            sampler       = ao.device.create(WGPUSamplerDescriptor {
                      .nextInChain  = nullptr,
                      .label        = "TiffRendererDesc",
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
                    .label           = "TiffGlobeTextureArray",
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
                .label       = "TiffRenderer_sharedTexView",
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

            // ------------------------------------------------------------------------------------------------------------------------------------------
            //     BindGroupLayout & BindGroup
            // ------------------------------------------------------------------------------------------------------------------------------------------

            WGPUBindGroupLayoutEntry sharedTexLayoutEntries[2] = {
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
            };
            sharedBindGroupLayout              = ao.device.create(WGPUBindGroupLayoutDescriptor {
                             .nextInChain = nullptr, .label = "TiffRendererSharedBGL", .entryCount = 2, .entries = sharedTexLayoutEntries });

            WGPUBindGroupEntry groupEntries[2] = {
                { .nextInChain = nullptr,
                 .binding     = 0,
                 .buffer      = 0,
                 .offset      = 0,
                 .size        = 0,
                 .sampler     = nullptr,
                 .textureView = sharedTexView                                                                                    },
                { .nextInChain = nullptr, .binding = 1, .buffer = 0, .offset = 0, .size = 0, .sampler = sampler, .textureView = 0 },
            };
            sharedBindGroup = ao.device.create(WGPUBindGroupDescriptor { .nextInChain = nullptr,
                                                                         .label       = "TiffRendererSharedBG",
                                                                         .layout      = sharedBindGroupLayout,
                                                                         .entryCount  = 2,
                                                                         .entries     = groupEntries });

            // ------------------------------------------------------------------------------------------------------------------------------------------
            //     Shader
            // ------------------------------------------------------------------------------------------------------------------------------------------

            ShaderModule shader { create_shader(ao.device, shaderSource, "tiffRendererShader") };

            // ------------------------------------------------------------------------------------------------------------------------------------------
            //     RenderPipeline & Layout
            // ------------------------------------------------------------------------------------------------------------------------------------------

            WGPUBindGroupLayout bgls[2] = {
                ao.getSceneBindGroupLayout().ptr,
                sharedBindGroupLayout.ptr,
            };
            mainPipelineAndLayout.layout      = ao.device.create(WGPUPipelineLayoutDescriptor {
                     .nextInChain          = nullptr,
                     .label                = "tiffRenderer",
                     .bindGroupLayoutCount = 2,
                     .bindGroupLayouts     = bgls,
            });

            WGPUVertexAttribute attributes[3] = {
                WGPUVertexAttribute {
                                     .format         = WGPUVertexFormat_Float32x3,
                                     .offset         = 0 * sizeof(float),
                                     .shaderLocation = 0,
                                     },
                WGPUVertexAttribute {
                                     .format         = WGPUVertexFormat_Float32x2,
                                     .offset         = 3 * sizeof(float),
                                     .shaderLocation = 1,
                                     },
                WGPUVertexAttribute {
                                     .format         = WGPUVertexFormat_Float32x3,
                                     .offset         = 5 * sizeof(float),
                                     .shaderLocation = 2,
                                     },
            };
            WGPUVertexBufferLayout vbl {
                .arrayStride    = (3 + 2 + 3) * sizeof(float),
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
                                                  .label        = "TiffRenderer",
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

            ShaderModule shader { create_shader(ao.device, shaderSourceCast, "tiffRendererCastShader") };

            // ------------------------------------------------------------------------------------------------------------------------------------------
            //     BindGroupLayout
            // ------------------------------------------------------------------------------------------------------------------------------------------

            WGPUBindGroupLayoutEntry castTexLayoutEntries[3] = {
                {
                 .nextInChain    = nullptr,
                 .binding        = 0,
                 .visibility     = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
                 .buffer         = WGPUBufferBindingLayout { .nextInChain = nullptr, .type = WGPUBufferBindingType_Undefined },
                 .sampler        = { .nextInChain = nullptr, .type = WGPUSamplerBindingType_Undefined },
                 .texture        = { .nextInChain   = nullptr,
                 .sampleType    = WGPUTextureSampleType_Float,
                 .viewDimension = WGPUTextureViewDimension_2D,
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
                 .visibility     = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
                 .buffer         = WGPUBufferBindingLayout { .nextInChain      = nullptr,
                 .type             = WGPUBufferBindingType_Uniform,
                 .hasDynamicOffset = false,
                 .minBindingSize   = 0 },
                 .sampler        = { .nextInChain = nullptr, .type = WGPUSamplerBindingType_Undefined },
                 .texture        = { .nextInChain = nullptr, .sampleType = WGPUTextureSampleType_Undefined },
                 .storageTexture = { .nextInChain = nullptr, .access = WGPUStorageTextureAccess_Undefined },
                 },
            };
            castBindGroupLayout = ao.device.create(WGPUBindGroupLayoutDescriptor {
                .nextInChain = nullptr, .label = "TiffRendererCastBGL", .entryCount = 3, .entries = castTexLayoutEntries });

            // ------------------------------------------------------------------------------------------------------------------------------------------
            //     RenderPipeline & Layout
            // ------------------------------------------------------------------------------------------------------------------------------------------

            WGPUBindGroupLayout bgls[3] = {
                ao.getSceneBindGroupLayout().ptr,
                sharedBindGroupLayout.ptr,
                castBindGroupLayout.ptr,
            };
            castPipelineAndLayout.layout      = ao.device.create(WGPUPipelineLayoutDescriptor {
                     .nextInChain          = nullptr,
                     .label                = "tiffRendererCast",
                     .bindGroupLayoutCount = 3,
                     .bindGroupLayouts     = bgls,
            });

            WGPUVertexAttribute attributes[3] = {
                WGPUVertexAttribute {
                                     .format         = WGPUVertexFormat_Float32x3,
                                     .offset         = 0 * sizeof(float),
                                     .shaderLocation = 0,
                                     },
                WGPUVertexAttribute {
                                     .format         = WGPUVertexFormat_Float32x2,
                                     .offset         = 3 * sizeof(float),
                                     .shaderLocation = 1,
                                     },
                WGPUVertexAttribute {
                                     .format         = WGPUVertexFormat_Float32x3,
                                     .offset         = 5 * sizeof(float),
                                     .shaderLocation = 2,
                                     },
            };
            WGPUVertexBufferLayout vbl {
                .arrayStride    = (3 + 2 + 3) * sizeof(float),
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
                                                  .label        = "TiffRendererCast",
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

        void GpuResources::updateCastBindGroupAndResources(const CastInfo& castInfo) {

            // If we have valid cast data, update mvp.
            {
                spdlog::get("wg")->info("uploading castMvp to castMvpBuf");
                size_t castMvpBufSize_raw = 4 * 4 * sizeof(float);
                // assert(castMvp != 0); // we need the MVP matrix at this point.

                if (castMvpBufSize == 0) {
                    castMvpBufSize = roundUp<256>(castMvpBufSize_raw);
                    spdlog::get("wg")->info("creating castMvpBuf of size {}", castMvpBufSize);
                    WGPUBufferDescriptor desc {
                        .nextInChain      = nullptr,
                        .label            = "CastMvpBuffer",
                        .usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform,
                        .size             = castMvpBufSize,
                        .mappedAtCreation = false,
                    };
                    castMvpBuf = ao.device.create(desc);
                }

                ao.queue.writeBuffer(castMvpBuf, 0, &castInfo.castData, castMvpBufSize_raw);
            }

            // Now upload the texture if the new `castInfo.img` is valid.
            // If the texture has not been created yet, or if it has changed size, we must (re-) create it.
            // If we (re-) create the texture, we must (re-) create the BindGroup as well.

            if (castInfo.img.empty()) {
                spdlog::get("wg")->info("skip empty cast tex");
                return;
            }

            uint32_t texw = castInfo.img.cols;
            uint32_t texh = castInfo.img.rows;
            auto texFmt   = WGPUTextureFormat_RGBA8Unorm;

            if (castInfo.img.cols == lastCastTexW and castInfo.img.rows == lastCastTexH /*and texFmt == lastCastTexFmt*/) {
                spdlog::get("wg")->info("use cached cast tex {} {} {}", texw, texh, (int)texFmt);
            } else {
                lastCastTexW = castInfo.img.cols;
                lastCastTexH = castInfo.img.rows;
                spdlog::get("wg")->info("(re-)create cached cast tex {} {} {} and bind group", texw, texh, (int)texFmt);

				// ----------------------------------------------------------------------------------------------------------------------
				//     Texture + View

                castTex     = ao.device.create(WGPUTextureDescriptor {
                        .nextInChain     = nullptr,
                        .label           = "TiffGlobeCastTex",
                        .usage           = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding,
                        .dimension       = WGPUTextureDimension_2D,
                        .size            = WGPUExtent3D { texw, texh, 1 },
                        .format          = WGPUTextureFormat_RGBA8Unorm,
                        .mipLevelCount   = 1,
                        .sampleCount     = 1,
                        .viewFormatCount = 0,
                        .viewFormats     = 0
                });

                castTexView = castTex.createView(WGPUTextureViewDescriptor {
                    .nextInChain     = nullptr,
                    .label           = "TiffRenderer_castTexView",
                    .format          = WGPUTextureFormat_RGBA8Unorm,
                    .dimension       = WGPUTextureViewDimension_2D,
                    .baseMipLevel    = 0,
                    .mipLevelCount   = 1,
                    .baseArrayLayer  = 0,
                    .arrayLayerCount = 1,
                    .aspect          = WGPUTextureAspect_All,
                });


				// ----------------------------------------------------------------------------------------------------------------------
				//     BindGroup

                WGPUBindGroupEntry groupEntries[3] = {
                    { .nextInChain = nullptr,
                     .binding     = 0,
                     .buffer      = 0,
                     .offset      = 0,
                     .size        = 0,
                     .sampler     = nullptr,
                     .textureView = castTexView                                                                                        },
                    { .nextInChain = nullptr, .binding = 1, .buffer = 0, .offset = 0,   .size = 0, .sampler = sampler, .textureView = 0 },
                    { .nextInChain = nullptr,
                     .binding     = 2,
                     .buffer      = castMvpBuf,
                     .offset      = 0,
                     .size        = castMvpBufSize,
                     .sampler     = 0,
                     .textureView = 0                                                                                                  },
                };
                castBindGroup = ao.device.create(WGPUBindGroupDescriptor { .nextInChain = nullptr,
                                                                             .label       = "TiffRendererCastBG",
                                                                             .layout      = castBindGroupLayout,
                                                                             .entryCount  = 3,
                                                                             .entries     = groupEntries });
            }

            // Upload tex.
            // ...

            uploadTex_(castTex, ao, 0, castInfo.img.data(), castInfo.img.total() * castInfo.img.elemSize(), texw, texh,
                       castInfo.img.channels());
        }

    }
}
