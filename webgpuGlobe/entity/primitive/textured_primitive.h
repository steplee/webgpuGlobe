#pragma once

#include "../entity.h"
#include "webgpuGlobe/util/image.h"

namespace wg {

	struct TexturedPrimitiveData {
		int nverts = 0;
		int nindex = 0;

		WGPUPrimitiveTopology topo = WGPUPrimitiveTopology_TriangleList;
		WGPUCullMode cullMode = WGPUCullMode_None;

		const float* vertData = 0;
		const uint32_t* indexData = 0;

		ImagePtr imgPtr;

		bool havePos = false, haveUv = false, haveColor = false, haveNormal = false;
		bool depthWrite = true;
		bool blendAdd = false;
	};

	struct TexturedPrimitiveEntity : public Entity {
		size_t vboSize = 0;
		size_t iboSize = 0;
		int nverts = 0;
		int nindex = 0;

        Buffer vbo;
        Buffer ibo;

		Sampler sampler;
		Texture tex;
		TextureView texView;
		BindGroup texBindGroup;
		BindGroupLayout texBindGroupLayout;
		int lastTexW, lastTexH;

		std::shared_ptr<RenderPipelineWithLayout> rpwl = nullptr;

        inline TexturedPrimitiveEntity() {}

        virtual void render(const RenderState& rs) override;

        void set(AppObjects& ao, TexturedPrimitiveData primitiveData);

		private:
		void makeOrUploadBuffers_(AppObjects& ao, const TexturedPrimitiveData& pd);
		void makeOrGetPipeline_(AppObjects& ao, const TexturedPrimitiveData& pd);
		void makeOrUploadTexture_(AppObjects& ao, const ImagePtr& imgPtr);

	};

}

