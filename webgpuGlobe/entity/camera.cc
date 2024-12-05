#include "renderable.h"
#include <Eigen/Core>
#include <Eigen/Geometry>

#include <spdlog/fmt/ostr.h>

#include "geo/earth.hpp"
#include "geo/conversions.h"
#include "util/fmtEigen.h"


namespace wg {

    using namespace Eigen;

    using RowMatrix4f = Eigen::Matrix<float, 4, 4, RowMajor>;
    using RowMatrix3f = Eigen::Matrix<float, 3, 3, RowMajor>;

	// untested.
    static Matrix3d getLtp(const Vector3d& eye) {
		Vector3d f = eye.normalized();
		Vector3d r = f.cross(Vector3d::UnitZ());
		Vector3d u = f.cross(r);
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

    void lookAtR(float R[9], const float target_[3], const float eye_[3], const float up_[3]) {
        Vector3f target { Map<const Vector3f>(target_) };
        Vector3f eye { Map<const Vector3f>(eye_) };
        Vector3f up { Map<const Vector3f>(up_) };
        assert(false);
    }

    CameraIntrin::CameraIntrin(int w, int h, float vfov, float n, float f)
        : w(w)
        , h(h)
        , near(n)
        , far(f) {
        v_ = std::tan(vfov * .5f) * 1;
        u_ = std::tan(vfov * .5f) * 1 * (static_cast<float>(w)/h);
        fy = .5 * h / v_;
        fx = .5 * w / u_;
    }

    void CameraIntrin::proj(float out[16]) const {
		double u = u_ * .5;
		double v = v_ * .5;
        float l = -u, r = u;
        // float b = -v, t = v;
        float b = v, t = -v;

        float A = (r + l) / (r - l);
        float B = (t + b) / (t - b);
        float C = -(far + near) / (far - near);
        float D = -2 * far * near / (far - near);

        // clang-format off
		Map<Matrix4f> O(out);
		/*
		O <<
			2*near / (r-l), 0, A, 0,
			0, 2*near / (t-b), B, 0,
			0, 0, C, D,
			0, 0, -1, 0;
		*/

		O <<
			2 / (r-l), 0, A, 0,
			0, 2 / (t-b), B, 0,
			0, 0, -C, D,
			0, 0, 1, 0;
        // clang-format on
    }



	SceneDataResource::SceneDataResource(AppObjects& ao) {
            size_t camBufSize = roundUp<256>(SceneCameraData1::size());

            // Buffer
            spdlog::get("wg")->info("creating SceneDataResource buffer of size {}", camBufSize);
            WGPUBufferDescriptor desc {
                .nextInChain      = nullptr,
                .label            = "SceneDataResource",
                .usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform,
                .size             = camBufSize,
                // .mappedAtCreation = true,
                .mappedAtCreation = false,
            };
            buffer = ao.device.create(desc);

			/*
            void* dst    = wgpuBufferGetMappedRange(buffer, 0, camBufSize);
            memset(dst, 0, camBufSize);
			SceneCameraData1 cam0;
			cam0.colorMult[0] = 1.f;
			cam0.colorMult[1] = 1.f;
			cam0.colorMult[2] = 1.f;
			cam0.colorMult[3] = 1.f;
            memcpy(dst, &cam0, sizeof(cam0));
            wgpuBufferUnmap(buffer);
			*/

            // Layout
            spdlog::get("wg")->info("creating bindGroupLayout");
            WGPUBindGroupLayoutEntry layoutEntry {
                // How wgpu knows which of the union is valid:
                // https://github.com/gfx-rs/wgpu-native/blob/7c87e99293ee96872fc2d5cd3426eb4d67ff8a61/src/conv.rs#L1402
                .nextInChain    = nullptr,
                .binding        = 0,
                .visibility     = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
                .buffer         = WGPUBufferBindingLayout { .nextInChain      = nullptr,
                                                           .type             = WGPUBufferBindingType_Uniform,
                                                           .hasDynamicOffset = false,
                                                           .minBindingSize   = 0 },
                .sampler        = { .nextInChain = nullptr, .type = WGPUSamplerBindingType_Undefined },
                .texture        = { .nextInChain = nullptr, .sampleType = WGPUTextureSampleType_Undefined },
                .storageTexture = { .nextInChain = nullptr, .access = WGPUStorageTextureAccess_Undefined },
            };
            bindGroupLayout = ao.device.create(WGPUBindGroupLayoutDescriptor {
                .nextInChain = nullptr, .label = "SceneCameraData1", .entryCount = 1, .entries = &layoutEntry });
            spdlog::get("wg")->info("created bindGroupLayout");

            // Entry
            WGPUBindGroupEntry groupEntry { .nextInChain = nullptr,
                                            .binding     = 0,
                                            .buffer      = buffer,
                                            .offset      = 0,
                                            .size        = camBufSize,
                                            .sampler     = nullptr,
                                            .textureView = nullptr };

            bindGroup = ao.device.create(WGPUBindGroupDescriptor { .nextInChain = nullptr,
                                                                   .label       = "SceneCameraData1",
                                                                   .layout      = bindGroupLayout,
                                                                   .entryCount  = 1,
                                                                   .entries     = &groupEntry });
            spdlog::get("wg")->info("created bindGroup");
	}



    SceneCameraData1 GlobeCamera::lower(const SceneData& sd) {
        // spdlog::get("wg")->info("GlobeCamera::lower");

        SceneCameraData1 out;

        Map<Matrix4f> mvp(out.mvp);
        Map<Matrix4f> imvp(out.imvp);
        Map<Matrix4f> mv(out.mv);
        Map<Vector3f> eye(out.eye);
        Map<Vector4f> colorMult(out.colorMult);
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
		imvp = mvp.inverse();


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

        spdlog::get("wg")->info("eye : {}", eye.transpose());
        spdlog::get("wg")->info("R   :\n{}", R);
		Matrix<float,4,4> proj;
		intrin.proj(proj.data());
        spdlog::get("wg")->info("proj:\n{}", proj);
		// throw std::runtime_error("");


	}

    void GlobeCamera::step(const SceneData& sd) {
		lastSceneData = sd;

		Map<Vector3d> eye(this->p);
		Vector3d eyeWgs;
		ecef_to_geodetic(eyeWgs.data(), 1, eye.data());
		double haeAlt = eyeWgs[2];

		Matrix3d Ltp0 = getLtp(eye);


		double speed = (std::abs(haeAlt) + 1e-2) * 8.1;
		float dt = lastSceneData.dt;
		float dragTimeConstant = .1;

		Vector3d accInCamera;
		accInCamera[2] = (keyDown[GLFW_KEY_W] ? 1 : keyDown[GLFW_KEY_S] ? -1 : 0) * speed;
		accInCamera[0] = (keyDown[GLFW_KEY_D] ? 1 : keyDown[GLFW_KEY_A] ? -1 : 0) * speed;
		accInCamera[1] = (keyDown[GLFW_KEY_R] ? 1 : keyDown[GLFW_KEY_F] ? -1 : 0) * speed;

        Map<Quaterniond> q(this->q);
		Matrix3d R = q.toRotationMatrix();

		Vector3d accInWorld = R * accInCamera;
		// Vector3d accInWorld = Ltp0 * accInCamera;

		Map<Vector3d> v(this->v);
		v = v * std::exp(-dt / dragTimeConstant) + accInWorld * dt;

		Vector3d deye = v * dt + accInWorld * dt * dt * .5;

		// When we move the eye, try to equalize the orientation relative to LTP.
		{
			eye += deye;
			Matrix3d Ltp1 = getLtp(eye);

			q = Ltp1 * Ltp0.transpose() * q;

			q = q.normalized();
		}

        // spdlog::get("wg")->info("new eye: {} from dt {} w down {} speed {} haeAlt {}m", eye.transpose(), dt, keyDown[GLFW_KEY_W], speed,haeAlt*Earth::R1);
        // spdlog::get("wg")->info("cam mouse click {} {} moused {} {}", leftClicked, rightClicked,  mouseDx, mouseDy);

		Map<Vector2d> mouseDxySmooth { this->mouseDxySmooth };
		if (leftClicked) {
			Vector2d dxy { mouseDx, mouseDy };
			mouseDxySmooth = mouseDxySmooth * std::exp(-dt / .07f) + dxy;

			double aspeed = dt * 8 * (M_PI/180);
			Quaterniond after { AngleAxisd(mouseDxySmooth[1] * aspeed, Vector3d::UnitX()) };
			Quaterniond before { AngleAxisd(-mouseDxySmooth[0] * aspeed, Vector3d::UnitY()) };
			// Quaterniond before { Quaterniond::Identity() };
			q = after * q * before;
		} else {
			mouseDxySmooth.setZero();
		}

		mouseDx = mouseDy = 0;
		// leftClicked = rightClicked = false;

    }
}
