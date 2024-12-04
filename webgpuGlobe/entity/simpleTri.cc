#include "app/shader.h"
#include "entity.h"

namespace {
    const char* shaderSource1 = R"(

struct VertexInput {
	@builtin(vertex_index) i: u32,
    // @location(0) position: vec2<f32>,
    // @location(1) uv: vec2<f32>,
    // @location(2) color: vec4<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
	@location(0) face: f32,
    // @location(0) color: vec4<f32>,
    // @location(1) uv: vec2<f32>,
};

@vertex
fn vs_main(vi: VertexInput) -> VertexOutput {
	var out : VertexOutput;

	var d = 0.5f;

	var i = vi.i;
	if (i == 0) { out.position = vec4<f32>(-d,-d, 1.0, 1.0); }
	if (i == 1) { out.position = vec4<f32>( d,-d, 1.0, 1.0); }
	if (i == 2) { out.position = vec4<f32>( d, d, 1.0, 1.0); }

	if (i == 3) { out.position = vec4<f32>( d, d, 1.0, 1.0); }
	if (i == 4) { out.position = vec4<f32>(-d, d, 1.0, 1.0); }
	if (i == 5) { out.position = vec4<f32>(-d,-d, 1.0, 1.0); }

	out.face = select(0., 1., i >= 3);

	return out;

}

@fragment
fn fs_main(vo: VertexOutput) -> @location(0) vec4<f32> {
	return vec4<f32>(vo.face, 0.9, 0.0, 1.0);
}
)";
}

namespace wg {

    struct SimpleTri : public Entity {

        inline SimpleTri(AppObjects& ao) {
            makePipeline_(ao);
        }

        inline virtual void render(const RenderState& rs) override {
            assert(rndrPipe);
            rs.pass.setRenderPipeline(rndrPipe);
            rs.pass.draw(6, 1, 0, 0);
        }

        RenderPipeline rndrPipe;

        inline void makePipeline_(AppObjects& ao) {
            ShaderModule shader { create_shader(ao.device, shaderSource1, "simpleTriShader") };

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
                .depthCompare        = WGPUCompareFunction_Less,
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

            WGPURenderPipelineDescriptor desc {
                .nextInChain  = nullptr,
                .label        = "simpleTri",
                .layout       = nullptr,
                .vertex       = vertexState,
                .primitive    = primState,
                .depthStencil = ao.surfaceDepthStencilFormat == WGPUTextureFormat_Undefined ? nullptr : &depthStencilState,
                // .depthStencil = nullptr,
                .multisample = multisampleState,
                .fragment    = &fragmentState
                // .fragment     = nullptr
            };

            rndrPipe = ao.device.create(desc);
        }
    };

    std::shared_ptr<Entity> createSimpleTri(AppObjects& ao) {
        return std::make_shared<SimpleTri>(ao);
    }

}
