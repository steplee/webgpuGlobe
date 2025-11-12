#include "instanced.h"
#include "instanced_shaders.hpp"
#include "webgpuGlobe/app/shader.h"

// Warning: This code is AWFUL.
// UBO stuff is not actually ubos...

namespace wg {

        void InstancedPrimitiveEntity::render(const RenderState& rs) {
			if (nverts == 0) return;

			assert(instancing.eachSize == 16*4);

            assert(rpwl);
            rs.pass.setRenderPipeline(rpwl->pipeline);

            rs.pass.setBindGroup(0, rs.appObjects.getSceneBindGroup());
			// rs.pass.setBindGroup(1, instancing.bindGroup);

            rs.pass.setVertexBuffer(0, vbo, 0, vboSize);
            rs.pass.setVertexBuffer(1, instancing.buffer, 0, instancing.size);
			if (iboSize) {
				rs.pass.setIndexBuffer(ibo, WGPUIndexFormat_Uint32, 0, iboSize);
				rs.pass.drawIndexed(nindex, instancing.n);
			} else {
				rs.pass.draw(nverts, instancing.n);
			}
        }

        void InstancedPrimitiveEntity::set(AppObjects& ao, InstancedPrimitiveData primData) {
			maybeMakeInstancingBgl(ao,primData.instancingUpdate);
			makeOrGetPipeline_(ao, primData);
			makeOrUploadBuffers_(ao, primData);
		}


		void InstancedPrimitiveEntity::makeOrUploadBuffers_(AppObjects& ao, const InstancedPrimitiveData& pd) {
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

		void InstancedPrimitiveEntity::makeOrGetPipeline_(AppObjects& ao, const InstancedPrimitiveData& pd) {
			const char* shaderName = nullptr;
			const char* shaderSrcPtr = nullptr;
			const char* topoName = nullptr;

			auto& id = pd.instancingUpdate;
			instancing.n = id.n;

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

			std::string cacheKey = "instancedPrimEntity | shader " + std::string{shaderName} + " | topo " + topoName;
			auto it = ao.renderPipelineCache.find(cacheKey);
			if (it == ao.renderPipelineCache.end()) {
				spdlog::get("wg")->info("renderPipelineCache miss for '{}', creating it.", cacheKey);
			} else {
				rpwl = it->second;
				spdlog::get("wg")->trace("renderPipelineCache hit for '{}', using it ({} {}).", cacheKey, (void*)it->second.get(), (void*)rpwl->pipeline.ptr);
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

			std::vector<WGPUVertexAttribute> iattributes = {
				WGPUVertexAttribute {
						.format = WGPUVertexFormat_Float32x4,
						.offset         = 0,
						.shaderLocation = 5
				},
				WGPUVertexAttribute {
						.format = WGPUVertexFormat_Float32x4,
						.offset         = 4*4*1,
						.shaderLocation = 6
				},
				WGPUVertexAttribute {
						.format = WGPUVertexFormat_Float32x4,
						.offset         = 4*4*2,
						.shaderLocation = 7
				},
				WGPUVertexAttribute {
						.format = WGPUVertexFormat_Float32x4,
						.offset         = 4*4*3,
						.shaderLocation = 8
				},
			};

            WGPUVertexBufferLayout bufferLayouts[2] = {
				{
                .arrayStride    = offset,
                .stepMode       = WGPUVertexStepMode_Vertex,
                // .stepMode       = WGPUVertexStepMode_Instance,
                .attributeCount = attributes.size(),
                .attributes     = attributes.data(),
				},
				{
                .arrayStride    = (uint64_t)instancing.eachSize,
                // .stepMode       = WGPUVertexStepMode_Vertex,
                .stepMode       = WGPUVertexStepMode_Instance,
                .attributeCount = iattributes.size(),
                .attributes     = iattributes.data(),
				}
            };

            WGPUVertexState vertexState { .nextInChain   = nullptr,
                                          .module        = shader,
                                          .entryPoint    = "vs_main",
                                          .constantCount = 0,
                                          .constants     = nullptr,
                                          .bufferCount   = 2,
                                          .buffers       = bufferLayouts };
			
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


			PipelineLayout pipelineLayout;

			WGPUBindGroupLayout bgls[] = {
				ao.getSceneBindGroupLayout().ptr,
				// instancing.bindGroupLayout.ptr,
			};
			pipelineLayout = ao.device.create(WGPUPipelineLayoutDescriptor {
					.nextInChain = nullptr,
					.label = "primitiveWithInstancing",
					// .bindGroupLayoutCount = 2,
					.bindGroupLayoutCount = 1,
					.bindGroupLayouts = bgls
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
			spdlog::get("wg")->trace("created '{}', ({} {}).", cacheKey, (void*)rpwl.get(), (void*)rpwl->pipeline.ptr);
			ao.renderPipelineCache[cacheKey] = rpwl;
		}



		void InstancedPrimitiveEntity::maybeMakeInstancingBgl(AppObjects& ao, const InstancingUpdate& id) {
			if (instancing.buffer.ptr == nullptr or instancing.buffer.getSize() != id.uboDataSize) {
				instancing.buffer = ao.device.create(
						WGPUBufferDescriptor {
						.nextInChain      = nullptr,
						.label            = "InstancingUbo",
						// .usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform,
						.usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex,
						.size             = (uint64_t)id.uboDataSize,
						// .mappedAtCreation = true,
						.mappedAtCreation = false,
						});

			}
			ao.queue.writeBuffer(instancing.buffer, 0, id.uboData, id.uboDataSize);
			instancing.size = id.uboDataSize;


			/*
			if (instancing.bindGroup.ptr == nullptr) {

				WGPUBindGroupLayoutEntry entries[1] = {
					{
						.nextInChain = nullptr,
						.binding = 0,
						.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
						.buffer =
							WGPUBufferBindingLayout{.nextInChain = nullptr,
													.type = WGPUBufferBindingType_Uniform,  .hasDynamicOffset=false, .minBindingSize=0},
						.sampler = {.nextInChain = nullptr,
									.type = WGPUSamplerBindingType_Undefined},
						.texture = {.nextInChain = nullptr,
									.sampleType = WGPUTextureSampleType_Undefined, },
						.storageTexture = {.nextInChain = nullptr, .access = WGPUStorageTextureAccess_Undefined},
					},
				};
				instancing.bindGroupLayout = ao.device.create(
					WGPUBindGroupLayoutDescriptor{.nextInChain = nullptr,
													.label = "PrimInstancingBgl",
													.entryCount = 1,
													.entries = entries});



    WGPUBindGroupEntry groupEntries[1] = {
        {.nextInChain = nullptr,
         .binding = 0,
         .buffer = instancing.buffer,
         .offset = 0,
         .size = instancing.buffer.getSize(),
         .sampler = nullptr,
         .textureView = 0},
    };

    instancing.bindGroup =
        ao.device.create(WGPUBindGroupDescriptor{.nextInChain = nullptr,
                                                 .label = "PrimInstancingBg",
                                                 .layout = instancing.bindGroupLayout,
                                                 .entryCount = 1,
                                                 .entries = groupEntries});


							}

							*/
		}




}

