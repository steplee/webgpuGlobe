#include "webgpuGlobe/app/app.h"
#include "globe.h"

namespace wg {

	struct Fog {

		Fog(AppObjects& ao, const GlobeOptions& gopts, const AppOptions& appOpts);

		void beginPass(CommandEncoder& ce);
		void endPass();
		void renderAfterEndingPass(RenderState& rs);

		// Attachments.
		Texture depthTexture;
		Texture colorTexture;
		TextureView colorTexView;
		TextureView depthTexView;
		Sampler sampler;

		BindGroupLayout bindGroupLayout;
		BindGroup bindGroup;

		RenderPassEncoder rpe;
		AppObjects& ao;

		// Render to quad (final thing)
		RenderPipelineWithLayout quadPipelineAndLayout;

	};


}
