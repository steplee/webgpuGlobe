#include "globe_camera.h"
#include <Eigen/Core>
#include <Eigen/Geometry>

#include <spdlog/fmt/ostr.h>
#include "util/fmtEigen.h"
#include "geo/conversions.h"

namespace wg {

	static constexpr float dragTimeConstant = .08;

    using namespace Eigen;

    using RowMatrix4f = Eigen::Matrix<float, 4, 4, RowMajor>;
    using RowMatrix3f = Eigen::Matrix<float, 3, 3, RowMajor>;
    using RowMatrix3d = Eigen::Matrix<double, 3, 3, RowMajor>;

	// untested.
    static Matrix3d getLtp(const Vector3d& eye) {
		Vector3d f = eye.normalized();
		Vector3d r = f.cross(Vector3d::UnitZ()).normalized();
		Vector3d u = f.cross(r).normalized();
		Matrix3d out;
		out.col(2) = f;
		out.col(1) = u;
		out.col(0) = r;
		return out;
	};

	// untested.
    static Matrix3d lookAtR(const Vector3d& target, const Vector3d& eye, const Vector3d& up0) {
		Matrix3d out;
		Vector3d f = (target - eye).normalized();
		Vector3d r = f.cross(up0).normalized();
		Vector3d u = f.cross(r).normalized();
		out.col(2) = f;
		out.col(1) = u;
		out.col(0) = r;
		return out;
	}

    SceneCameraData1 GlobeCamera::lower(const SceneData& sd) {
        // spdlog::get("wg")->info("GlobeCamera::lower");

        SceneCameraData1 out;

        Map<Matrix4f> mvp(out.mvp);
        Map<Matrix4f> imvp(out.imvp);
        Map<Matrix4f> mv(out.mv);
        Map<Vector3f> eye(out.eye);
        Map<Vector4f> colorMult(out.colorMult);
        Map<Vector2f> wh(out.wh);
        Map<Vector4f> sun(out.sun);
        float& haeAlt = out.haeAlt;
        float& haze = out.haze;
        float& time = out.time;
        float& dt   = out.dt;

        Map<const Vector3d> eye_(this->p);
        Map<const Quaterniond> q_(this->q);
        // spdlog::get("wg")->info("eye: {}", eye_.transpose());
        // spdlog::get("wg")->info("R:\n{}", q_.toRotationMatrix());
        // spdlog::get("wg")->info("R'R:\n{}", q_.toRotationMatrix()*q_.toRotationMatrix().transpose());


        Matrix4d mv_;
		mv_.row(3) << 0,0,0,1;
		mv_.topLeftCorner<3,3>() = q_.toRotationMatrix().transpose();
		mv_.topRightCorner<3,1>() = -(q_.conjugate() * eye_);

        Matrix4f proj_;
		intrin.proj(proj_.data());

        // mvp.setIdentity();
        // mv.setIdentity();
		mv = mv_.cast<float>();
		mvp = proj_ * mv_.cast<float>();
		// imvp = mvp.inverse();
		imvp = (proj_.cast<double>() * mv_).inverse().cast<float>();

		wh(0) = sd.wh[0];
		wh(1) = sd.wh[1];


		eye = eye_.cast<float>();

		Vector3f eyeGeodetic;
		ecef_to_geodetic(eyeGeodetic.data(), 1, eye.data());
		haeAlt = eyeGeodetic(2);

        colorMult.setConstant(1);
        sun.setZero();

        haze = time = dt = 0;
		dt = lastSceneData.dt;
		time = lastSceneData.elapsedTime;

        return out;
    };

	GlobeCamera::GlobeCamera(
				const CameraIntrin& intrin,
				AppObjects& ao,
				const double startingLocationEcef[3]) : InteractiveCamera(intrin, ao) {

        Map<Vector3d> eye(this->p);
		eye = Map<const Vector3d>(startingLocationEcef);

        Map<Quaterniond> q(this->q);
		Matrix3d R = lookAtR(Vector3d::Zero(), eye, Vector3d::UnitZ());
		q = R;

		Matrix<float,4,4> proj;
		intrin.proj(proj.data());
        spdlog::get("wg")->trace("eye : {}", eye.transpose());
        spdlog::get("wg")->trace("R   :\n{}", R);
        spdlog::get("wg")->trace("proj:\n{}", proj);
		// throw std::runtime_error("");

		resetTargetOffset();

	}

    void GlobeCamera::step(const SceneData& sd) {
		lastSceneData = sd;


		Map<Vector3d> eye(this->p);
		Vector3d eyeWgs;
		ecef_to_geodetic(eyeWgs.data(), 1, eye.data());
		double haeAlt = eyeWgs[2];

		if (dynamicNearFarPlane) {
			intrin.near = std::max(100 / 6e6, std::min(3'000 / 6e6, .08*haeAlt));
			if (haeAlt < 10'000/6e6) {
				intrin.near = (60 / 6e6);
			}
			if (haeAlt < 3'000/6e6) {
				intrin.near = (35 / 6e6);
			}
			if (haeAlt < 2'000/6e6) {
				intrin.near = (30 / 6e6);
			}
			// intrin.far  = std::max(intrin.near * 1000.f, 1.f / 6e6f);
			intrin.far  = std::max(haeAlt*10., .2 * 6e6 / 6e6);
		}

		Matrix3d Ltp0 = getLtp(eye);

		double m = 1;
		if (haeAlt > 6'000/6e6) m *= 1 + 1.*std::sqrt(std::min(haeAlt-6'000/6e6, 1.));
		double speed = (std::abs(haeAlt) + 2e-3) * 9.1 * m;
		if (shiftDown) speed *= .1;
		float dt = lastSceneData.dt;

		Vector3d accInCamera;
		accInCamera[2] = (keyDown[GLFW_KEY_W] ? 1 : keyDown[GLFW_KEY_S] ? -1 : 0) * speed;
		accInCamera[0] = (keyDown[GLFW_KEY_D] ? 1 : keyDown[GLFW_KEY_A] ? -1 : 0) * speed;
		accInCamera[1] = (keyDown[GLFW_KEY_R] ? 1 : keyDown[GLFW_KEY_F] ? -1 : 0) * speed;

        Map<Quaterniond> q(this->q);
		Matrix3d R = q.toRotationMatrix();

		Vector3d accInWorld = R * accInCamera;
		// Vector3d accInWorld = Ltp0 * accInCamera;

		if (haveTarget) {
			stepWithTarget(sd, dt, accInWorld.data());
			mouseDx = mouseDy = 0;
			return;
		}


		Map<Vector3d> v(this->v);
		v = v * std::exp(-dt / dragTimeConstant) + accInWorld * dt;

		if (v.squaredNorm() < 1e-16) v.setZero();
		Vector3d deye = v * dt + accInWorld * dt * dt * .5;

		// When we move the eye, try to equalize the orientation relative to LTP.
		if (deye.squaredNorm() > 1e-20) {
			eye += deye;
			Matrix3d Ltp1 = getLtp(eye);

			q = Ltp1 * Ltp0.transpose() * q;
			// q = q * Ltp1 * Ltp0.transpose();

			q = q.normalized();
		}

        // spdlog::get("wg")->info("new eye: {} from dt {} w down {} speed {} haeAlt {}m", eye.transpose(), dt, keyDown[GLFW_KEY_W], speed,haeAlt*Earth::R1);
        // spdlog::get("wg")->info("cam mouse click {} {} moused {} {}", leftClicked, rightClicked,  mouseDx, mouseDy);

		Map<Vector2d> mouseDxySmooth { this->mouseDxySmooth };
		if (leftClicked) {
			Vector2d dxy { mouseDx, mouseDy };
			mouseDxySmooth = mouseDxySmooth * std::exp(-dt / .07f) + dxy;

			double aspeed = dt * 8 * (M_PI/180);

			/*
			Quaterniond after { AngleAxisd(mouseDxySmooth[1] * aspeed, Vector3d::UnitX()) };
			Quaterniond before { AngleAxisd(-mouseDxySmooth[0] * aspeed, Vector3d::UnitY()) };
			q = after * q * before;
			*/
			Quaterniond updown { AngleAxisd(mouseDxySmooth[1] * aspeed, -Ltp0.col(0)) };
			Quaterniond leftright { AngleAxisd(-mouseDxySmooth[0] * aspeed, -Ltp0.col(2)) };
			// q = updown * leftright * q;
			q = leftright * q * updown;

			// TODO: Not good. In fact a trackball cam with just modifying pitch/yaw would be better
			RowMatrix3d R_constrained;
			R_constrained.col(2) = q.toRotationMatrix().col(2);
			if (std::abs(Ltp0.col(2).dot(R_constrained.col(2))) < .98) {
				R_constrained.col(0) = R_constrained.col(2).cross(Ltp0.col(2)).normalized();
				R_constrained.col(1) = R_constrained.col(2).cross(R_constrained.col(0)).normalized();
				q = R_constrained;
			}

		} else {
			mouseDxySmooth.setZero();
		}

		mouseDx = mouseDy = 0;
		// leftClicked = rightClicked = false;
    }

    void GlobeCamera::stepWithTarget(const SceneData& sd, float dt, const double accInWorld_[3]) {
		Map<const Vector3d> accInWorld(accInWorld_);
		Map<Vector3d> eyeOut(this->p);
        Map<Quaterniond> qOut(this->q);

		Map<Vector3d> tp(this->target_p);
        Map<Quaterniond> tq(this->target_q);

		Map<Vector3d> top(this->target_offset_p);
        Map<Quaterniond> toq(this->target_offset_q);

		Map<Vector3d> fp(this->follow_p);
        Map<Quaterniond> fq(this->follow_q);

		Matrix3d Ltp0 = getLtp(eyeOut);
		
		Map<Vector3d> v(this->v);
		v = v * std::exp(-dt / dragTimeConstant) + accInWorld * dt;
		if (v.squaredNorm() < 1e-16) v.setZero();
		Vector3d deye = v * dt + accInWorld * dt * dt * .5;

		// Multiplying by toq* is messy, but by Ltp* makes sense so that we can specify the default in ENU coords.
		top += Ltp0.transpose() * toq.conjugate() * deye;

		// Quaternion interpolation via so3 algebra, position simpler.
		Quaterniond fqInv_x_tq = fq.conjugate() * tq;
		AngleAxisd delta_fq_0 { fqInv_x_tq };
		AngleAxisd delta_fq { delta_fq_0.angle() * alpha_q, delta_fq_0.axis() };
		fq = (fq * delta_fq).normalized();
		fp = fp * (1.-alpha_p) + alpha_p * tp;

		// Update toq
		Map<Vector2d> mouseDxySmooth { this->mouseDxySmooth };
		if (leftClicked) {
			Vector2d dxy { mouseDx, mouseDy };
			mouseDxySmooth = mouseDxySmooth * std::exp(-dt / .07f) + dxy;

			double aspeed = dt * 8 * (M_PI/180);

			Quaterniond updown { AngleAxisd(mouseDxySmooth[1] * aspeed, -Ltp0.col(0)) };
			Quaterniond leftright { AngleAxisd(-mouseDxySmooth[0] * aspeed, -Ltp0.col(2)) };
			// leftright.setIdentity();
			// updown.setIdentity();

			toq = leftright * toq * updown;
			toq = toq.normalized();

			// TODO: Not good. In fact a trackball cam with just modifying pitch/yaw would be better
			/*
			RowMatrix3d R_constrained;
			R_constrained.col(2) = toq.toRotationMatrix().col(2);
			if (std::abs(Ltp0.col(2).dot(R_constrained.col(2))) < .98) {
				R_constrained.col(0) = R_constrained.col(2).cross(Ltp0.col(2)).normalized();
				R_constrained.col(1) = R_constrained.col(2).cross(R_constrained.col(0)).normalized();
				toq = R_constrained;
			}
			*/

		} else {
			mouseDxySmooth.setZero();
		}

		// Possibly reset. If you press 'z' twice it clears target, once just resets to default
		if (keyDown[GLFW_KEY_Z]) {
			if (shiftDown) {
				spdlog::get("wg")->info("'shift+z' pressed, clearing target");
				clearTarget();
			} else {
				spdlog::get("wg")->info("'z' pressed, resetting target offsets");
				resetTargetOffset();
			}
		}

		// eyeOut = fp + toq * top;
		eyeOut = fp + toq * Ltp0*top;
		// qOut = fq * toq;
		qOut = toq * fq;

	}

	void GlobeCamera::setTarget(bool activate, const double* p, const double* q) {
		if (activate) haveTarget = true;
		for (int i=0; i<3; i++) target_p[i] = p[i];
		for (int i=0; i<4; i++) target_q[i] = q[i];
	}
	void GlobeCamera::clearTarget() {
		haveTarget = false;
	}
	void GlobeCamera::resetTargetOffset() {
		for (int i=0; i<3; i++) target_offset_p[i] = target_offset_p_default[i];
		for (int i=0; i<4; i++) target_offset_q[i] = target_offset_q_default[i];
	}

}

