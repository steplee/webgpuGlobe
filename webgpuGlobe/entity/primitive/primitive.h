#pragma once

#include "../entity.h"

namespace wg {

	struct PrimitiveData {
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
	};

	struct PrimitiveEntity : public Entity {
		size_t vboSize = 0;
		size_t iboSize = 0;
		int nverts = 0;
		int nindex = 0;

        Buffer vbo;
        Buffer ibo;

		std::shared_ptr<RenderPipelineWithLayout> rpwl = nullptr;

        inline PrimitiveEntity() {}

        virtual void render(const RenderState& rs) override;

        void set(AppObjects& ao, PrimitiveData primitiveData);

		private:
		void makeOrUploadBuffers_(AppObjects& ao, const PrimitiveData& pd);
		void makeOrGetPipeline_(AppObjects& ao, const PrimitiveData& pd);

	};

}
