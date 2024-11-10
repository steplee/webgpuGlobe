#pragma once

#include "app/app.h"
#include "../tiff.h"
#include "../../globe.h"

namespace wg {
namespace tiff {

    struct GpuResources {
        Texture sharedTex;
        TextureView sharedTexView;

        Sampler sampler;

        std::vector<int32_t> freeTileInds;

        // "shared" because the same texture is used for all tiles -- by way of array layers / subresources.
        BindGroupLayout sharedBindGroupLayout;
        BindGroup sharedBindGroup;

        PipelineLayout pipelineLayout;
        RenderPipeline renderPipeline;

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
