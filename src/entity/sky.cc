#include "app/shader.h"
#include "entity.h"
#include "geo/conversions.h"

#include <Eigen/Core>
#include <spdlog/spdlog.h>

namespace {
    const char* shaderSource1 = R"(
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

struct VertexInput {
    @builtin(vertex_index) vertex_index: u32,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) p: vec3<f32>,
    @location(1) ray: vec3<f32>,
};

@vertex
fn vs_main(vi: VertexInput) -> VertexOutput {
	var vo : VertexOutput;

	var ps = array<vec4f, 4> (
		vec4f(-1., -1., 1., 1.),
		vec4f( 1., -1., 1., 1.),
		vec4f( 1.,  1., 1., 1.),
		vec4f(-1.,  1., 1., 1.),
	);

	var indices = array<u32,6>(0,1,2, 2,3,0);
	var i = indices[vi.vertex_index];

	vo.position = ps[i];
	vo.p = ps[i].xyz;

	return vo;
}

@fragment
fn fs_main(vo: VertexOutput) -> @location(0) vec4<f32> {

	// var p = vo.position;
	var p = vo.p;

	var c = vec4f(1.);

	let ray0 = scd.imvp * vec4f(p[0], p[1], p[2], 1.);
	let ray = normalize(ray0.xyz);

	if (true) {
		var r = length(scd.eye + ray * scd.haeAlt);

		let s = 1. - clamp(r - 1., 0., 1.);
		let t = clamp(r - 1., 0., 1.);
		c[0] = pow(smoothstep(.5, .0, s), 3.);
		c[1] = pow(smoothstep(.7, .0, s), 3.) + pow(smoothstep(.7, 1., s), 5.);
		c[2] = s;
	}

	c *= exp(-clamp(6.*(scd.haeAlt - .4), 0., 100.));

	return c;
}
)";
}

namespace wg {

    struct Sky : public Entity {

        inline Sky(AppObjects& ao) {
            makePipeline_(ao);
        }

        inline virtual void render(const RenderState& rs) override {
            assert(rndrPipe);
            rs.pass.setRenderPipeline(rndrPipe);
            rs.pass.setBindGroup(0, rs.appObjects.getSceneBindGroup());
            rs.pass.draw(6);
        }

        Buffer cameraBuffer;
        Buffer indexBuffer;
        Buffer vbo;
        Buffer ibo;
		PipelineLayout pipelineLayout;
        RenderPipeline rndrPipe;

        inline void makePipeline_(AppObjects& ao) {
            ShaderModule shader { create_shader(ao.device, shaderSource1, "skyShader") };
			// throw std::runtime_error("ex");

            WGPUVertexState vertexState { .nextInChain   = nullptr,
                                          .module        = shader,
                                          .entryPoint    = "vs_main",
                                          .constantCount = 0,
                                          .constants     = nullptr,
                                          .bufferCount   = 0,
                                          .buffers       = nullptr };

            WGPUPrimitiveState primState {
                .nextInChain      = nullptr,
                .topology         = WGPUPrimitiveTopology_TriangleList,
                .stripIndexFormat = WGPUIndexFormat_Undefined,
                .frontFace        = WGPUFrontFace_CW,
                .cullMode         = WGPUCullMode_None,
            };

            WGPUDepthStencilState depthStencilState {
                .nextInChain         = nullptr,
                .format              = ao.surfaceDepthStencilFormat,
                .depthWriteEnabled   = true,
                // .depthCompare        = WGPUCompareFunction_Less,
                .depthCompare        = WGPUCompareFunction_Never,
                .stencilFront        = WGPUStencilFaceState_Default(),
                .stencilBack         = WGPUStencilFaceState_Default(),
                .stencilReadMask     = 0,
                .stencilWriteMask    = 0,
                .depthBias           = 0,
                .depthBiasSlopeScale = 0.f,
                .depthBiasClamp      = 0.f,
            };

            WGPUMultisampleState multisampleState { .nextInChain = nullptr, .count = 1, .mask = ~0u, .alphaToCoverageEnabled = false };

            WGPUBlendState blend {
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
            WGPUColorTargetState colorTargetState {
                .nextInChain = nullptr,
                .format      = ao.surfaceColorFormat,
                .blend       = &blend,
                .writeMask   = WGPUColorWriteMask_All,
            };
            WGPUFragmentState fragmentState { .nextInChain   = nullptr,
                                              .module        = shader,
                                              .entryPoint    = "fs_main",
                                              .constantCount = 0,
                                              .constants     = nullptr,
                                              .targetCount   = 1,
                                              .targets       = &colorTargetState };


			pipelineLayout = ao.device.create(WGPUPipelineLayoutDescriptor {
					.nextInChain = nullptr,
					.label = "sky",
					.bindGroupLayoutCount = 1,
					.bindGroupLayouts = &ao.getSceneBindGroupLayout().ptr
			});

            WGPURenderPipelineDescriptor desc {
                .nextInChain  = nullptr,
                .label        = "sky",
                .layout       = pipelineLayout,
                .vertex       = vertexState,
                .primitive    = primState,
                .depthStencil = ao.surfaceDepthStencilFormat == WGPUTextureFormat_Undefined ? nullptr : &depthStencilState,
                .multisample = multisampleState,
                .fragment    = &fragmentState
            };

            rndrPipe = ao.device.create(desc);
        }
    };

    std::shared_ptr<Entity> createSky(AppObjects& ao) {
        return std::make_shared<Sky>(ao);
    }

}

