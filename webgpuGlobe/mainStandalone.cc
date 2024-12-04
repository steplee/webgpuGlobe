#include "app/wrappers.hpp"
#include <cassert>
#include <spdlog/spdlog.h>

#include <GLFW/glfw3.h>
#include <unistd.h>

using namespace wg;

#define WGPU 1
#if defined(WGPU)
#include <wgpu/wgpu.h>
#endif

void setup_logging() {
#if defined(WGPU)
		wgpuSetLogCallback([](WGPULogLevel level, char const * message, void * userdata) {
				printf("wgpu %d: %s\n", level,message);
				// spdlog::get("wgpu")->info("{}: {}", level, message);
		}, 0);
		wgpuSetLogLevel(WGPULogLevel_Trace);
#endif
}

int main() {

	setup_logging();

	// -----------------------------------------------------------------------------------------------------------------
	// WebGPU Instance

	Instance instance { Instance::create(WGPUInstanceDescriptor {
			.nextInChain = nullptr
	}) };

	// -----------------------------------------------------------------------------------------------------------------
	// GLFW

    if (!glfwInit()) { throw std::runtime_error("Could not initialize GLFW"); }

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	// window = glfwCreateWindow(appOptions.initialWidth, appOptions.initialHeight, appOptions.windowTitle.c_str(), NULL, NULL);
	auto window = glfwCreateWindow(640, 480, "wg", NULL, NULL);
	if (!window) {
		spdlog::get("wg")->critical("Could not open window.");
		throw std::runtime_error("Could not open window.");
	}

	Surface surface { glfwGetWGPUSurface(instance, window) };

	// -----------------------------------------------------------------------------------------------------------------
	// WebGPU Core Objects

	assert(instance.ptr != nullptr);

	Adapter adapter { instance.requestAdapter(WGPURequestAdapterOptions {
			.nextInChain = nullptr,
	})};

	Device device { adapter.requestDevice(WGPUDeviceDescriptor {
			.nextInChain = nullptr,
			.label = "dev10",
	})};

	Queue q { device.getQueue() };

	WGPUSurfaceConfiguration surfaceCfg;
	surfaceCfg.nextInChain = nullptr;
	surfaceCfg.device          = device;
	surfaceCfg.format          = WGPUTextureFormat_BGRA8Unorm;
	surfaceCfg.usage           = WGPUTextureUsage_RenderAttachment;
	surfaceCfg.width           = 640;
	surfaceCfg.height          = 480;
	surfaceCfg.presentMode     = WGPUPresentMode_Fifo;
	surfaceCfg.viewFormatCount = 0;
	surfaceCfg.viewFormats     = 0;
	surfaceCfg.alphaMode       = WGPUCompositeAlphaMode_Auto;
    surface.configure(surfaceCfg);

	// -----------------------------------------------------------------------------------------------------------------
	// WebGPU Rendering


	// auto surfTexView = 

	for (int i=0; i<100; i++) {

		auto surfTex = surface.getCurrentTexture();
		auto surfTexView = surfTex.texture.createView(WGPUTextureViewDescriptor {
				.nextInChain = nullptr,
				.format = WGPUTextureFormat_BGRA8Unorm,
				.dimension = WGPUTextureViewDimension_2D,
				.baseMipLevel = 0,
				.mipLevelCount = 1,
				.baseArrayLayer = 0,
				.arrayLayerCount = 1,
				.aspect = WGPUTextureAspect_All,
		});

		CommandEncoder cmdEncoder { device.create(WGPUCommandEncoderDescriptor {
			.nextInChain = nullptr,
			.label = "first",
		})};

		WGPURenderPassColorAttachment colorAttach {
						.nextInChain = nullptr,
						.view = surfTexView,
						.depthSlice = 0,
						.resolveTarget = nullptr,
						.loadOp = WGPULoadOp_Clear,
						.storeOp = WGPUStoreOp_Store,
						.clearValue = WGPUColor { .0f, .0f, .5f, .99f }
					};

		RenderPassEncoder rpe {
			cmdEncoder.beginRenderPass(WGPURenderPassDescriptor {
					.nextInChain = nullptr,
					.label = "firstRp",
					.colorAttachmentCount = 1,
					.colorAttachments = &colorAttach,
					.depthStencilAttachment = nullptr
			})
		};

		rpe.end();
		rpe.release();

		auto cmd = cmdEncoder.finish({nullptr, "cmdFinish1"});
		cmdEncoder.release();

		q.submit(1, &cmd);
		cmd.release();

		surface.present();

		usleep(33'000);
	}


	return 0;
}
