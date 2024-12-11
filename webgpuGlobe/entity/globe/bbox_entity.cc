#include "globe.h"

#include <Eigen/Core>
#include <spdlog/spdlog.h>

#include "app/shader.h"
#include "util/fmtEigen.h"

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

@group(0) @binding(0)
var<uniform> scd: SceneCameraData;

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) color: vec4<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
};

@vertex
fn vs_main(vi: VertexInput) -> VertexOutput {
	var vo : VertexOutput;

	var p = scd.mvp * vec4(vi.position, 1.);
	vo.position = p;

	vo.color = vi.color * scd.colorMult;

	return vo;

}

@fragment
fn fs_main(vo: VertexOutput) -> @location(0) vec4<f32> {
	return vo.color;
}
)";
}

namespace wg {

	InefficientBboxEntity::InefficientBboxEntity(AppObjects& ao) : ao(ao) {

            std::vector<float> verts {
				-1, -1, -1,       1, 1, 1, .5f,
				 1, -1, -1,       1, 1, 1, .5f,
				 1,  1, -1,       1, 1, 1, .5f,
				-1,  1, -1,       1, 1, 1, .5f,
				-1, -1,  1,       1, 1, 1, .5f,
				 1, -1,  1,       1, 1, 1, .5f,
				 1,  1,  1,       1, 1, 1, .5f,
				-1,  1,  1,       1, 1, 1, .5f,
			};

            WGPUBufferDescriptor desc {
                .nextInChain      = nullptr,
                .label            = "BB_vbo",
                .usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex,
                .size             = verts.size() * sizeof(float),
                .mappedAtCreation = true,
            };
            vbo = ao.device.create(desc);
            void* dst = wgpuBufferGetMappedRange(vbo, 0, verts.size() * sizeof(float));
            memcpy(dst, verts.data(), verts.size() * sizeof(float));
            wgpuBufferUnmap(vbo);
            spdlog::get("wg")->info("created vbo");


		// Index buffer
		std::vector<uint16_t> inds {
				0, 1,
				1, 2,
				2, 3,
				3, 0,

				4, 5,
				5, 6,
				6, 7,
				7, 4,

				0, 4,
				1, 5,
				2, 6,
				3, 7
		};
		size_t indsBufSize = sizeof(uint16_t) * inds.size();
		WGPUBufferDescriptor desc2 {
			.nextInChain      = nullptr,
			.label            = "BB_ibo",
			.usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index,
			.size             = indsBufSize,
			.mappedAtCreation = true,
		};
		ibo = ao.device.create(desc2);
		void* dst2 = wgpuBufferGetMappedRange(ibo, 0, indsBufSize);
		memcpy(dst2, inds.data(), indsBufSize);
		wgpuBufferUnmap(ibo);
		nindex = inds.size();


		{
            ShaderModule shader { create_shader(ao.device, shaderSource1, "bbShader") };

            WGPUVertexAttribute attributes[2] = {
                WGPUVertexAttribute {
                                     .format         = WGPUVertexFormat_Float32x3,
                                     .offset         = 0 * sizeof(float),
                                     .shaderLocation = 0,
                                     },
                WGPUVertexAttribute {
                                     .format         = WGPUVertexFormat_Float32x4,
                                     .offset         = 3 * sizeof(float),
                                     .shaderLocation = 1,
                                     },
            };
            WGPUVertexBufferLayout bufferLayout {
                .arrayStride    = (3 + 4) * sizeof(float),
                .stepMode       = WGPUVertexStepMode_Vertex,
                .attributeCount = 2,
                .attributes     = attributes,
            };

            WGPUVertexState vertexState { .nextInChain   = nullptr,
                                          .module        = shader,
                                          .entryPoint    = "vs_main",
                                          .constantCount = 0,
                                          .constants     = nullptr,
                                          .bufferCount   = 1,
                                          .buffers       = &bufferLayout };

            auto primState         = WGPUPrimitiveState_Default();
            primState.topology     = WGPUPrimitiveTopology_LineList;
            auto multisampleState  = WGPUMultisampleState_Default();
            auto blend             = WGPUBlendState_Default();
            auto cst               = WGPUColorTargetState_Default(ao, blend);
            auto fragmentState     = WGPUFragmentState_Default(shader, cst);
            auto depthStencilState = WGPUDepthStencilState_Default(ao);

			pipelineLayout = ao.device.create(WGPUPipelineLayoutDescriptor {
					.nextInChain = nullptr,
					.label = "bb",
					.bindGroupLayoutCount = 1,
					.bindGroupLayouts = &ao.getSceneBindGroupLayout().ptr
			});

            WGPURenderPipelineDescriptor desc {
                .nextInChain  = nullptr,
                .label        = "bb",
                .layout       = pipelineLayout,
                .vertex       = vertexState,
                .primitive    = primState,
                .depthStencil = ao.surfaceDepthStencilFormat == WGPUTextureFormat_Undefined ? nullptr : &depthStencilState,
                .multisample = multisampleState,
                .fragment    = &fragmentState
            };

            rndrPipe = ao.device.create(desc);
		}



	}

		void InefficientBboxEntity::set(const UnpackedOrientedBoundingBox& uobb) {
			/*
			float* ptr;
			size_t bufSize = (3+4) * 8 * sizeof(float);
			void* dst = wgpuBufferGetMappedRange(vbo, 0, bufSize);
			memcpy(dst, ptr, bufSize);

			wgpuBufferUnmap(vbo);
			*/

			// WARNING: Possibly very inefficient? But annoying to map buffer async.

			Vector4f color { 1,1,1, .5f };
			Matrix<float,8,7,RowMajor> verts;
			verts.rightCols<4>().rowwise() = color.transpose();
			verts.leftCols<3>() = uobb.pts;

            // spdlog::get("wg")->info("created vbo:\n{}",verts.rowwise() - verts.row(0));


            WGPUBufferDescriptor desc {
                .nextInChain      = nullptr,
                .label            = "BB_vbo",
                .usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex,
                .size             = verts.rows() * verts.cols() * sizeof(float),
                .mappedAtCreation = true,
            };
            vbo = ao.device.create(desc);

            void* dst = wgpuBufferGetMappedRange(vbo, 0, verts.rows() * verts.cols() * sizeof(float));
            memcpy(dst, verts.data(), verts.rows() * verts.cols() * sizeof(float));
            wgpuBufferUnmap(vbo);
		}

        void InefficientBboxEntity::render(const RenderState& rs) {
            assert(rndrPipe);
            rs.pass.setRenderPipeline(rndrPipe);
            rs.pass.setBindGroup(0, rs.appObjects.getSceneBindGroup());
            rs.pass.setVertexBuffer(0, vbo, 0, vbo.getSize());
            rs.pass.setIndexBuffer(ibo, WGPUIndexFormat_Uint16, 0, ibo.getSize());
            rs.pass.drawIndexed(nindex);
		}

}
