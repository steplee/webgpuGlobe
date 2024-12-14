#pragma once

#include "webgpuGlobe/app/app.h"

namespace wg {

	void lookAtR(float R[9], const float target[3], const float eye[3], const float up[3]);
	void lookAt(float T[16], const float target[3], const float eye[3], const float up[3]);
    void getEllipsoidalLtp(float R[9], const float eye_[3]);

	template <size_t M> inline constexpr size_t roundUp(size_t x) {
		return ((x + M - 1) / M) * M;
	}

    struct CameraIntrin {
        int w, h;
        float fx, fy;
        float cx, cy;
		float near, far;
        // float u_, v_;
		struct {
			float l, r, t, b;
		} frustum;

		// CameraIntrin(int w, int h, float vfov, float near=20 / 6e6, float far = 100'000 / 6e6);
		CameraIntrin(int w, int h, float vfov, float near=220 / 6e6, float far = 5.5 * 6e6 / 6e6);
		CameraIntrin(int w, int h, float fx, float fy, float cx, float cy, float near, float far);
		void proj(float out[16]) const;

		void updateSize_(int nw, int nh);
    };

	// #pragma attribute((layout())(C)
	#pragma __attribute__((packed, aligned(256)))
	extern "C"
	struct SceneCameraData1 {
		alignas(16) float mvp[16];
		alignas(16) float imvp[16];
		alignas(16) float mv[16];
		alignas(16) float eye[3];
		alignas(16) float colorMult[4];
		alignas(16) float wh[2];
		alignas(16) float sun[4];
		float haeAlt;
		float haze;
		float time;
		float dt;

		inline static constexpr size_t size() { return sizeof(SceneCameraData1); }
	};

    struct Camera {
		virtual SceneCameraData1 lower(const SceneData& sd) =0;
		virtual void step(const SceneData& sd) =0;
	};

	struct SceneDataResource {

		SceneDataResource(AppObjects& ao);

		using DataType = SceneCameraData1;
		Buffer buffer;
		BindGroupLayout bindGroupLayout;
		BindGroup bindGroup;
	};

    struct InteractiveCamera : public IoListenerWithState, public Camera {

		// inline InteractiveCamera(const CameraIntrin& intrin, GLFWwindow* w) : intrin(intrin), IoListenerWithState(w) {}
		inline InteractiveCamera(const CameraIntrin& intrin, AppObjects& ao) : intrin(intrin), sdr(ao), IoListenerWithState(ao.window) {}

		CameraIntrin intrin;
		SceneDataResource sdr;
		SceneData lastSceneData;

		inline operator SceneDataResource& () { return sdr; }
		inline BindGroupLayout& getBindGroupLayout() { return sdr.bindGroupLayout; }
		inline BindGroup& getBindGroup() { return sdr.bindGroup; }

    };


}
