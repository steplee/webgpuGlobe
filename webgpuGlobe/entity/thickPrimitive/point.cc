#include "point.h"
#include "point_shaders.hpp"
#include "webgpuGlobe/app/shader.h"

namespace wg {

        void ThickPointEntity::render(const RenderState& rs) {
			if (nverts == 0) return;

            assert(rpwl);
            rs.pass.setRenderPipeline(rpwl->pipeline);
            rs.pass.setBindGroup(0, rs.appObjects.getSceneBindGroup());
            rs.pass.setVertexBuffer(0, vbo, 0, vboSize);
			if (iboSize) {
				rs.pass.setIndexBuffer(ibo, WGPUIndexFormat_Uint32, 0, iboSize);
				rs.pass.drawIndexed(nindex);
			} else {
				rs.pass.draw(nverts);
			}
        }

        void ThickPointEntity::set(AppObjects& ao, ThickPointData lineData) {
			makeOrGetPipeline_(ao, lineData);
			makeOrUploadBuffers_(ao, lineData);
		}


		void ThickPointEntity::makeOrUploadBuffers_(AppObjects& ao, const ThickPointData& pd) {
			int width = 0;
			if (pd.havePos) width += 4;
			if (pd.haveColor) width += 4;
			if (pd.haveNormal) width += 3;
			assert(!pd.haveNormal);

			// We have two triangles per pair of lines.
			nverts = pd.nverts * 6;
			nindex = 0;
			// spdlog::get("wg")->info("thick point (nindex {}, nvert {}, from original verts {})", nindex, nverts, pd.nverts);

			// Create verts
			assert(pd.vertData != nullptr);


			std::vector<float> verts;
			verts.resize(nverts*width);
			for (int oi=0; oi<nverts; oi++) {
				int vi, vj;
				vi = oi/6;
				vj = oi/6+1;
				// spdlog::get("wg")->info("thick point oi={}, vi={}, vj={}", oi,vi,vj);

				verts[oi*width+0] = pd.vertData[vi*width+0];
				verts[oi*width+1] = pd.vertData[vi*width+1];
				verts[oi*width+2] = pd.vertData[vi*width+2];
				verts[oi*width+3] = pd.vertData[vi*width+3];

				if (pd.haveColor) {
					verts[oi*width+4] = pd.vertData[vi*width+4];
					verts[oi*width+5] = pd.vertData[vi*width+5];
					verts[oi*width+6] = pd.vertData[vi*width+6];
					verts[oi*width+7] = pd.vertData[vi*width+7];
				}
			}


			size_t newVboSize = sizeof(float) * width * nverts;
			size_t newIboSize = sizeof(uint32_t) * nindex;



			if (newVboSize) {
				auto vboCapacity = vbo ? vbo.getSize() : 0;

				if (newVboSize > vboCapacity) {
					spdlog::get("wg")->info("need new vbo ({} > {})", newVboSize, vboCapacity);
					WGPUBufferDescriptor desc {
						.nextInChain      = nullptr,
						.label            = "thickPointVbo",
						.usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex,
						.size             = newVboSize,
						.mappedAtCreation = true,
					};
					vbo = ao.device.create(desc);
					void* dst = wgpuBufferGetMappedRange(vbo, 0, newVboSize);
					memcpy(dst, verts.data(), newVboSize);
					wgpuBufferUnmap(vbo);
				} else {
					// Re-use it.
					// spdlog::get("wg")->debug("reuse vbo ({} <= {})", newVboSize, vboCapacity);
					ao.queue.writeBuffer(vbo, 0, verts.data(), newVboSize);
				}

				vboSize = newVboSize;
			} else {
				vboSize = 0;
			}

			iboSize = 0;
		}

		void ThickPointEntity::makeOrGetPipeline_(AppObjects& ao, const ThickPointData& pd) {
			const char* shaderName = nullptr;
			const char* shaderSrcPtr = nullptr;

			if (!pd.havePos) {
				assert(false and "must provide position");
			}

			if (!pd.haveColor and !pd.haveNormal) {
				shaderName = "pos";
				// shaderSrcPtr = src_flat_pos;
				assert(false && "only pos+color supported right now");
			} else if (!pd.haveNormal)  {
				shaderName = "pos_color";
				shaderSrcPtr = src_flat_pos_color;
			} else {
				assert(false && "only pos+color supported right now");
				// shaderName = "pos_normal";
				// shaderSrcPtr = src_flat_pos_normal;
			}

			assert(shaderSrcPtr != nullptr && "invalid inputs; could not find matching shader");
			assert(shaderName != nullptr && "invalid inputs; could not find matching shader");

			std::string cacheKey = "thickPointEntity | shader " + std::string{shaderName};
			auto it = ao.renderPipelineCache.find(cacheKey);
			if (it == ao.renderPipelineCache.end()) {
				spdlog::get("wg")->info("renderPipelineCache miss for '{}', creating it.", cacheKey);
			} else {
				// spdlog::get("wg")->trace("renderPipelineCache hit for '{}', using it.", cacheKey);
				rpwl = it->second;
				return;
			}

            ShaderModule shader { create_shader(ao.device, shaderSrcPtr, "thickPointShader") };

			std::vector<WGPUVertexAttribute> attributes;
			uint64_t offset=0;
			uint32_t location=0;
			if (pd.havePos) {
				attributes.push_back(WGPUVertexAttribute{
						.format = WGPUVertexFormat_Float32x4,
						.offset         = offset,
						.shaderLocation = location
				});
				offset += sizeof(float) * 4;
				location++;
			}
			if (pd.haveColor) {
				attributes.push_back(WGPUVertexAttribute{
						.format = WGPUVertexFormat_Float32x4,
						.offset         = offset,
						.shaderLocation = location
				});
				offset += sizeof(float) * 4;
				location++;
			}
			if (pd.haveNormal) {
				attributes.push_back(WGPUVertexAttribute{
						.format = WGPUVertexFormat_Float32x3,
						.offset         = offset,
						.shaderLocation = location
				});
				offset += sizeof(float) * 3;
				location++;
			}

            WGPUVertexBufferLayout bufferLayout {
                .arrayStride    = offset,
                .stepMode       = WGPUVertexStepMode_Vertex,
                .attributeCount = attributes.size(),
                .attributes     = attributes.data(),
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


			auto pipelineLayout = ao.device.create(WGPUPipelineLayoutDescriptor {
					.nextInChain = nullptr,
					.label = "thickPoint",
					.bindGroupLayoutCount = 1,
					.bindGroupLayouts = &ao.getSceneBindGroupLayout().ptr
			});

            WGPURenderPipelineDescriptor desc {
                .nextInChain  = nullptr,
                .label        = "thickPoint",
                .layout       = pipelineLayout,
                .vertex       = vertexState,
                .primitive    = primState,
                .depthStencil = ao.surfaceDepthStencilFormat == WGPUTextureFormat_Undefined ? nullptr : &depthStencilState,
                .multisample = multisampleState,
                .fragment    = &fragmentState
            };

            auto pipeline = ao.device.create(desc);

			rpwl = std::make_shared<RenderPipelineWithLayout>();
			rpwl->layout = std::move(pipelineLayout);
			rpwl->pipeline = std::move(pipeline);
			ao.renderPipelineCache[cacheKey] = rpwl;
		}

}


