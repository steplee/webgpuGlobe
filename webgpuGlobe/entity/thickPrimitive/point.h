#pragma once

#include "../entity.h"

//
// WebGPU does not support lines or points with thickness, like say OpenGL.
//
// This class allows creating just that: lines/points with thickness.
// Only instead of specifying a thickness per draw call, the thickness is specified per vertex.
//
// The vertex positions are now float32 x4, with the `w` coordinate being thickness in pixels.
//

namespace wg {

	struct ThickPointData {
		int nverts = 0;
		
		const float* vertData = 0;

		bool havePos = false, haveColor = false, haveNormal = false;
	};

	struct ThickPointEntity : public Entity {
		size_t vboSize = 0;
		size_t iboSize = 0;
		int nverts = 0;
		int nindex = 0;

        Buffer vbo;
        Buffer ibo;

		std::shared_ptr<RenderPipelineWithLayout> rpwl = nullptr;

        inline ThickPointEntity() {}

        virtual void render(const RenderState& rs) override;

        void set(AppObjects& ao, ThickPointData primitiveData);
        void reset();

		private:
		void makeOrUploadBuffers_(AppObjects& ao, const ThickPointData& pd);
		void makeOrGetPipeline_(AppObjects& ao, const ThickPointData& pd);

	};

}


