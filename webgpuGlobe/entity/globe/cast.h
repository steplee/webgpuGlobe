#pragma once

#include <optional>
#include <array>

#include "webgpuGlobe/app/app.h"

#include "webgpuGlobe/util/image.h"

namespace wg {


	// 
	// Used to configure the cast resources of a Globe impl.
	// Everything is optional, making this a good API type.
	// When an update to any field is available, pass it here.
	// Otherwise the state will not be updated and the previous state passed through.
	//
	// Turn off casting by passing an empty Image (but still with an optional value), or
	// even better just pass a mask of value 0u32.
	//
	struct CastUpdate {

		std::optional<Image> img;

		std::optional<std::array<float,16>> castMvp1;
		std::optional<std::array<float,16>> castMvp2;
		std::optional<std::array<float,4>> castColor1;
		std::optional<std::array<float,4>> castColor2;

		std::optional<uint32_t> mask;

		WGPUTextureFormat texFmt = WGPUTextureFormat_BGRA8Unorm;

	};

	// What actually goes into the UBO.
	struct __attribute__((packed)) CastData {
		float castMvp1[16];
		float castMvp2[16];
		float castColor1[4] = {1};
		float castColor2[4] = {1};
		uint32_t mask = 0;
	};

	//
	// Shared code for any Globe impl that supports casting.
	// This includes the texture, BindGroup[Layout], and the UBO of the CastData.
	//
	// Note that this lacks a RenderPipeline. Each Globe renderer must create that itself.
	//
	struct CastGpuResources {
		BindGroupLayout bindGroupLayout;
		BindGroup bindGroup;

		// False if we don't need to render in cast mode (usually: all masks are off)
		bool active() const { return castDataCpu.mask != 0; }

		void create(AppObjects& ao);
		void updateCastBindGroupAndResources(AppObjects& ao, const CastUpdate& castUpdate);

		private:
			Buffer buffer;
			size_t bufferSize = 0;

			int lastTexW = 0;
			int lastTexH = 0;
			WGPUTextureFormat lastTextFmt = WGPUTextureFormat_Undefined;
			Texture tex;
			TextureView texView;
			Sampler sampler;

			CastData castDataCpu;


	};

}
