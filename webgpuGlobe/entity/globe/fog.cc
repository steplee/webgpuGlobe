#include "fog.h"
#include "app/shader.h"

namespace {
    const char* quadSource = R"(

struct SceneCameraData {
	mvp: mat4x4<f32>,
	imvp: mat4x4<f32>,
	mv: mat4x4<f32>,

	eye: vec3f,
	colorMult: vec4f,

	sun: vec4f,
	haeAlt: f32,
	haze: f32,
	time: f32,
	dt: f32,
}

@group(0) @binding(0)
var<uniform> scd: SceneCameraData;

@group(1) @binding(0) var texColor: texture_2d<f32>;
@group(1) @binding(1) var texDepth: texture_depth_2d;
@group(1) @binding(2) var texSampler: sampler;

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
	// let texColor = vec4f(1.);

	var c = texColor;

	let depth : f32 = textureSample(texDepth, texSampler, vo.uv);
	// let screen_pt = vec4f(vo.uv.x*2-1, vo.uv.y*2-1, depth, 1.);
	let screen_pt = vec4f(vo.uv.x*2-1, -(vo.uv.y*2-1), depth, 1.);
	let world_pt4 = scd.imvp * screen_pt;

	let world_pt3 = world_pt4.xyz / world_pt4.w;
	let d = distance(scd.eye,world_pt3);

	let fog_color = vec4f(.01, .3, .9, .9);

	var fog = 1. - exp(-d*8.);
	// fog *= pow(smoothstep(1.3,.997, scd.haeAlt), 4.);
	fog *= pow(smoothstep(.9,.0, scd.haeAlt), 6.);

	c = mix(c, fog_color, fog);

	// c.r *= d;
	// c.g *= d;
	// c.b *= d;

	return c;
}
)";
}

namespace wg {
	Fog::Fog(AppObjects& ao, const GlobeOptions& gopts, const AppOptions& appOpts) : ao(ao) {
		//
		// Textures
		//

		uint32_t w = appOpts.initialWidth;
		uint32_t h = appOpts.initialHeight;
                colorTexture     = ao.device.create(WGPUTextureDescriptor {
                        .nextInChain     = nullptr,
                        .label           = "FogColorTex",
                        .usage           = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment,
                        .dimension       = WGPUTextureDimension_2D,
                        .size            = WGPUExtent3D { w,h, 1 },
                        .format          = ao.surfaceColorFormat,
                        .mipLevelCount   = 1,
                        .sampleCount     = 1,
                        .viewFormatCount = 0,
                        .viewFormats     = 0
                });

                colorTexView = colorTexture.createView(WGPUTextureViewDescriptor {
                    .nextInChain     = nullptr,
                    .label           = "FogColorTexView",
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
                    .label           = "FogDepthTexView",
                    .format          = ao.surfaceDepthStencilFormat,
                    .dimension       = WGPUTextureViewDimension_2D,
                    .baseMipLevel    = 0,
                    .mipLevelCount   = 1,
                    .baseArrayLayer  = 0,
                    .arrayLayerCount = 1,
                    .aspect          = WGPUTextureAspect_DepthOnly,
                });

            sampler       = ao.device.create(WGPUSamplerDescriptor {
                      .nextInChain  = nullptr,
                      .label        = "FogSampler",
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
            bindGroupLayout              = ao.device.create(WGPUBindGroupLayoutDescriptor {
                             .nextInChain = nullptr, .label = "FogBGL", .entryCount = 3, .entries = bglEntries });

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
            bindGroup = ao.device.create(WGPUBindGroupDescriptor { .nextInChain = nullptr,
                                                                         .label       = "FogBG",
                                                                         .layout      = bindGroupLayout,
                                                                         .entryCount  = 3,
                                                                         .entries     = groupEntries });

		//
		// Pipeline
		//

			ShaderModule shader { create_shader(ao.device, quadSource, "fogShader") };

            WGPUBindGroupLayout bgls[2] = {
                ao.getSceneBindGroupLayout().ptr,
                bindGroupLayout.ptr,
            };
            quadPipelineAndLayout.layout      = ao.device.create(WGPUPipelineLayoutDescriptor {
                     .nextInChain          = nullptr,
                     .label                = "fog",
                     .bindGroupLayoutCount = 2,
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

		void Fog::beginPass(CommandEncoder& ce) {
			rpe = ce.beginRenderPassBasic(ao, colorTexView, depthTexView, "fogPass");
		}
		void Fog::endPass() {
			rpe.end();
			rpe.release();
		}

		void Fog::renderAfterEndingPass(RenderState& rs) {
            rs.pass.setRenderPipeline(quadPipelineAndLayout.pipeline);
			rs.pass.setBindGroup(0, ao.getSceneBindGroup());
			rs.pass.setBindGroup(1, bindGroup);
            rs.pass.draw(6);
		}
}
