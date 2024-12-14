#pragma once

#include "webgpuGlobe/app/app.h"
#include "webgpuGlobe/app/wrappers.hpp"
#include "webgpuGlobe/camera/camera.h"

namespace wg {




    // A cheap reference type that can track local transformations down some render tree.
    struct RenderState {
        const SceneCameraData1& camData;
		const CameraIntrin& intrin;
        CommandEncoder& cmdEncoder;
        RenderPassEncoder& pass;
        AppObjects& appObjects;
        FrameData& frameData;

        RenderState(const RenderState& o)            = delete;
        RenderState& operator=(const RenderState& o) = delete;

        inline RenderState cloneWithCam(SceneCameraData1& newCamData) {
            return RenderState {
                .camData    = newCamData,
                .intrin     = intrin,
                .cmdEncoder = cmdEncoder,
                .pass       = pass,
                .appObjects = appObjects,
                .frameData  = frameData,
            };
        }
    };

    struct Renderable {
        inline virtual ~Renderable() {
        }

        virtual void render(const RenderState& rs) = 0;

		// virtual void buildPipeline(Pass& pass);

    };

}
