#pragma once

#include "camera.h"

namespace wg {
    struct GlobeCamera : public InteractiveCamera {

		// using InteractiveCamera::InteractiveCamera;
		GlobeCamera(
				const CameraIntrin& intrin,
				AppObjects& ao,
				const double startingLocationEcef[3]);

		virtual SceneCameraData1 lower(const SceneData& sd) override;
		virtual void step(const SceneData& sd) override;

		void stepWithTarget(const SceneData& sd, float dt, const double accInWorld[3]);

		void setTarget(bool activate, const double* p, const double* q);
		void clearTarget();
		void resetTargetOffset();
		
        double p[3] = { 0, -8e6, 0 };
        double q[4] = { 0, 0, 0, 1 };

        double v[3] = { 0, 0, 0 };
        double a[3] = { 0, 0, 0 }; // angular vel

		double mouseDxySmooth[2] = {0,0};

		bool dynamicNearFarPlane = true;

		bool haveTarget = false;
		// set with `setTarget`. We'll follow this pose with the offset defined by `target_offset_*`
		double target_p[3] = {0};
		double target_q[4] = {0,0,0,1};
		// User IO will alter `target_offset_*`
		double target_offset_p[3] = {0};
		double target_offset_q[4] = {0,0,0,1};
		// These are just internal buffers that will drive the final output `p`/`q`. They constant move towards `target_*`
		double follow_p[3] = {0};
		double follow_q[4] = {0,0,0,1};

		// Default: 200m elevated from target.
		double target_offset_p_default[3] = {0,0,200/6.371e6};
		double target_offset_q_default[4] = {0,0,0,1};

		// Pretty important params that control how `follow_p` converges to `target_p`.
		// May need to be increased depending on speed/angular-velocity of target.
		// NOTE: `follow_*` are blended with `dt`, not `exp(-dt/lambda)` as would be better.
		//       Specifically, blending with `dt` makes the numbers framerate dependent.
		//       TODO: Consider using `exp(-dt/lambda)`.
		double alpha_p = .8;
		double alpha_q = .2;

		// May be set in IoListener overrides or in step() from the IoListenerWithState members. Used in step()
		// double acc[3];
		// double aacc[3];

    };
}
