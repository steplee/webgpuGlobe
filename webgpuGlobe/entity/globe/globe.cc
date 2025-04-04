#include "globe.h"

#include <sys/stat.h>

#include "util/fmtEigen.h"

namespace wg {

		UnpackedOrientedBoundingBox::UnpackedOrientedBoundingBox(const PackedOrientedBoundingBox& pobb) : packed(pobb) {

			Matrix<float, 8, 3> pts0; pts0 <<
				-1, -1, -1,
				 1, -1, -1,
				 1,  1, -1,
				-1,  1, -1,
				-1, -1,  1,
				 1, -1,  1,
				 1,  1,  1,
				-1,  1,  1;

			Affine3f T;
			T.fromPositionOrientationScale(packed.p(), packed.q().toRotationMatrix(), packed.extents());

			pts = (T * pts0.transpose()).transpose();
		}

		static float sdBox(const Vector3f& eye, const Vector3f& extents) {
			Vector3f q = eye.cwiseAbs() - extents;
			return q.array().max(Array3f::Zero()).matrix().norm() + std::min(0.f, q.maxCoeff());
		}

		static float sdBox_obb(const Vector3f& eye, const Vector3f& extents, const Vector3f& ctr, const Quaternionf& q) {
			Vector3f eye1 = q.conjugate() * (eye - ctr);
			return sdBox(eye1, extents);
		}
	
        float UnpackedOrientedBoundingBox::computeSse(const Matrix4f& mvp, const Vector3f& eye, float tanHalfFovTimesHeight) {

			float exteriorDistance = sdBox_obb(eye, packed.extents(), packed.p(), packed.q());

            // spdlog::get("wg")->debug("computeSse exteriorDistance: {}", exteriorDistance);

			if (exteriorDistance <= 0) {
				spdlog::get("wg")->debug("computeSse INSIDE.");
				return kBoundingBoxContainsEye;
			}

			{


				if (0)
				{
					// Give the inigo quilezles article a read: https://iquilezles.org/articles/frustumcorrect/
					// But I don't do what he does with checking the dot product of the 6 planes in world space.
					// Rather I project the bbox points into the camera frustum and check the scalar coordinates. Simpler IMO.
					// So this is sort of transposed to what he does, in two ways (the coordinate space and the logical check/loop).
					Array<float,8,4> ndcPoints1 = (pts.rowwise().homogeneous() * mvp.transpose()).array();
					Array<float,8,3> ndcPoints  = ndcPoints1.rowwise().hnormalized();


					// spdlog::get("wg")->debug("computeSse pts1\n{}", ndcPoints1);
					// spdlog::get("wg")->debug("computeSse pts\n{}", ndcPoints);
					// for (int i=0; i<8; i++) if (ndcPoints1(i,3) < 0) ndcPoints.row(i) << 2,2,2;


					if (
							(ndcPoints.col(0) < -1 or ndcPoints1.col(3) <= 0).all() or
							(ndcPoints.col(1) < -1 or ndcPoints1.col(3) <= 0).all() or
							(ndcPoints.col(2) <  0 or ndcPoints1.col(3) <= 0).all() or
							(ndcPoints.col(0) >  1 or ndcPoints1.col(3) <= 0).all() or
							(ndcPoints.col(1) >  1 or ndcPoints1.col(3) <= 0).all() or
							(ndcPoints.col(2) >  1 or ndcPoints1.col(3) <= 0).all()    ) {
						spdlog::get("wg")->debug("computeSse OUTSIDE FRUSTUM (check 1.5).");
						return kBoundingBoxNotVisible;
					}

					/*
					// WARNING: This is wrong, when some points lie BEHIND camera, it causes check to fail.
					if (
							(ndcPoints.col(0) < -1).all() or
							(ndcPoints.col(1) < -1).all() or
							(ndcPoints.col(2) <  0).all() or
							(ndcPoints.col(0) >  1).all() or
							(ndcPoints.col(1) >  1).all() or
							(ndcPoints.col(2) >  1).all()    ) {
						spdlog::get("wg")->debug("computeSse OUTSIDE FRUSTUM (check 1).");
						return kBoundingBoxNotVisible;
					}

					// Second check, following inigo quilezles's trick.
					// Again logic is simpler because of the frame being the camera frame.
					// Honestly I understand his diagram but did not take the time to really understand how the logic works.
					// WARNING: Test this actually works... Does my change of frame fix the issue that this is supposed to?
					//          Must this mask based on behind camera as well?
					if (
							(-1 > ndcPoints.col(0).maxCoeff()) or
							( 1 < ndcPoints.col(0).minCoeff()) or
							(-1 > ndcPoints.col(1).maxCoeff()) or
							( 1 < ndcPoints.col(1).minCoeff()) or
							( 0 > ndcPoints.col(2).maxCoeff()) or
							( 1 < ndcPoints.col(2).minCoeff())    ) {
						spdlog::get("wg")->debug("computeSse OUTSIDE FRUSTUM (check 2).");
						return kBoundingBoxNotVisible;
					}
					*/
				} else {

					Array<float,8,4> ndcPoints1 = (pts.rowwise().homogeneous() * mvp.transpose()).array();
					Array<float,8,3> ndcPoints  = ndcPoints1.topLeftCorner<8,3>();
					for (int i=0; i<8; i++) ndcPoints.row(i) /= std::max(ndcPoints1(i,3), .0000001f);
					if (
							(ndcPoints.col(0).array() >  1.f).all()
							or (ndcPoints.col(0).array() < -1.f).all()
							or (ndcPoints.col(1).array() >  1.f).all()
							or (ndcPoints.col(1).array() < -1.f).all()
							or (ndcPoints.col(2).array() <  0.f).all()
							or (ndcPoints.col(2).array() >  3.f).all()
							) return kBoundingBoxNotVisible;


							}
			}

			float sse = packed.geoError * tanHalfFovTimesHeight / exteriorDistance;
            // spdlog::get("wg")->debug("computeSse sse: {} from geoError {}, thfth {}, ed {}", sse, (float)packed.geoError, tanHalfFovTimesHeight, exteriorDistance);

			// assert(false);
			return sse;
		}

		Globe::Globe(AppObjects& ao, const GlobeOptions& opts) : ao(ao), opts(opts) {
		}

        Globe::~Globe() {
		}

}
