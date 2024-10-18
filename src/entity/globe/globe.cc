
#include "globe.h"

namespace wg {

		UnpackedOrientedBoundingBox::UnpackedOrientedBoundingBox(const PackedOrientedBoundingBox& pobb) {
			assert(false);
		}
	
        float UnpackedOrientedBoundingBox::sse(const Matrix4f& mvp, const Vector3f& eye) {
			assert(false);
			return 0;
		}

		Globe::Globe(AppObjects& ao, const GlobeOptions& opts) : ao(ao), opts(opts) {
		}

        Globe::~Globe() {
		}
}
