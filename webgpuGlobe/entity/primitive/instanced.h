#pragma once

#include "../entity.h"

namespace wg {

	// Will cause a new bindgroup/bindgrouplayout to be created and added
	// to the render pipeline.
	// The only support is for the new bindgroup to have one binding, a ubo buffer
	// of arbitrary size
	struct InstancingUpdate {
		int n = 0;
		int uboSizePerItem = 0;
		int uboDataSize = 0;
		void* uboData = 0;
	};

	struct InstancedPrimitiveData {
		int nverts = 0;
		int nindex = 0;

		WGPUPrimitiveTopology topo = WGPUPrimitiveTopology_LineList;
		WGPUCullMode cullMode = WGPUCullMode_Back;

		// const float* pos = 0;     // x3
		// const float* color = 0;   // x4, optional
		// const float* normal = 0;  // x3, optional
		
		const float* vertData = 0;
		const uint32_t* indexData = 0;

		bool havePos = false, haveColor = false, haveNormal = false;
		bool depthWrite = true;

		InstancingUpdate instancingUpdate;

		// bool supportInstancing = false;
	};

	struct InstancedPrimitiveEntity : public Entity {
		size_t vboSize = 0;
		size_t iboSize = 0;
		int nverts = 0;
		int nindex = 0;

        Buffer vbo;
        Buffer ibo;

		struct {
			// BindGroup bindGroup;
			// BindGroupLayout bindGroupLayout;
			Buffer buffer;
			int n = 0;
			int size = 0;
			int eachSize=16*4;
		} instancing;

		std::shared_ptr<RenderPipelineWithLayout> rpwl = nullptr;

        inline InstancedPrimitiveEntity() {}

        virtual void render(const RenderState& rs) override;

        void set(AppObjects& ao, InstancedPrimitiveData primitiveData);

		private:
		void makeOrUploadBuffers_(AppObjects& ao, const InstancedPrimitiveData& pd);
		void makeOrGetPipeline_(AppObjects& ao, const InstancedPrimitiveData& pd);
		void maybeMakeInstancingBgl(AppObjects& ao, const InstancingUpdate& id);

	};

}

