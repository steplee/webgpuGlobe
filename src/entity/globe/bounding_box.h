#pragma once

#include <Eigen/Core>
#include <Eigen/Geometry>

namespace wg {

    using namespace Eigen;

    struct __attribute__((packed)) PackedOrientedBoundingBox {
		float p_[3];
		float q_[4];
		float extents_[3];
        float geoError;

		inline PackedOrientedBoundingBox() {}
		inline PackedOrientedBoundingBox(
				Ref<const Vector3f> pp,
				const Quaternionf& qq,
				Ref<const Vector3f> ee,
				float geoe
				) {
			p() = pp;
			q() = qq;
			extents() = ee;
			geoError = geoe;
		}

		inline Map<Vector3f> p() { return Map<Vector3f>{p_}; }
		inline Map<Quaternionf> q() { return Map<Quaternionf>{q_}; }
		inline Map<Vector3f> extents() { return Map<Vector3f>{extents_}; }
    };

	// For information about geometric and screen-space error, see the 3d tiles spec.
	// Even better they have a PDF somewhere giving a good intuition for it, as
	// well as it's definition in terms of object distance and camera intrinsics.
	// IIRC, geometric error is the error induced by not expanding a tile's children, or
	// put another way: by rendering a parent instead of all of it's children.
	//
	// Screen space error is that value (originally has units meters) transformed to make
	// it have units in pixels, which is what the actual open/close thresholds shall be
	// specified in.
	//
	// In this implementation, screen space error will be computed using the exterior
	// distance from the eye to the closest point of the bounding box for a tile.
	//
	// Let me write a little bit about sse to remind myself how it goes.
	// As we get closer to an object, the screen space error will grow.
	// So SSE will be inversely proportional to distance-from-eye.
	// When we are inside a bounding box it shall infinitely high.
	//

	// Special value returned by `computeSse` indicating that the bounding box failed
	// the frustum culling check (i.e. is not visible in scene).
	constexpr float kBoundingBoxNotVisible = -2.f;
	constexpr float kBoundingBoxContainsEye = -3.f; // We are inside bounding box, SSE would be infinite.


	// A concrete type, shared amongst all globe implementations.
    struct UnpackedOrientedBoundingBox {

        inline UnpackedOrientedBoundingBox() : terminal(false), root(false) {}
        UnpackedOrientedBoundingBox(const UnpackedOrientedBoundingBox&)            = default;
        UnpackedOrientedBoundingBox(UnpackedOrientedBoundingBox&&)                 = default;
        UnpackedOrientedBoundingBox& operator=(const UnpackedOrientedBoundingBox&) = default;
        UnpackedOrientedBoundingBox& operator=(UnpackedOrientedBoundingBox&&)      = default;

        UnpackedOrientedBoundingBox(const PackedOrientedBoundingBox& pobb);

        Matrix<float, 8, 3> pts;
		PackedOrientedBoundingBox packed;
        // float geoError;

        // Extra information:
        // This is sort of an ugly design, but it is efficient and fits perfectly.
        bool terminal : 1;
        bool root : 1;

        // Compute screen space error, while also doing frustum cull check.
        float computeSse(const Matrix4f& mvp, const Vector3f& eye, float tanHalfFovTimesHeight);
    };

}
