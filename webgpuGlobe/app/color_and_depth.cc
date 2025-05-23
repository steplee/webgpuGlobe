#include "color_and_depth.h"
#include <wgpu/wgpu.h>
#include <Eigen/Geometry>

namespace wg {

	void ColorAndDepthInfo::queueRead(AppObjects& ao, const float *imvp_, const CameraIntrin& intrin_, Texture& colorTex, Texture& depthTex, CommandEncoder& ce) {
		intrin = intrin_;
		memcpy(imvp, imvp_, 16*4);
		memcpy(eye, eye, 3*4);


		// int step = intrin.w * 4; // same for rgba as for depth32

		uint32_t alignment = 256;
		uint32_t step = (intrin.w * 4 + alignment - 1) / alignment * alignment;

		if (color.empty()) {

			// color.create(intrin.h, intrin.w, CV_8UC4);
			// depth.create(intrin.h, intrin.w, CV_32FC1);


			// We ALLOCATE wider than output image then immediately VIEW the inner box.
			step = (intrin.w * 4 + alignment - 1) / alignment * alignment;
			color.create(intrin.h, step/4, CV_8UC4);
			depth.create(intrin.h, step/4, CV_32FC1);
			color = color(cv::Rect{0,0,intrin.w,intrin.h});
			depth = depth(cv::Rect{0,0,intrin.w,intrin.h});

			colorBuf = ao.device.create(WGPUBufferDescriptor {
					.nextInChain = nullptr,
					.label = "colorAndDepthInfo:colorBuf",
					.usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
					.size = intrin.h*step,
					.mappedAtCreation = false
					});

			depthBuf = ao.device.create(WGPUBufferDescriptor {
					.nextInChain = nullptr,
					.label = "colorAndDepthInfo:depthBuf",
					.usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
					.size = intrin.h*step,
					.mappedAtCreation = false
					});
		}


		ce.copyTextureToBuffer(
			WGPUImageCopyTexture {
				.nextInChain = nullptr,
				.texture = colorTex,
				.mipLevel=0,
				.origin = {},
				.aspect = WGPUTextureAspect_All
			},
			WGPUImageCopyBuffer {
				.nextInChain = nullptr,
				.layout = WGPUTextureDataLayout {
					.nextInChain  = nullptr,
					.offset       = 0,
					.bytesPerRow  = step,
					.rowsPerImage = (uint32_t)intrin.h,
				},
				.buffer = colorBuf,
			},
			WGPUExtent3D { (uint32_t)intrin.w, (uint32_t)intrin.h, 1 });

		ce.copyTextureToBuffer(
			WGPUImageCopyTexture {
				.nextInChain = nullptr,
				.texture = depthTex,
				.mipLevel=0,
				.origin = {},
				.aspect = WGPUTextureAspect_DepthOnly
			},
			WGPUImageCopyBuffer {
				.nextInChain = nullptr,
				.layout = WGPUTextureDataLayout {
					.nextInChain  = nullptr,
					.offset       = 0,
					.bytesPerRow  = step,
					.rowsPerImage = (uint32_t)intrin.h,
				},
				.buffer = depthBuf,
			},
			WGPUExtent3D { (uint32_t)intrin.w, (uint32_t)intrin.h, 1 });
	}

	static void callback_(WGPUBufferMapAsyncStatus status, void * userdata) {
	}

		void ColorAndDepthInfo::mapAndCopyToMat(AppObjects& ao) {
			wgpuBufferMapAsync(colorBuf, WGPUMapMode_Read, 0, colorBuf.getSize(), &callback_, nullptr);
			wgpuBufferMapAsync(depthBuf, WGPUMapMode_Read, 0, depthBuf.getSize(), &callback_, nullptr);
			wgpuDevicePoll(ao.device, true, NULL);

			const void* c_ptr = wgpuBufferGetConstMappedRange(colorBuf, 0, colorBuf.getSize());
			memcpy(color.data, c_ptr, colorBuf.getSize());
			const void* d_ptr = wgpuBufferGetConstMappedRange(depthBuf, 0, depthBuf.getSize());
			memcpy(depth.data, d_ptr, depthBuf.getSize());

			wgpuBufferUnmap(colorBuf);
			wgpuBufferUnmap(depthBuf);
		}




		// screen space.
		float ColorAndDepthInfo::accessDepth(int x, int y) const {
			assert(x < depth.cols and x >= 0 and y >= 0 and y < depth.rows);
			
			const float *ptr = reinterpret_cast<const float*>(depth.data);

			return ptr[y*depth.step1() + x];
		}

		Eigen::Vector3f ColorAndDepthInfo::accessUnitEcefPoint(int x, int y) const {
			// screen space => world space

			float xx = static_cast<float>(x) / intrin.w * 2.f - 1.f;
			float yy = static_cast<float>(y) / intrin.h * 2.f - 1.f;
			float d = accessDepth(x,y);

			Eigen::Vector3f out;
			Eigen::Map<const Eigen::Matrix4f> IMVP { imvp };
			out = (IMVP * Eigen::Vector4f { xx, yy, d, 1. }).hnormalized();
			return out;
		}

}
