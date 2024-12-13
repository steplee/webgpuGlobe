#include "webgpuGlobe/app/app.h"
#include "../globe/globe.h"

namespace wg {
	
	struct __attribute__((packed)) DeferredCastData {
		float castMvp[16] = {0};
		float castColor[4] = {1};
	};

	struct DeferredCast {

		DeferredCast(AppObjects& ao, const GlobeOptions& gopts, const AppOptions& appOpts);

		void beginPass(CommandEncoder& ce, int w, int h);
		void endPass();
		void renderAfterEndingPass(RenderState& rs);

		// Fbo Attachments.
		Texture depthTexture;
		Texture colorTexture;
		TextureView colorTexView;
		TextureView depthTexView;
		Sampler sampler;

		BindGroupLayout sceneFboBindGroupLayout;
		BindGroup sceneFboBindGroup;

		Texture castTexture;
		TextureView castTextureView;
		BindGroupLayout castTexBindGroupLayout;
		BindGroup castTexBindGroup;
		DeferredCastData deferredCastDataCpu;
		Buffer deferredCastDataGpu;

		RenderPassEncoder rpe;
		AppObjects& ao;

		// Render to quad (final thing)
		RenderPipelineWithLayout quadPipelineAndLayout;

		void createSceneFboTextures(AppObjects& ao, int w, int h);
		float fboW = -1;
		float fboH = -1;

		// TODO: Allow caching multiple in case user calls multiple times?
		void setCastTexture(const uint8_t* data, uint32_t w, uint32_t h, uint32_t c);
		void setCastData(const DeferredCastData& d);
		int castBufSize = 0;
		int castTexW = -1;
		int castTexH = -1;

		/*
		 Would be nice, but will require a layer of abstraction for surface and custom passes
		struct Guard {
			DeferredCast* ptr = nullptr;
			inline Guard(DeferredCast& dc, CommandEncoder& ce, int fboW, int fboH) : ptr(&dc) {
				ptr->beginPass(ce, fboW, fboH);
			}
			inline ~Guard()  {
				ptr->endPass();
				ptr->renderAfterEndingPass();
			}

		};
		*/

	};


}

