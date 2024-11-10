#pragma once

#include "app/app.h"
#include "../tiff.h"
#include "../../globe.h"

namespace wg {
namespace tiff {


#error "use gpucastdata -- we actually must store the texture ourself privately."

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
        BindGroupLayout castBindGroupLayout;
        BindGroup castBindGroup;

		RenderPipelineWithLayout castPipelineAndLayout;
		Buffer castMvpBuf;
		// size_t castMvpBufSize = roundUp<256>(4*4*sizeof(float));
		size_t castMvpBufSize = 0;

		int lastCastTexW = 0;
		int lastCastTexH = 0;
		WGPUTextureFormat lastCastTexFmt = WGPUTextureFormat_Undefined;
		void updateCastPipeline(int texw, int texh, WGPUTextureFormat texFmt, TextureView texView, const float* castMvp=0);


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
