#include "app/shader.h"
#include "entity.h"

namespace {
    const char* shaderSource1 = R"(

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) uv: vec2<f32>,
    @location(2) color: vec4<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
    @location(1) uv: vec2<f32>,
};

@vertex
fn vs_main(vi: VertexInput) -> VertexOutput {
	var vo : VertexOutput;

	vo.position = vec4f(vi.position * .05, 1.);
	// vo.position = vec4f(vi.position, 1.);

	vo.color = vi.color;
	vo.uv = vi.uv;

	return vo;

}

@fragment
fn fs_main(vo: VertexOutput) -> @location(0) vec4<f32> {
	return vo.color;
}
)";
}

namespace wg {

    struct SimpleTri2 : public Entity {

        inline SimpleTri2(AppObjects& ao) {
            makeBuffer_(ao);
            makePipeline_(ao);
        }

        inline virtual void render(const RenderState& rs) override {
            assert(rndrPipe);
            rs.pass.setRenderPipeline(rndrPipe);
            rs.pass.setVertexBuffer(0, buffer, 0, buffer.getSize());
            rs.pass.draw(3, 1, 0, 0);
        }

        RenderPipeline rndrPipe;
        Buffer buffer;

        inline void makeBuffer_(AppObjects& ao) {
            constexpr size_t bufSize = 3 * (3 + 2 + 4) * 4;
            float verts[bufSize / 4] = {
                -.5f, -.4f, 0, 0, 0, 1, 0, 0, 1, .5f, -.4f, 0, 0, 0, 0, 1, 0, 1, .0f, .4f, 0, 0, 0, 0, 0, 1, 1,
            };
            WGPUBufferDescriptor desc {
                .nextInChain      = nullptr,
                .label            = "SimpleTri2_buffer",
                .usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex,
                .size             = bufSize,
                .mappedAtCreation = true,
            };
            buffer    = ao.device.create(desc);

            void* dst = wgpuBufferGetMappedRange(buffer, 0, bufSize);
            memcpy(dst, verts, bufSize);

            wgpuBufferUnmap(buffer);
        }

        inline void makePipeline_(AppObjects& ao) {
            ShaderModule shader { create_shader(ao.device, shaderSource1, "simpleTriShader") };

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
                                     .format         = WGPUVertexFormat_Float32x4,
                                     .offset         = 5 * sizeof(float),
                                     .shaderLocation = 2,
                                     },
            };
            WGPUVertexBufferLayout bufferLayout {
                .arrayStride    = (3 + 2 + 4) * sizeof(float),
                .stepMode       = WGPUVertexStepMode_Vertex,
                .attributeCount = 3,
                .attributes     = attributes,
            };

            WGPUVertexState vertexState { .nextInChain   = nullptr,
                                          .module        = shader,
                                          .entryPoint    = "vs_main",
                                          .constantCount = 0,
                                          .constants     = nullptr,
                                          .bufferCount   = 1,
                                          .buffers       = &bufferLayout };

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

    std::shared_ptr<Entity> createSimpleTri2(AppObjects& ao) {
        return std::make_shared<SimpleTri2>(ao);
    }

}
