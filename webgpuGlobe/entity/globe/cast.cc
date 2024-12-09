#include "cast.h"
#include "entity/globe/webgpu_utils.hpp"
#include "entity/renderable.h"
#include <Eigen/Core>
#include <Eigen/Dense>

namespace wg {

		void CastGpuResources::create(AppObjects& ao) {

            sampler       = ao.device.create(WGPUSamplerDescriptor {
                      .nextInChain  = nullptr,
                      .label        = "CastSampler",
                      .addressModeU = WGPUAddressMode_ClampToEdge,
                      .addressModeV = WGPUAddressMode_ClampToEdge,
                      .addressModeW = WGPUAddressMode_ClampToEdge,
                      .magFilter    = WGPUFilterMode_Linear,
                      .minFilter    = WGPUFilterMode_Linear,
                      .mipmapFilter = WGPUMipmapFilterMode_Nearest,
                      .lodMinClamp  = 0,
                      .lodMaxClamp  = 32,
                // .compare       = WGPUCompareFunction_Less,
                      .compare       = WGPUCompareFunction_Undefined,
                      .maxAnisotropy = 1,
            });

            WGPUBindGroupLayoutEntry castTexLayoutEntries[3] = {
                {
                 .nextInChain    = nullptr,
                 .binding        = 0,
                 .visibility     = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
                 .buffer         = WGPUBufferBindingLayout { .nextInChain = nullptr, .type = WGPUBufferBindingType_Undefined },
                 .sampler        = { .nextInChain = nullptr, .type = WGPUSamplerBindingType_Undefined },
                 .texture        = { .nextInChain   = nullptr,
                 .sampleType    = WGPUTextureSampleType_Float,
                 .viewDimension = WGPUTextureViewDimension_2D,
                 .multisampled  = false },
                 .storageTexture = { .nextInChain = nullptr, .access = WGPUStorageTextureAccess_Undefined },
                 },
                {
                 .nextInChain    = nullptr,
                 .binding        = 1,
                 .visibility     = WGPUShaderStage_Fragment,
                 .buffer         = WGPUBufferBindingLayout { .nextInChain = nullptr, .type = WGPUBufferBindingType_Undefined },
                 .sampler        = { .nextInChain = nullptr, .type = WGPUSamplerBindingType_Filtering },
                 .texture        = { .nextInChain = nullptr, .sampleType = WGPUTextureSampleType_Undefined },
                 .storageTexture = { .nextInChain = nullptr, .access = WGPUStorageTextureAccess_Undefined },
                 },
                {
                 .nextInChain    = nullptr,
                 .binding        = 2,
                 .visibility     = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
                 .buffer         = WGPUBufferBindingLayout { .nextInChain      = nullptr,
                 .type             = WGPUBufferBindingType_Uniform,
                 .hasDynamicOffset = false,
                 .minBindingSize   = 0 },
                 .sampler        = { .nextInChain = nullptr, .type = WGPUSamplerBindingType_Undefined },
                 .texture        = { .nextInChain = nullptr, .sampleType = WGPUTextureSampleType_Undefined },
                 .storageTexture = { .nextInChain = nullptr, .access = WGPUStorageTextureAccess_Undefined },
                 },
            };
            bindGroupLayout = ao.device.create(WGPUBindGroupLayoutDescriptor {
                .nextInChain = nullptr, .label = "CastBGL", .entryCount = 3, .entries = castTexLayoutEntries });
		}

        void CastGpuResources::updateCastBindGroupAndResources(AppObjects& ao, const CastUpdate& castUpdate) {

            // If we have valid cast data, update mvp.
			if (castUpdate.castMvp1.has_value() or
			    castUpdate.castMvp2.has_value() or
			    castUpdate.castColor1.has_value() or
			    castUpdate.castColor2.has_value() or
			    castUpdate.mask.has_value())
            {
                spdlog::get("wg")->info("uploading new cast buffer");

				if (castUpdate.castMvp1.has_value())
					memcpy(castDataCpu.castMvp1, castUpdate.castMvp1->data(), sizeof(float)*16);
				if (castUpdate.castMvp2.has_value())
					memcpy(castDataCpu.castMvp2, castUpdate.castMvp2->data(), sizeof(float)*16);
				if (castUpdate.castColor1.has_value())
					memcpy(castDataCpu.castColor1, castUpdate.castColor1->data(), sizeof(float)*4);
				if (castUpdate.castColor2.has_value())
					memcpy(castDataCpu.castColor2, castUpdate.castColor2->data(), sizeof(float)*4);
				if (castUpdate.mask.has_value())
					memcpy(&castDataCpu.mask, &castUpdate.mask.value(), sizeof(uint32_t)*1);

                size_t castMvpBufSize_raw = sizeof(CastData);

                if (bufferSize == 0) {
                    bufferSize = roundUp<256>(castMvpBufSize_raw);
                    spdlog::get("wg")->info("creating castMvpBuf of size {}", bufferSize);
                    WGPUBufferDescriptor desc {
                        .nextInChain      = nullptr,
                        .label            = "CastMvpBuffer",
                        .usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform,
                        .size             = bufferSize,
                        .mappedAtCreation = false,
                    };
                    buffer = ao.device.create(desc);
                }

                ao.queue.writeBuffer(buffer, 0, &castDataCpu, castMvpBufSize_raw);
            }

            // Now upload the texture if the new `castInfo.img` is valid.
            // If the texture has not been created yet, or if it has changed size, we must (re-) create it.
            // If we (re-) create the texture, we must (re-) create the BindGroup as well.

            if (!castUpdate.img.has_value()) {
                spdlog::get("wg")->info("skip empty non-new tex");
                return;
            }

			if (castUpdate.img->empty()) {
                spdlog::get("wg")->error("Empty image. Skipping. What should we do here?");
                return;
			}

			const auto& img = castUpdate.img.value();
            uint32_t texw = img.cols;
            uint32_t texh = img.rows;
            auto texFmt   = WGPUTextureFormat_RGBA8Unorm;

            if (img.cols == lastTexW and img.rows == lastTexH /*and texFmt == lastCastTexFmt*/) {
                spdlog::get("wg")->info("use cached cast tex {} {} {}", texw, texh, (int)texFmt);
            } else {
                lastTexW = img.cols;
                lastTexH = img.rows;
                spdlog::get("wg")->info("(re-)create cached cast tex {} {} {} and bind group", texw, texh, (int)texFmt);

				// ----------------------------------------------------------------------------------------------------------------------
				//     Texture + View

                tex     = ao.device.create(WGPUTextureDescriptor {
                        .nextInChain     = nullptr,
                        .label           = "CastTex",
                        .usage           = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding,
                        .dimension       = WGPUTextureDimension_2D,
                        .size            = WGPUExtent3D { texw, texh, 1 },
                        .format          = WGPUTextureFormat_RGBA8Unorm,
                        .mipLevelCount   = 1,
                        .sampleCount     = 1,
                        .viewFormatCount = 0,
                        .viewFormats     = 0
                });

                texView = tex.createView(WGPUTextureViewDescriptor {
                    .nextInChain     = nullptr,
                    .label           = "CastTexView",
                    .format          = WGPUTextureFormat_RGBA8Unorm,
                    .dimension       = WGPUTextureViewDimension_2D,
                    .baseMipLevel    = 0,
                    .mipLevelCount   = 1,
                    .baseArrayLayer  = 0,
                    .arrayLayerCount = 1,
                    .aspect          = WGPUTextureAspect_All,
                });


				// ----------------------------------------------------------------------------------------------------------------------
				//     BindGroup

                WGPUBindGroupEntry groupEntries[3] = {
                    { .nextInChain = nullptr,
                     .binding     = 0,
                     .buffer      = 0,
                     .offset      = 0,
                     .size        = 0,
                     .sampler     = nullptr,
                     .textureView = texView                                                                                        },
                    { .nextInChain = nullptr, .binding = 1, .buffer = 0, .offset = 0,   .size = 0, .sampler = sampler, .textureView = 0 },
                    { .nextInChain = nullptr,
                     .binding     = 2,
                     .buffer      = buffer,
                     .offset      = 0,
                     .size        = bufferSize,
                     .sampler     = 0,
                     .textureView = 0                                                                                                  },
                };
                bindGroup = ao.device.create(WGPUBindGroupDescriptor { .nextInChain = nullptr,
                                                                             .label       = "CastBG",
                                                                             .layout      = bindGroupLayout,
                                                                             .entryCount  = 3,
                                                                             .entries     = groupEntries });
            }

            // Upload tex.
            // ...

            uploadTex_(tex, ao, 0, img.data(), img.total() * img.elemSize(), texw, texh, img.channels());
        }



	void make_cast_matrix(
			float* out_,
			const double eye_[3],
			const double R_[9],
			const float f[2],
			const float c[2],
			const int wh[2],
			float near, float far
			) {

		using namespace Eigen;
		using RowMatrix3f = Matrix<float, 3,3, RowMajor>;
		using RowMatrix4f = Matrix<float, 4,4, RowMajor>;
		using RowMatrix3d = Matrix<double, 3,3, RowMajor>;
		using RowMatrix4d = Matrix<double, 4,4, RowMajor>;

		Vector3d eye { Map<const Vector3d>{eye_} };
		RowMatrix3d R { Map<const RowMatrix3d>{R_} };

		// [R|eye] is the inverse-view matrix. We can do an efficient SE3 inverse here since
		// we know R is det(1).
		RowMatrix4d view;
#if 1
		view.topLeftCorner<3,3>() = R.transpose();
		view.topRightCorner<3,1>() = -R.transpose() * eye;
		// view.topRightCorner<3,1>() = -R * eye;
		view.row(3) << 0,0,0,1;
#else
		view.topLeftCorner<3,3>() = R;
		view.topRightCorner<3,1>() = eye;
		view.row(3) << 0,0,0,1;
		view = view.inverse().eval();
#endif

		CameraIntrin cam(wh[0], wh[1], f[0], f[1], c[0], c[1], near, far);
		Matrix4f proj;
		cam.proj(proj.data());

		Map<Matrix4f> out{out_};
		out = (proj.cast<double>() * view).cast<float>();
		// out = ( view).cast<float>();
	}

}
