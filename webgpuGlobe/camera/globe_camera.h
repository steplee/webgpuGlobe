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

		void setTarget(const double* p, const double* q);
		void clearTarget();
		void resetTargetOffset();
		
        double p[3] = { 0, -8e6, 0 };
        double q[4] = { 0, 0, 0, 1 };

        double v[3] = { 0, 0, 0 };
        double a[3] = { 0, 0, 0 }; // angular vel

		double mouseDxySmooth[2] = {0,0};

		bool haveTarget = false;
		double target_p[3] = {0};
		double target_q[4] = {0,0,0,1};
		double target_offset_p[3] = {0};
		double target_offset_q[4] = {0,0,0,1};

		// May be set in IoListener overrides or in step() from the IoListenerWithState members. Used in step()
		// double acc[3];
		// double aacc[3];

    };
}
