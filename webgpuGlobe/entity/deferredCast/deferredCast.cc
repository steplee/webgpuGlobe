#include "deferredCast.h"
#include "app/shader.h"
#include "shader_noop.hpp"

namespace {
    const char* cast_source = R"(

struct SceneCameraData {
	mvp: mat4x4<f32>,
	imvp: mat4x4<f32>,
	mv: mat4x4<f32>,

	eye: vec3f,
	colorMult: vec4f,

	wh: vec2f,
	sun: vec4f,
	haeAlt: f32,
	haze: f32,
	time: f32,
	dt: f32,
}

struct CastData {
	mvp: mat4x4<f32>,
	color: vec4f,
}

@group(0) @binding(0)
var<uniform> scd: SceneCameraData;

@group(1) @binding(0) var texColor: texture_2d<f32>;
@group(1) @binding(1) var texDepth: texture_depth_2d;
@group(1) @binding(2) var texSampler: sampler;

@group(2) @binding(0) var castTex: texture_2d<f32>;
@group(2) @binding(1) var<uniform> castData: CastData;

struct VertexInput {
    @builtin(vertex_index) vertex_index: u32
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(vi: VertexInput) -> VertexOutput {

	let vertex_index = vi.vertex_index;
	var ps = array<vec4f, 6> (
		vec4f(-1., -1., 1., 1.),
		vec4f( 1., -1., 1., 1.),
		vec4f( 1.,  1., 1., 1.),

		vec4f( 1.,  1., 1., 1.),
		vec4f(-1.,  1., 1., 1.),
		vec4f(-1., -1., 1., 1.),
	);

	var vo : VertexOutput;
	vo.position = ps[vi.vertex_index];
	vo.uv = ps[vi.vertex_index].xy * .5 + .5;
	vo.uv.y = 1. - vo.uv.y;


	return vo;
}

@fragment
fn fs_main(vo: VertexOutput) -> @location(0) vec4<f32> {
	let texColor = textureSample(texColor, texSampler, vo.uv);

	var color = texColor;

	let depth : f32 = textureSample(texDepth, texSampler, vo.uv);
	let screen_pt = vec4f(vo.uv.x*2-1, -(vo.uv.y*2-1), depth, 1.);
	let world_pt4 = scd.imvp * screen_pt;

	var cmvp = castData.mvp;
	// cmvp[0][3] -= scd.eye[0];
	// cmvp[1][3] -= scd.eye[1];
	// cmvp[2][3] -= scd.eye[2];

	// NOTE: This offset on dc data does not help the quantization error much. I think that means `scd.imvp` is the issue.
	// let offset = vec3f(.17, -.75, .62);
	let offset = vec3f(0.);
	var castA_4 = (cmvp * vec4f(world_pt4.xyz/world_pt4.w - offset,1.));
	var castA_3 = castA_4.xyz / castA_4.w;
	let uv_cast = castA_3.xy * vec2f(.5, -.5) + .5;

	if (uv_cast.x > 0 && uv_cast.y > 0 && uv_cast.x < 1 && uv_cast.y < 1) {
		let alpha = castData.color.a;
		color += textureSample(castTex, texSampler, uv_cast) * vec4(castData.color.rgb, 1.) * alpha;
	}

	// color.g += castA_3.z;

	color = (color / color.a + 0.00001);

	return color;
}
)";
}

namespace wg {
	DeferredCast::DeferredCast(AppObjects& ao, const GlobeOptions& gopts, const AppOptions& appOpts) : ao(ao) {
		//
		// Textures
		//

		uint32_t w = appOpts.initialWidth;
		uint32_t h = appOpts.initialHeight;


            sampler       = ao.device.create(WGPUSamplerDescriptor {
                      .nextInChain  = nullptr,
                      .label        = "DeferredCastSampler",
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

		//
		// BindGroup + Layout
		//

            WGPUBindGroupLayoutEntry bglEntries[3] = {
                {
                 .nextInChain    = nullptr,
                 .binding        = 0,
                 .visibility     = WGPUShaderStage_Fragment,
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
                 .sampler        = { .nextInChain = nullptr, .type = WGPUSamplerBindingType_Undefined },
                 .texture        = { .nextInChain   = nullptr,
                 .sampleType    = WGPUTextureSampleType_Depth,
                 .viewDimension = WGPUTextureViewDimension_2D,
                 .multisampled  = false },
                 .storageTexture = { .nextInChain = nullptr, .access = WGPUStorageTextureAccess_Undefined },
                 },
                {
                 .nextInChain    = nullptr,
                 .binding        = 2,
                 .visibility     = WGPUShaderStage_Fragment,
                 .buffer         = WGPUBufferBindingLayout { .nextInChain = nullptr, .type = WGPUBufferBindingType_Undefined },
                 .sampler        = { .nextInChain = nullptr, .type = WGPUSamplerBindingType_Filtering },
                 .texture        = { .nextInChain = nullptr, .sampleType = WGPUTextureSampleType_Undefined },
                 .storageTexture = { .nextInChain = nullptr, .access = WGPUStorageTextureAccess_Undefined },
                 },
            };
            sceneFboBindGroupLayout              = ao.device.create(WGPUBindGroupLayoutDescriptor {
                             .nextInChain = nullptr, .label = "DeferredCastBGL", .entryCount = 3, .entries = bglEntries });


            WGPUBindGroupLayoutEntry bglEntries_tex[2] = {
                {
                 .nextInChain    = nullptr,
                 .binding        = 0,
                 .visibility     = WGPUShaderStage_Fragment,
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
            castTexBindGroupLayout              = ao.device.create(WGPUBindGroupLayoutDescriptor {
                             .nextInChain = nullptr, .label = "DeferredCastTexBGL", .entryCount = 2, .entries = bglEntries_tex });

		createSceneFboTextures(ao,w,h);

		//
		// Pipeline
		//

			ShaderModule shader { create_shader(ao.device, cast_source, "deferredCastShader") };

            WGPUBindGroupLayout bgls[3] = {
                ao.getSceneBindGroupLayout().ptr,
                sceneFboBindGroupLayout.ptr,
                castTexBindGroupLayout.ptr,
            };
            quadPipelineAndLayout.layout      = ao.device.create(WGPUPipelineLayoutDescriptor {
                     .nextInChain          = nullptr,
                     .label                = "fog",
                     .bindGroupLayoutCount = 3,
                     .bindGroupLayouts     = bgls,
            });

            auto vertexState       = WGPUVertexState_Default(shader);

            auto primState         = WGPUPrimitiveState_Default();
            primState.cullMode     = WGPUCullMode_None;
            auto multisampleState  = WGPUMultisampleState_Default();
            auto blend             = WGPUBlendState_Default();
            auto cst               = WGPUColorTargetState_Default(ao, blend);
            auto fragmentState     = WGPUFragmentState_Default(shader, cst);
            auto depthStencilState = WGPUDepthStencilState_Default(ao);

            WGPURenderPipelineDescriptor rpDesc { .nextInChain  = nullptr,
                                                  .label        = "fog",
                                                  .layout       = quadPipelineAndLayout.layout,
                                                  .vertex       = vertexState,
                                                  .primitive    = primState,
                                                  .depthStencil = ao.surfaceDepthStencilFormat == WGPUTextureFormat_Undefined
                                                                      ? nullptr
                                                                      : &depthStencilState,
                                                  .multisample  = multisampleState,
                                                  .fragment     = &fragmentState };
            quadPipelineAndLayout.pipeline = ao.device.create(rpDesc);

	}

	void DeferredCast::createSceneFboTextures(AppObjects& ao, int w, int h) {
		spdlog::get("wg")->info("DeferredCast::createSceneFboTextures {} {}", w,h);

				fboW = w;
				fboH = h;

                colorTexture     = ao.device.create(WGPUTextureDescriptor {
                        .nextInChain     = nullptr,
                        .label           = "DeferredCastColorTex",
                        .usage           = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment,
                        .dimension       = WGPUTextureDimension_2D,
                        .size            = WGPUExtent3D { (uint32_t)w,(uint32_t)h, 1 },
                        .format          = ao.surfaceColorFormat,
                        .mipLevelCount   = 1,
                        .sampleCount     = 1,
                        .viewFormatCount = 0,
                        .viewFormats     = 0
                });

                colorTexView = colorTexture.createView(WGPUTextureViewDescriptor {
                    .nextInChain     = nullptr,
                    .label           = "DeferredCastColorTexView",
                    .format          = ao.surfaceColorFormat,
                    .dimension       = WGPUTextureViewDimension_2D,
                    .baseMipLevel    = 0,
                    .mipLevelCount   = 1,
                    .baseArrayLayer  = 0,
                    .arrayLayerCount = 1,
                    .aspect          = WGPUTextureAspect_All,
                });

		depthTexture = ao.device.createDepthTexture(w,h, ao.surfaceDepthStencilFormat);

                depthTexView = depthTexture.createView(WGPUTextureViewDescriptor {
                    .nextInChain     = nullptr,
                    .label           = "DeferredCastDepthTexView",
                    .format          = ao.surfaceDepthStencilFormat,
                    .dimension       = WGPUTextureViewDimension_2D,
                    .baseMipLevel    = 0,
                    .mipLevelCount   = 1,
                    .baseArrayLayer  = 0,
                    .arrayLayerCount = 1,
                    .aspect          = WGPUTextureAspect_DepthOnly,
                });

            WGPUBindGroupEntry groupEntries[3] = {
                { .nextInChain = nullptr,
                 .binding     = 0,
                 .buffer      = 0,
                 .offset      = 0,
                 .size        = 0,
                 .sampler     = nullptr,
                 .textureView = colorTexView                                                                                    },
                { .nextInChain = nullptr,
                 .binding     = 1,
                 .buffer      = 0,
                 .offset      = 0,
                 .size        = 0,
                 .sampler     = nullptr,
                 .textureView = depthTexView                                                                                    },
                { .nextInChain = nullptr, .binding = 2, .buffer = 0, .offset = 0, .size = 0, .sampler = sampler, .textureView = 0 },
            };
            sceneFboBindGroup = ao.device.create(WGPUBindGroupDescriptor { .nextInChain = nullptr,
                                                                         .label       = "DeferredCastSceneFboBG",
                                                                         .layout      = sceneFboBindGroupLayout,
                                                                         .entryCount  = 3,
                                                                         .entries     = groupEntries });


	}

		void DeferredCast::setCastData(const DeferredCastData& d) {
			deferredCastDataCpu = d;
			if (castBufSize == 0) {
				castBufSize = roundUp<256>(sizeof(DeferredCastData));
				spdlog::get("wg")->info("creating deferredCastMvpBuf of size {}", castBufSize);
				WGPUBufferDescriptor desc {
					.nextInChain      = nullptr,
						.label            = "DeferredCastMvpBuffer",
						.usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform,
						.size             = (uint32_t)castBufSize,
						.mappedAtCreation = false,
				};
				deferredCastDataGpu = ao.device.create(desc);
			}
			ao.queue.writeBuffer(deferredCastDataGpu, 0, &deferredCastDataCpu, sizeof(DeferredCastData));
		}

		void DeferredCast::setCastTexture(const uint8_t* data, uint32_t w, uint32_t h, uint32_t c) {

			if (w <=0 or h<=0) {
				castTexW = castTexH = -1;
				return;
			}

			assert(c == 4);

			if (castTexW != w or castTexH != h) {
                castTexture  = ao.device.create(WGPUTextureDescriptor {
                        .nextInChain     = nullptr,
                        .label           = "DeferredCastCastTexture",
                        .usage           = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment,
                        .dimension       = WGPUTextureDimension_2D,
                        .size            = WGPUExtent3D { w,h, 1 },
						.format          = WGPUTextureFormat_RGBA8Unorm,
                        .mipLevelCount   = 1,
                        .sampleCount     = 1,
                        .viewFormatCount = 0,
                        .viewFormats     = 0
                });

                castTextureView = castTexture.createView(WGPUTextureViewDescriptor {
                    .nextInChain     = nullptr,
                    .label           = "DeferredCastTextureTexView",
                    .format          = WGPUTextureFormat_RGBA8Unorm,
                    .dimension       = WGPUTextureViewDimension_2D,
                    .baseMipLevel    = 0,
                    .mipLevelCount   = 1,
                    .baseArrayLayer  = 0,
                    .arrayLayerCount = 1,
                    .aspect          = WGPUTextureAspect_All,
                });

				// make sure we've created a buffer.
				if (castBufSize == 0) setCastData({});

            WGPUBindGroupEntry castTexGroupEntries[2] = {
                { .nextInChain = nullptr,
                 .binding     = 0,
                 .buffer      = 0,
                 .offset      = 0,
                 .size        = 0,
                 .sampler     = nullptr,
                 .textureView = castTextureView                                                                                    },
                { .nextInChain = nullptr,
                 .binding     = 1,
                 .buffer      = deferredCastDataGpu.ptr,
                 .offset      = 0,
                 .size        = (uint64_t)castBufSize,
                 .sampler     = nullptr,
                 .textureView = nullptr                                                                                    },
			};
            castTexBindGroup = ao.device.create(WGPUBindGroupDescriptor { .nextInChain = nullptr,
                                                                         .label       = "DeferredCastCastTexBG",
                                                                         .layout      = castTexBindGroupLayout,
                                                                         .entryCount  = 2,
                                                                         .entries     = castTexGroupEntries });
			}
			
			ao.queue.writeTexture(
					WGPUImageCopyTexture {
					.nextInChain = nullptr,
					.texture     = castTexture,
					.mipLevel    = 0,
					.origin      = WGPUOrigin3D { 0, 0, 0 },
					.aspect = WGPUTextureAspect_All,
					},
					data, w*h*c,
					WGPUTextureDataLayout {
					.nextInChain  = nullptr,
					.offset       = 0,
					.bytesPerRow  = w * c,
					.rowsPerImage = h,
					},
					WGPUExtent3D { w, h, 1 });
		}

		void DeferredCast::beginPass(CommandEncoder& ce, int w, int h) {
			if (w != fboW or h != fboH) {
				createSceneFboTextures(ao, w,h);
			}

			if (castTexture.ptr == nullptr) {
				uint8_t data[4] = {0,0,0,0};
				setCastTexture(data, 1,1,4);
			}

			rpe = ce.beginRenderPassBasic(ao, colorTexView, depthTexView, "fogPass");
		}
		void DeferredCast::endPass() {
			rpe.end();
			rpe.release();
		}

		void DeferredCast::renderAfterEndingPass(RenderState& rs) {
            rs.pass.setRenderPipeline(quadPipelineAndLayout.pipeline);
			rs.pass.setBindGroup(0, ao.getSceneBindGroup());
			rs.pass.setBindGroup(1, sceneFboBindGroup);
			rs.pass.setBindGroup(2, castTexBindGroup);
            rs.pass.draw(6);
		}
}

