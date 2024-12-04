#pragma once

#include "webgpuGlobe/app/app.h"
#include "webgpuGlobe/app/wrappers.hpp"

namespace wg {

	template <size_t M> inline constexpr size_t roundUp(size_t x) {
		return ((x + M - 1) / M) * M;
	}

	// #pragma attribute((layout())(C)
	#pragma __attribute__((packed, aligned(256)))
	extern "C"
	struct SceneCameraData1 {
		alignas(16) float mvp[16];
		alignas(16) float imvp[16];
		alignas(16) float mv[16];
		alignas(16) float eye[3];
		alignas(16) float colorMult[4];
		alignas(16) float sun[4];
		float haeAlt;
		float haze;
		float time;
		float dt;

		inline static constexpr size_t size() { return sizeof(SceneCameraData1); }
	};

	void lookAtR(float R[9], const float target[3], const float eye[3], const float up[3]);
	void lookAt(float T[16], const float target[3], const float eye[3], const float up[3]);

    struct CameraIntrin {
        int w, h;
        float fx, fy;
        float u_, v_;
		float near, far;

		// CameraIntrin(int w, int h, float vfov, float near=20 / 6e6, float far = 100'000 / 6e6);
		CameraIntrin(int w, int h, float vfov, float near=220 / 6e6, float far = 5.5 * 6e6 / 6e6);
		void proj(float out[16]) const;
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

    struct GlobeCamera : public InteractiveCamera {

		// using InteractiveCamera::InteractiveCamera;
		GlobeCamera(
				const CameraIntrin& intrin,
				AppObjects& ao,
				const double startingLocationEcef[3]);

		virtual SceneCameraData1 lower(const SceneData& sd) override;
		virtual void step(const SceneData& sd) override;
		
        double p[3] = { 0, -8e6, 0 };
        double q[4] = { 0, 0, 0, 1 };

        double v[3] = { 0, 0, 0 };
        double a[3] = { 0, 0, 0 }; // angular vel

		double mouseDxySmooth[2] = {0,0};

		// May be set in IoListener overrides or in step() from the IoListenerWithState members. Used in step()
		// double acc[3];
		// double aacc[3];

    };

    // A cheap reference type that can track local transformations down some render tree.
    struct RenderState {
        const SceneCameraData1& camData;
		const CameraIntrin& intrin;
        CommandEncoder& cmdEncoder;
        RenderPassEncoder& pass;
        AppObjects& appObjects;
        FrameData& frameData;

        RenderState(const RenderState& o)            = delete;
        RenderState& operator=(const RenderState& o) = delete;

        inline RenderState cloneWithCam(SceneCameraData1& newCamData) {
            return RenderState {
                .camData    = newCamData,
                .intrin     = intrin,
                .cmdEncoder = cmdEncoder,
                .pass       = pass,
                .appObjects = appObjects,
                .frameData  = frameData,
            };
        }
    };

    struct Renderable {
        inline virtual ~Renderable() {
        }

        virtual void render(const RenderState& rs) = 0;

		// virtual void buildPipeline(Pass& pass);

    };

}
