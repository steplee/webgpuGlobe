#include "primitive.h"
#include "shaders.hpp"
#include "webgpuGlobe/app/shader.h"

namespace wg {

        void PrimitiveEntity::render(const RenderState& rs) {
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

        void PrimitiveEntity::set(AppObjects& ao, PrimitiveData primData) {
			makeOrGetPipeline_(ao, primData);
			makeOrUploadBuffers_(ao, primData);
		}


		void PrimitiveEntity::makeOrUploadBuffers_(AppObjects& ao, const PrimitiveData& pd) {
			int width = 0;
			if (pd.havePos) width += 3;
			if (pd.haveColor) width += 4;
			if (pd.haveNormal) width += 3;

			size_t newVboSize = sizeof(float) * width * pd.nverts;
			size_t newIboSize = sizeof(uint32_t) * pd.nindex;
			if (pd.nindex <= 0) iboSize = 0;

			// spdlog::get("wg")->info("new {} {}", newVboSize, newIboSize);

			if (newVboSize) {
				assert(pd.vertData != nullptr);

				auto vboCapacity = vbo ? vbo.getSize() : 0;

				if (newVboSize > vboCapacity) {
					spdlog::get("wg")->trace("need new vbo ({} > {})", newVboSize, vboCapacity);
					WGPUBufferDescriptor desc {
						.nextInChain      = nullptr,
						.label            = "primitiveVbo",
						.usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex,
						.size             = newVboSize,
						.mappedAtCreation = true,
					};
					vbo = ao.device.create(desc);
					void* dst = wgpuBufferGetMappedRange(vbo, 0, newVboSize);
					memcpy(dst, pd.vertData, newVboSize);
					wgpuBufferUnmap(vbo);
				} else {
					// Re-use it.
					// spdlog::get("wg")->debug("reuse vbo ({} <= {})", newVboSize, vboCapacity);
					ao.queue.writeBuffer(vbo, 0, pd.vertData, newVboSize);
				}

				vboSize = newVboSize;
			} else {
				vboSize = 0;
			}

			if (newIboSize) {
				if (newIboSize > iboSize) {
					spdlog::get("wg")->trace("need new ibo ({} > {})", newIboSize, iboSize);
					WGPUBufferDescriptor desc {
						.nextInChain      = nullptr,
						.label            = "primitiveIbo",
						.usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index,
						.size             = newIboSize,
						.mappedAtCreation = true,
					};
					ibo = ao.device.create(desc);
					void* dst = wgpuBufferGetMappedRange(ibo, 0, newIboSize);
					memcpy(dst, pd.indexData, newIboSize);
					wgpuBufferUnmap(ibo);
				} else {
					// Re-use it.
					// spdlog::get("wg")->debug("reuse ibo ({} <= {})", newIboSize, iboSize);
					ao.queue.writeBuffer(ibo, 0, pd.indexData, newIboSize);
				}

				iboSize = newIboSize;
			} else {
				iboSize = 0;
			}


			nverts = pd.nverts;
			nindex = pd.nindex;
		}

		void PrimitiveEntity::makeOrGetPipeline_(AppObjects& ao, const PrimitiveData& pd) {
			const char* shaderName = nullptr;
			const char* shaderSrcPtr = nullptr;
			const char* topoName = nullptr;

			if (pd.topo == WGPUPrimitiveTopology_TriangleList) topoName = "triList";
			if (pd.topo == WGPUPrimitiveTopology_TriangleStrip) topoName = "triStrip";
			if (pd.topo == WGPUPrimitiveTopology_LineList) topoName = "lineList";
			if (pd.topo == WGPUPrimitiveTopology_LineStrip) topoName = "lineStrip";
			if (pd.topo == WGPUPrimitiveTopology_PointList) topoName = "pointList";

			if (!pd.havePos) {
				assert(false and "must provide position");
			}

			if (!pd.haveColor and !pd.haveNormal) {
				shaderName = "pos";
				shaderSrcPtr = src_flat_pos;
			} else if (!pd.haveNormal)  {
				shaderName = "pos_color";
				shaderSrcPtr = src_flat_pos_color;
			} else {
				// shaderName = "pos_normal";
				// shaderSrcPtr = src_flat_pos_normal;
			}

			assert(shaderSrcPtr != nullptr && "invalid inputs; could not find matching shader");
			assert(shaderName != nullptr && "invalid inputs; could not find matching shader");
			assert(topoName != nullptr && "invalid inputs; could name topo");

			std::string cacheKey = "primEntity | shader " + std::string{shaderName} + " | topo " + topoName;
			auto it = ao.renderPipelineCache.find(cacheKey);
			if (it == ao.renderPipelineCache.end()) {
				spdlog::get("wg")->info("renderPipelineCache miss for '{}', creating it.", cacheKey);
			} else {
				// spdlog::get("wg")->trace("renderPipelineCache hit for '{}', using it.", cacheKey);
				rpwl = it->second;
				return;
			}

            ShaderModule shader { create_shader(ao.device, shaderSrcPtr, "primitiveShader") };

			std::vector<WGPUVertexAttribute> attributes;
			uint64_t offset=0;
			uint32_t location=0;
			if (pd.havePos) {
				attributes.push_back(WGPUVertexAttribute{
						.format = WGPUVertexFormat_Float32x3,
						.offset         = offset,
						.shaderLocation = location
				});
				offset += sizeof(float) * 3;
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
                .topology         = pd.topo,
                // .stripIndexFormat = WGPUIndexFormat_Undefined,
                .stripIndexFormat = (pd.topo == WGPUPrimitiveTopology_TriangleStrip || pd.topo == WGPUPrimitiveTopology_LineStrip) ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Undefined,
                .frontFace        = WGPUFrontFace_CW,
                .cullMode         = pd.cullMode,
            };

            WGPUDepthStencilState depthStencilState {
                .nextInChain         = nullptr,
                .format              = ao.surfaceDepthStencilFormat,
                .depthWriteEnabled   = pd.depthWrite,
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
					.label = "primitive",
					.bindGroupLayoutCount = 1,
					.bindGroupLayouts = &ao.getSceneBindGroupLayout().ptr
			});

            WGPURenderPipelineDescriptor desc {
                .nextInChain  = nullptr,
                .label        = "primitive",
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
