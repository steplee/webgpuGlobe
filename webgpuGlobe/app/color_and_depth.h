#include "wrappers.hpp"
#include "../camera/camera.h"

#include <Eigen/Core>
#include <opencv2/core.hpp>

namespace wg {

	struct ColorAndDepthInfo {
		Buffer colorBuf={}, depthBuf={};

		cv::Mat color;
		cv::Mat depth;

		CameraIntrin intrin = CameraIntrin(0,0,0);
		float imvp[16];

		void queueRead(AppObjects& ao, const float *imvp, const CameraIntrin& intrin, Texture& colorTex, Texture& depthTex, CommandEncoder& ce);
		void mapAndCopyToMat(AppObjects& ao);

		float accessDepth(int x, int y) const;
		Eigen::Vector3f accessUnitEcefPoint(int x, int y) const;
	};

}
