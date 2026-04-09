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

	wh: vec2f,
	sun: vec4f,
	haeAlt: f32,
	haze: f32,
	time: f32,
	dt: f32,
}

struct ModelData {
    model: mat4x4<f32>,
    color: vec4f,
}

@group(0) @binding(0)
var<uniform> scd: SceneCameraData;
@group(1) @binding(0)
var<uniform> modelData: ModelData;

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) uv: vec2<f32>,
    @location(2) normal: vec3<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
    @location(1) uv: vec2<f32>,
};

@vertex
fn vs_main(vi: VertexInput) -> VertexOutput {
	var vo : VertexOutput;

	var p = scd.mvp * modelData.model * vec4(vi.position, 1.);
	vo.position = p;

	//vo.color = vec4f(vi.normal,1) * scd.colorMult;
	vo.color = modelData.color * scd.colorMult;

	vo.uv = vi.uv;

	var d = dot(vi.normal, -normalize(transpose(scd.mv * modelData.model)[2].xyz));
	// d = pow(1. - d, 2.);
	d = (1. - d);

	vo.color.a = d;

	return vo;

}

@fragment
fn fs_main(vo: VertexOutput) -> @location(0) vec4<f32> {
	return vo.color;
}
)";
}

namespace wg {
    namespace {

        struct ModelData {
            float model[16];
            float color[4];
        };

    struct TransformedSphere : public TransformedEntity {

        size_t bufSize;
        int nverts;
		int nindex;

		Buffer modelBuffer;
		BindGroupLayout modelBindGroupLayout;
		BindGroup modelBindGroup;

        AppObjects& ao;

        ModelData modelData;


        float color[4] = {1,1,1,1};

        inline TransformedSphere(AppObjects& ao, int numRows, int numCols) : ao(ao) {

            makeVertexBuffer_(ao, numRows, numCols);

            makeModelStuff();

            // makeCameraBindGroup_(ao);
            makePipeline_(ao);
        }

        void makeModelStuff() {
            WGPUBufferDescriptor desc {
                .nextInChain      = nullptr,
                .label            = "TSphere_modelBuffer",
                .usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform,
                .size             = roundUp<256>(sizeof(float)*16),
                .mappedAtCreation = true,
            };
            bzero(&modelData, sizeof(ModelData));
            modelData.color[0] = modelData.color[1] = modelData.color[2] = modelData.color[3] = 1;
            modelData.model[0] = modelData.model[4] = modelData.model[9] = modelData.model[15] = 1;
            modelBuffer = ao.device.create(desc);
            void* dst = wgpuBufferGetMappedRange(modelBuffer, 0, sizeof(ModelData));
            memcpy(dst, &modelData, sizeof(ModelData));
            wgpuBufferUnmap(modelBuffer);

            WGPUBindGroupLayoutEntry layoutEntry {
                .nextInChain    = nullptr,
                .binding        = 0,
                .visibility     = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
                .buffer         = WGPUBufferBindingLayout { .nextInChain      = nullptr,
                                                           .type             = WGPUBufferBindingType_Uniform,
                                                           .hasDynamicOffset = false,
                                                           .minBindingSize   = 0 },
                .sampler        = { .nextInChain = nullptr, .type = WGPUSamplerBindingType_Undefined },
                .texture        = { .nextInChain = nullptr, .sampleType = WGPUTextureSampleType_Undefined },
                .storageTexture = { .nextInChain = nullptr, .access = WGPUStorageTextureAccess_Undefined },
            };
            modelBindGroupLayout = ao.device.create(WGPUBindGroupLayoutDescriptor {
                .nextInChain = nullptr, .label = "tsphereModelBgl", .entryCount = 1, .entries = &layoutEntry });

            WGPUBindGroupEntry groupEntry { .nextInChain = nullptr,
                                            .binding     = 0,
                                            .buffer      = modelBuffer,
                                            .offset      = 0,
                                            .size        = roundUp<256>(sizeof(ModelData)),
                                            .sampler     = nullptr,
                                            .textureView = nullptr };
            modelBindGroup = ao.device.create(WGPUBindGroupDescriptor { .nextInChain = nullptr,
                                                                   .label       = "tsphereBindGroup",
                                                                   .layout      = modelBindGroupLayout,
                                                                   .entryCount  = 1,
                                                                   .entries     = &groupEntry });
        }

        inline virtual void render(const RenderState& rs) override {
            assert(rndrPipe);
            rs.pass.setRenderPipeline(rndrPipe);
            rs.pass.setBindGroup(0, rs.appObjects.getSceneBindGroup());
            rs.pass.setBindGroup(1, modelBindGroup);
            rs.pass.setVertexBuffer(0, vbo, 0, vbo.getSize());
            rs.pass.setIndexBuffer(ibo, WGPUIndexFormat_Uint32, 0, ibo.getSize());
            rs.pass.drawIndexed(nindex);
        }

        Buffer vbo;
        Buffer ibo;
		PipelineLayout pipelineLayout;
        RenderPipeline rndrPipe;

        virtual void setModelTransform(float model[16]) override {
            memcpy(modelData.model, model, sizeof(float)*16);
			ao.queue.writeBuffer(modelBuffer, 0, &modelData, sizeof(ModelData));
        }
        virtual void setModelColor(float color[4]) override {
            if (modelData.color[0] != color[0] or modelData.color[3] != color[3] or modelData.color[1] != color[1]) {
                memcpy(modelData.color, color, sizeof(float)*4);
                ao.queue.writeBuffer(modelBuffer, 0, &modelData, sizeof(ModelData));
            }
        }

        inline void makeVertexBuffer_(AppObjects& ao, int rows, int cols) {

            nverts = rows * cols;
            spdlog::get("wg")->info("making {} verts", nverts);

            // vert ~ pos, uv, normal
            bufSize = nverts * (3 + 2 + 3) * sizeof(float);

            std::vector<double> pts;
            std::vector<float> uvs;
            std::vector<uint32_t> inds;

            for (int r = 0; r < rows; r++) {
                for (int c = 0; c < cols; c++) {
                    double x   = static_cast<double>(c) / (cols - 1);
                    double y   = static_cast<double>(r) / (rows - 1);

                    double xx = std::cos(x * M_PI * 2) * std::cos(y * M_PI - M_PI/2);
                    double yy = std::sin(x * M_PI * 2) * std::cos(y * M_PI - M_PI/2);
                    double zz = std::sin(y * M_PI - M_PI/2);

                    pts.push_back(xx);
                    pts.push_back(yy);
                    pts.push_back(zz);
                    uvs.push_back(x);
                    uvs.push_back(y);

					if (r < rows - 1) {
						int I = ((r + 0) % rows) * cols + ((c + 0) % cols);
						int J = ((r + 1) % rows) * cols + ((c + 0) % cols);
						int K = ((r + 1) % rows) * cols + ((c + 1) % cols);
						int L = ((r + 0) % rows) * cols + ((c + 1) % cols);
						inds.push_back(I);
						inds.push_back(J);
						inds.push_back(K);
						inds.push_back(K);
						inds.push_back(L);
						inds.push_back(I);
					}
                }
            }

			for (uint32_t i=0; i<inds.size(); i++) assert(inds[i] >= 0 and inds[i] < pts.size()/3);
			nindex = inds.size();

            std::vector<float> verts;
            verts.resize(nverts * (3 * 2 + 3));
            int j = 0;
            for (int i = 0; i < nverts; i++) {
                verts[j++] = pts[i * 3 + 0];
                verts[j++] = pts[i * 3 + 1];
                verts[j++] = pts[i * 3 + 2];
                verts[j++] = uvs[i * 2 + 0];
                verts[j++] = uvs[i * 2 + 1];
                double n[3];
                verts[j++] = pts[i*3+0];
                verts[j++] = pts[i*3+1];
                verts[j++] = pts[i*3+2];
            }
            spdlog::get("wg")->info("made verts");

            WGPUBufferDescriptor desc {
                .nextInChain      = nullptr,
                .label            = "TEllipsoid_vbo",
                .usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex,
                .size             = bufSize,
                .mappedAtCreation = true,
            };
            spdlog::get("wg")->info("creating vbo");
            vbo = ao.device.create(desc);
            spdlog::get("wg")->info("uploading verts");

            void* dst = wgpuBufferGetMappedRange(vbo, 0, bufSize);
            memcpy(dst, verts.data(), bufSize);

            wgpuBufferUnmap(vbo);
            spdlog::get("wg")->info("created vbo");

			// Index buffer
			size_t indsBufSize = sizeof(uint32_t) * inds.size();
            WGPUBufferDescriptor desc2 {
                .nextInChain      = nullptr,
                .label            = "TEllipsoid_ibo",
                .usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index,
                .size             = indsBufSize,
                .mappedAtCreation = true,
            };
            ibo = ao.device.create(desc2);
            void* dst2 = wgpuBufferGetMappedRange(ibo, 0, indsBufSize);
            memcpy(dst2, inds.data(), indsBufSize);
            wgpuBufferUnmap(ibo);
        }

        inline void makePipeline_(AppObjects& ao) {
            ShaderModule shader { create_shader(ao.device, shaderSource1, "ellipsoidShader") };

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
            WGPUVertexBufferLayout bufferLayout {
                .arrayStride    = (3 + 2 + 3) * sizeof(float),
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
                .cullMode         = WGPUCullMode_Back,
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



            WGPUBindGroupLayout bgls[2] = {
                ao.getSceneBindGroupLayout().ptr,
                modelBindGroupLayout.ptr,
            };
			pipelineLayout = ao.device.create(WGPUPipelineLayoutDescriptor {
					.nextInChain = nullptr,
					.label = "tsphere",
					.bindGroupLayoutCount = 2,
					.bindGroupLayouts = bgls,
			});

            WGPURenderPipelineDescriptor desc {
                .nextInChain  = nullptr,
                .label        = "tsphere",
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
    }

    std::shared_ptr<TransformedEntity> createTransformedSphere(AppObjects& ao, int rows, int cols) {
        return std::make_shared<TransformedSphere>(ao, rows, cols);
    }

}

