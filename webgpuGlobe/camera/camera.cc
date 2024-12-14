#include "camera.h"
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

    static Matrix3d getEllipsoidalLtp(const Vector3d& p) {
		Vector3d n = ((p.array()
					/ Eigen::Array3d{Earth::R1, Earth::R1, Earth::R2})
					* Eigen::Array3d{1./Earth::R1, 1./Earth::R1, 1./Earth::R2}
				).matrix().normalized();
		return lookAtR(Vector3d::Zero(), n, Vector3d::UnitZ());
	}

    void lookAtR(float R[9], const float target_[3], const float eye_[3], const float up_[3]) {
        Vector3f target { Map<const Vector3f>(target_) };
        Vector3f eye { Map<const Vector3f>(eye_) };
        Vector3f up0 { Map<const Vector3f>(up_) };
        Map<RowMatrix3f> out { R };
		out.col(2) = (target - eye).normalized();
		out.col(0) = out.col(2).cross(up0).normalized();
		out.col(1) = out.col(2).cross(out.col(0)).normalized();
    }

    void getEllipsoidalLtp(float R[9], const float eye_[3]) {
        Vector3d eye { Map<const Vector3f>(eye_).cast<double>() };

		RowMatrix3d outd = getEllipsoidalLtp(eye);
        Map<RowMatrix3f> out { R };
		out = outd.cast<float>();
    }

    CameraIntrin::CameraIntrin(int w, int h, float vfov, float n, float f)
        : w(w)
        , h(h)
        , near(n)
        , far(f) {
        float v_ = std::tan(vfov * .5f) * 2;
        float u_ = std::tan(vfov * .5f) * 2 * (static_cast<float>(w)/h);
        fy = h / v_;
        fx = w / u_;
		cx = w * .5f;
		cy = h * .5f;

		// frustum.l = -u_ * .5;
		// frustum.r =  u_ * .5;
		// frustum.t = -v_ * .5;
		// frustum.b =  v_ * .5;
		frustum.l = (0 - cx) / fx;
		frustum.r = (w - cx) / fx;
		frustum.t = (0 - cy) / fy;
		frustum.b = (h - cy) / fy;
    }

	CameraIntrin::CameraIntrin(int w, int h, float fx, float fy, float cx, float cy, float near, float far)
		: w(w)
		  , h(h)
		  , fx(fx)
		  , fy(fy)
		  , cx(cx)
		  , cy(cy)
		  , near(near)
		  , far(far)
	{
		// float u_ = .5f * h / fx;
		// float v_ = .5f * w / fy;
		// frustum.l = -u_ * .5f;
		// frustum.r =  u_ * .5f;
		// frustum.t = -v_ * .5f;
		// frustum.b =  v_ * .5f;

		frustum.l = (0 - cx) / fx;
		frustum.r = (w - cx) / fx;
		frustum.t = (0 - cy) / fy;
		frustum.b = (h - cy) / fy;
	}

    void CameraIntrin::proj(float out[16]) const {
		// double u = u_ * .5;
		// double v = v_ * .5;
        // float l = -u, r = u;
        // float b = -v, t = v;
        // float b = v, t = -v;

        float A = (frustum.r + frustum.l) / (frustum.r - frustum.l);
        float B = (frustum.t + frustum.b) / (frustum.t - frustum.b);
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
			2 / (frustum.r-frustum.l), 0, A, 0,
			0, 2 / (frustum.t-frustum.b), B, 0,
			0, 0, -C, D,
			0, 0, 1, 0;
        // clang-format on
    }

	void CameraIntrin::updateSize_(int nw, int nh) {
		float v = h / fy;
		float u = v * static_cast<float>(nw) / nh;
		fy = nh / v;
		fx = nw / u;
		w = nw;
		h = nh;
		cx = nw * .5f;
		cy = nh * .5f;

		frustum.l = (0 - cx) / fx;
		frustum.r = (w - cx) / fx;
		frustum.t = (0 - cy) / fy;
		frustum.b = (h - cy) / fy;
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

}
