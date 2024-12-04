#pragma once

#include "webgpuGlobe/app/app.h"
#include "../tiff.h"
#include "../../globe.h"
#include "../../cast.h"

namespace wg {
namespace tiff {


    struct GpuResources {
        Texture sharedTex;
        TextureView sharedTexView;

        Sampler sampler;

        std::vector<int32_t> freeTileInds;

		// ---------------------------------------------------------------------------------------------------
		// Main Pipeline
		// Created in ctor.
		// ---------------------------------------------------------------------------------------------------

		// Used with main pipeline (for rendering tiles with reference textures)
        // "shared" because the same texture is used for all tiles -- by way of array layers / subresources.
        BindGroupLayout sharedBindGroupLayout;
        BindGroup sharedBindGroup;

		RenderPipelineWithLayout mainPipelineAndLayout;
		void createMainPipeline();

		// ---------------------------------------------------------------------------------------------------
		// Cast Pipeline
		// NOT created in constructor because we need a texture view to create it.
		// So the `update` function can be called every frame and will only recreate when necessary
		// (when to-be-casted texture(s) change).
		// ---------------------------------------------------------------------------------------------------

		// Used with cast pipeline (for rendering tiles with projected textures)
		CastGpuResources castGpuResources;
		RenderPipelineWithLayout castPipelineAndLayout;

		void updateCastBindGroupAndResources(const CastUpdate& castUpdate);
		void createCastPipeline();


		AppObjects& ao;

        GpuResources(AppObjects& ao, const GlobeOptions& opts);

        inline int32_t takeTileInd() {
            if (freeTileInds.size() == 0) {
                throw NoTilesAvailableExecption {};
            } else {
                int32_t out = freeTileInds.back();
                freeTileInds.pop_back();
                return out;
            }
        }

        inline void returnTileInd(int32_t ind) {
			freeTileInds.push_back(ind);
			assert(freeTileInds.size() <= MAX_TILES);
		}

    };

}
}
