#pragma once

#include "camera.h"


namespace wg {
    struct OrthographicCamera : public InteractiveCamera {

		OrthographicCamera(
				const CameraIntrin& intrin,
				AppObjects& ao,
				const double startingLocation[3],
				const double startingRotation[9]
				);

		virtual SceneCameraData1 lower(const SceneData& sd) override;
		virtual void step(const SceneData& sd) override;

		double p[3];
		double q[4];
	};
}
