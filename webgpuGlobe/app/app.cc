#include <webgpu/webgpu.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#define IMGUI_IMPL_WEBGPU_BACKEND_WGPU
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_wgpu.h>
#include <imgui.h>

#include "app.h"
#include "wrappers.hpp"

#warning "Every entity should allow to RE-CREATE it's pipeline with WHATEVER renderpass we give it (possibly storing old)"

namespace wg {

	void App::destroy() {
		currentFrameData_ = nullptr;
		mainDepthTexture = {};
		appObjects = {};
		glfwDestroyWindow(window);
	}

	std::pair<int,int> App::getWindowSize() {
		return {windowWidth,windowHeight};
	}

    void App::baseInit() {
        logger = spdlog::stdout_color_mt("app");
        spdlog::stdout_color_mt("wg");

        initWebgpu();
		auto wh = getWindowSize();
		initSurface(wh.first, wh.second);
        initHandlers();
        initImgui();
    }

    void App::initWebgpu() {

        appObjects.instance = Instance::create(WGPUInstanceDescriptor { .nextInChain = nullptr });
        if (!appObjects.instance) { throw std::runtime_error("Could not initialize WebGPU"); }

        if (!glfwInit()) { throw std::runtime_error("Could not initialize GLFW"); }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
		if (appOptions.headless)
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
		else
			glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
        window = glfwCreateWindow(appOptions.initialWidth, appOptions.initialHeight, appOptions.windowTitle.c_str(), NULL, NULL);
		// glfwGetWindowSize(window, &windowWidth, &windowHeight);
		glfwGetFramebufferSize(window, &windowWidth, &windowHeight);
		appObjects.window = window;
        if (!window) {
            logger->critical("Could not open window.");
            throw std::runtime_error("Could not open window.");
        }

        logger->info("Requesting surface...");
        appObjects.surface = Surface { glfwGetWGPUSurface(appObjects.instance, window) };

        logger->info("Requesting adapter...");
        WGPURequestAdapterOptions adapterOpts {};
        adapterOpts.nextInChain       = nullptr;
        adapterOpts.compatibleSurface = appObjects.surface;
        adapterOpts.powerPreference = WGPUPowerPreference_Undefined;
        adapterOpts.backendType = WGPUBackendType_Undefined;
        adapterOpts.forceFallbackAdapter = false;
        appObjects.adapter            = appObjects.instance.requestAdapter(adapterOpts);
		assert(appObjects.adapter.ptr != nullptr);

        WGPUSurfaceCapabilities caps;
        // appObjects.surface.getCapabilities(adapter, &caps);
        wgpuSurfaceGetCapabilities(appObjects.surface, appObjects.adapter, &caps);
        assert(caps.formatCount > 0);
        // surfaceInfo.targetTextureFormat = caps.formats[0];
        for (int i = 0; i < caps.formatCount; i++) logger->info("available surface format: {}", (int)caps.formats[i]);
        appObjects.surfaceColorFormat = WGPUTextureFormat_BGRA8Unorm;
        // TODO: Assert we support bgra8 or use preferred...

        WGPUSupportedLimits supportedLimits;
		supportedLimits.nextInChain = nullptr;
        wgpuAdapterGetLimits(appObjects.adapter.ptr, &supportedLimits);
		logger->info("");
				
		auto &l = supportedLimits.limits;
		logger->info("SupportedLimits:");
		logger->info(" - maxTextureDimension1D: {}", l.maxTextureDimension1D);
		logger->info(" - maxTextureDimension2D: {}", l.maxTextureDimension2D);
		logger->info(" - maxTextureDimension3D: {}", l.maxTextureDimension3D);
		logger->info(" - maxTextureArrayLayers: {}", l.maxTextureArrayLayers);
		logger->info(" - maxBindGroups: {}", l.maxBindGroups);
		logger->info(" - maxBindGroupsPlusVertexBuffers: {}", l.maxBindGroupsPlusVertexBuffers);
		logger->info(" - maxBindingsPerBindGroup: {}", l.maxBindingsPerBindGroup);
		logger->info(" - maxDynamicUniformBuffersPerPipelineLayout: {}", l.maxDynamicUniformBuffersPerPipelineLayout);
		logger->info(" - maxDynamicStorageBuffersPerPipelineLayout: {}", l.maxDynamicStorageBuffersPerPipelineLayout);
		logger->info(" - maxSampledTexturesPerShaderStage: {}", l.maxSampledTexturesPerShaderStage);
		logger->info(" - maxSamplersPerShaderStage: {}", l.maxSamplersPerShaderStage);
		logger->info(" - maxStorageBuffersPerShaderStage: {}", l.maxStorageBuffersPerShaderStage);
		logger->info(" - maxStorageTexturesPerShaderStage: {}", l.maxStorageTexturesPerShaderStage);
		logger->info(" - maxUniformBuffersPerShaderStage: {}", l.maxUniformBuffersPerShaderStage);
		logger->info(" - maxUniformBufferBindingSize: {}", l.maxUniformBufferBindingSize);
		logger->info(" - maxStorageBufferBindingSize: {}", l.maxStorageBufferBindingSize);
		logger->info(" - minUniformBufferOffsetAlignment: {}", l.minUniformBufferOffsetAlignment);
		logger->info(" - minStorageBufferOffsetAlignment: {}", l.minStorageBufferOffsetAlignment);
		logger->info(" - maxVertexBuffers: {}", l.maxVertexBuffers);
		logger->info(" - maxBufferSize: {}", l.maxBufferSize);
		logger->info(" - maxVertexAttributes: {}", l.maxVertexAttributes);
		logger->info(" - maxVertexBufferArrayStride: {}", l.maxVertexBufferArrayStride);
		logger->info(" - maxInterStageShaderComponents: {}", l.maxInterStageShaderComponents);
		logger->info(" - maxInterStageShaderVariables: {}", l.maxInterStageShaderVariables);
		logger->info(" - maxColorAttachments: {}", l.maxColorAttachments);
		logger->info(" - maxColorAttachmentBytesPerSample: {}", l.maxColorAttachmentBytesPerSample);
		logger->info(" - maxComputeWorkgroupStorageSize: {}", l.maxComputeWorkgroupStorageSize);
		logger->info(" - maxComputeInvocationsPerWorkgroup: {}", l.maxComputeInvocationsPerWorkgroup);
		logger->info(" - maxComputeWorkgroupSizeX: {}", l.maxComputeWorkgroupSizeX);
		logger->info(" - maxComputeWorkgroupSizeY: {}", l.maxComputeWorkgroupSizeY);
		logger->info(" - maxComputeWorkgroupSizeZ: {}", l.maxComputeWorkgroupSizeZ);
		logger->info(" - maxComputeWorkgroupsPerDimension: {}", l.maxComputeWorkgroupsPerDimension);

        WGPURequiredLimits requiredLimits                      = defaultRequiredLimits();
        requiredLimits.limits.maxTextureDimension2D            = 2048;
        requiredLimits.limits.maxTextureArrayLayers            = 1024;
        requiredLimits.limits.maxSamplersPerShaderStage        = 1;
        requiredLimits.limits.maxSampledTexturesPerShaderStage = 4;
        requiredLimits.limits.maxUniformBuffersPerShaderStage  = 2;
        requiredLimits.limits.maxVertexAttributes              = 4;
        requiredLimits.limits.maxVertexBuffers                 = 2;
        requiredLimits.limits.minStorageBufferOffsetAlignment  = supportedLimits.limits.minStorageBufferOffsetAlignment;
        requiredLimits.limits.minUniformBufferOffsetAlignment  = supportedLimits.limits.minUniformBufferOffsetAlignment;

        WGPUDeviceDescriptor deviceDesc;
        deviceDesc.nextInChain          = nullptr;
        deviceDesc.label                = "MyDevice";
        deviceDesc.requiredFeatureCount = 0;
        deviceDesc.requiredLimits       = &requiredLimits;
        deviceDesc.defaultQueue.label   = "MyQueue";
        deviceDesc.deviceLostCallback = [](WGPUDeviceLostReason reason, char const* msg, void* userdata) {
			static_cast<spdlog::logger*>(userdata)->warn("WebGPU device lost (reason {}) msg: '{}'", (int)reason, msg);
		};
        deviceDesc.deviceLostUserdata = logger.get();


#ifndef __EMSCRIPTEN__
        deviceDesc.uncapturedErrorCallbackInfo = WGPUUncapturedErrorCallbackInfo{
			.nextInChain = nullptr,
			.callback = [](WGPUErrorType type, char const* msg, void* userdata) {
				static_cast<spdlog::logger*>(userdata)->critical("WebGPU uncaptured error (type {}) msg: '{}'", (int)type, msg);
				// if (strstr(msg, "In a set_viewport command") == nullptr) {
					// assert(false);
				// }
			},
			.userdata = logger.get()
		};
#endif

        appObjects.device               = appObjects.adapter.requestDevice(deviceDesc);
        logger->info("Got device.");

        appObjects.queue = appObjects.device.getQueue();
        logger->info("Got queue.");

	}

	void App::initSurface(int w, int h) {
        WGPUSurfaceConfiguration surfaceCfg;
        surfaceCfg.nextInChain = nullptr;
        surfaceCfg.device      = appObjects.device;
        surfaceCfg.format      = appObjects.surfaceColorFormat;
        surfaceCfg.usage       = WGPUTextureUsage_RenderAttachment;
        surfaceCfg.width       = w;
        surfaceCfg.height      = h;
        surfaceCfg.presentMode = appOptions.presentMode;
        // surfaceCfg.presentMode     = PresentMode::Immediate;

        // surfaceCfg.viewFormatCount = 1;
        // surfaceCfg.viewFormats     = (WGPUTextureFormat*)&surfaceInfo.targetTextureFormat;
        surfaceCfg.viewFormatCount = 0;
        surfaceCfg.viewFormats     = 0;

        surfaceCfg.alphaMode       = WGPUCompositeAlphaMode_Auto;
        appObjects.surface.configure(surfaceCfg);
        logger->info("Configured surface with size {}x{}.", w, h);

		appObjects.surfaceDepthStencilFormat = WGPUTextureFormat_Depth32Float;
		mainDepthTexture = appObjects.device.createDepthTexture(w,h, appObjects.surfaceDepthStencilFormat);
        logger->info("Created `mainDepthTexture`.");
    }

    void App::initImgui() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO();

        // Setup Platform/Renderer backends
        ImGui_ImplGlfw_InitForOther(window, true);
        // ImGui_ImplWGPU_Init(device, 3, swapChainFormat, depthTextureFormat);
        ImGui_ImplWGPU_InitInfo info;
        info.Device             = appObjects.device;
        info.NumFramesInFlight  = 3;
        info.RenderTargetFormat = appObjects.surfaceColorFormat;
        info.DepthStencilFormat = appObjects.surfaceDepthStencilFormat;
        ImGui_ImplWGPU_Init(&info);
    }

    void App::beginFrame() {


        auto surfTex     = appObjects.surface.getCurrentTexture();

		/*
#if 0
		int w,h;
		// glfwGetWindowSize(window, &w, &h);
		glfwGetFramebufferSize(window, &w, &h);
#else
		int w = surfTex.texture.getWidth();
		int h = surfTex.texture.getHeight();
#endif
		if (windowWidth != w or windowHeight != h) {
			windowWidth = w;
			windowHeight = h;
			initSurface(w,h);
			surfTex     = appObjects.surface.getCurrentTexture();
		}
		*/

		{
			int w = surfTex.texture.getWidth();
			int h = surfTex.texture.getHeight();
			spdlog::get("wg")->info("surface size {} {}", w, h);
		}

        auto surfTexView = surfTex.texture.createView(WGPUTextureViewDescriptor {
            .nextInChain     = nullptr,
            .label           = "surfTexView",
            .format          = WGPUTextureFormat_BGRA8Unorm,
            .dimension       = WGPUTextureViewDimension_2D,
            .baseMipLevel    = 0,
            .mipLevelCount   = 1,
            .baseArrayLayer  = 0,
            .arrayLayerCount = 1,
            .aspect          = WGPUTextureAspect_All,
        });

        SceneData sceneData {
            .dt = .033, .elapsedTime = 0, .frameNumber = 0, .sun = { 0, 0, 0, 0 },
                       .haze = 0, .wh={windowWidth, windowHeight}
        };


		TextureView dsView;
		if (appObjects.surfaceDepthStencilFormat != WGPUTextureFormat_Undefined) {
			dsView = mainDepthTexture.createView(WGPUTextureViewDescriptor {
            .nextInChain     = nullptr,
            .label           = "dsTexView",
            .format          = WGPUTextureFormat_Depth32Float,
            .dimension       = WGPUTextureViewDimension_2D,
            .baseMipLevel    = 0,
            .mipLevelCount   = 1,
            .baseArrayLayer  = 0,
            .arrayLayerCount = 1,
            .aspect          = WGPUTextureAspect_All,
			});
		}
        logger->trace("Created view of `mainDepthTexture` as `frameData.surfaceDepthStencilView`.");

        currentFrameData_ = std::make_unique<FrameData>(
            std::move(surfTex), std::move(surfTexView), std::move(dsView),
            appObjects.device.create(WGPUCommandEncoderDescriptor { .nextInChain = nullptr, .label = "beginFrame" }), sceneData);
    }

    void App::renderFrame() {
        beginFrame();


        render();
        endFrame();
    }

    void App::render() {
    }

    void App::drawImgui() {
    }

    void App::endFrame() {

		// logger->info("|time| app end frame submit");
        auto cmdBuf = currentFrameData_->commandEncoder.finish("endFrame");
        appObjects.queue.submit(1, &cmdBuf);

#ifndef __EMSCRIPTEN__
        appObjects.surface.present();
#endif

#if defined(WEBGPU_BACKEND_DAWN)
        device.tick();
#elif defined(WEBGPU_BACKEND_WGPU)
        device.poll(false);
#endif

        // Release the surface's current texture + view
        currentFrameData_ = nullptr;

		// logger->info("|time| app end frame tick");
		justChangedSize = false;
        glfwPollEvents();
		// logger->info("|time| app end frame glfw poll");
    }

    void App::initHandlers() {
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, [](GLFWwindow* window, int w, int h) {
        // glfwSetWindowSizeCallback(window, [](GLFWwindow* window, int w, int h) {
            auto that = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));
            if (that == nullptr) return;
			spdlog::get("wg")->info("Resize {} {}", w,h);
            that->handleResize_(w, h);
        });
        glfwSetCursorPosCallback(window, [](GLFWwindow* window, double xpos, double ypos) {
            auto that = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));
            if (that == nullptr) return;
			ImGuiIO& io = ImGui::GetIO();
			if (io.WantCaptureMouse) return;
            that->handleMouseMove_(xpos, ypos);
        });
        glfwSetMouseButtonCallback(window, [](GLFWwindow* window, int button, int action, int mods) {
            auto that = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));
            if (that == nullptr) return;
			ImGuiIO& io = ImGui::GetIO();
			if (io.WantCaptureMouse) return;
            that->handleMouseButton_(button, action, mods);
        });
        glfwSetScrollCallback(window, [](GLFWwindow* window, double xoffset, double yoffset) {
            auto that = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));
            if (that == nullptr) return;
            that->handleMouseMove_(xoffset, yoffset);
        });
        glfwSetKeyCallback(window, [](GLFWwindow* window, int key, int scan, int act, int mods) {
            auto that = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));
            if (that == nullptr) return;
			ImGuiIO& io = ImGui::GetIO();
			if (io.WantCaptureKeyboard) return;
            that->handleKey_(key, scan, act, mods);
        });

		// ImGui_ImplGlfw_InstallCallbacks(window);
    }

    void App::handleResize_(int w, int h) {
		windowWidth = w;
		windowHeight = h;
		justChangedSize = true;
		initSurface(w,h);
        if (handleResize(w, h)) return;
        for (auto& l : ioListeners)
            if (l->handleResize(w, h)) return;
    }
    void App::handleMouseMove_(double x, double y) {
        if (handleMouseMove(x, y)) return;
        for (auto& l : ioListeners)
            if (l->handleMouseMove(x, y)) return;
    }
    void App::handleMouseButton_(int btn, int act, int mod) {
        if (handleMouseButton(btn, act, mod)) return;
        for (auto& l : ioListeners)
            if (l->handleMouseButton(btn, act, mod)) return;
    }
    void App::handleScroll_(double xoff, double yoff) {
        if (handleScroll(xoff, yoff)) return;
        for (auto& l : ioListeners)
            if (l->handleScroll(xoff, yoff)) return;
    }
    void App::handleKey_(int key, int scan, int act, int mod) {
        if (handleKey(key, scan, act, mod)) return;
        for (auto& l : ioListeners)
            if (l->handleKey(key, scan, act, mod)) return;
    }

    bool App::handleResize(int w, int h) {
        return false;
    }
    bool App::handleMouseMove(double x, double y) {
        return false;
    }
    bool App::handleMouseButton(int btn, int act, int mod) {
        return false;
    }
    bool App::handleScroll(double xoff, double yoff) {
        return false;
    }
    bool App::handleKey(int key, int scan, int act, int mod) {
        logger->info("key: {}, scan: {} scan, act: {}, mod: {}", key, scan, act, mod);
        if (key == GLFW_KEY_Q) glfwSetWindowShouldClose(window, true);

        return false;
    }

	void App::beginImguiFrame() {
		ImGui_ImplWGPU_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
	}
	void App::endImguiFrame(WGPURenderPassEncoder rpe) {
		ImGui::Render();
		ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), rpe);
	}

	void App::renderImguiFull(WGPURenderPassEncoder rpe) {
		// WARNING: ImGUI will can cause a crash if the window resizes *inside* a render loop iteration,
		//          because glfwPollEvents is not run until after.
		//          This would throw a validation error, which may not necessarily halt the program.
		//          But it does if we assert(false) in the uncaptured error handler above.
		//          So just prevent it from happening by making sure window size has not changed.
		int ww,hh;
		glfwGetFramebufferSize(window, &ww, &hh);
		if (windowWidth == ww and windowHeight == hh) {
			beginImguiFrame();
			drawImgui();
			endImguiFrame(rpe);
		}
	}

    bool App::shouldQuit() const {
        return glfwWindowShouldClose(window);
    }

	// ---------------------------------------------------------------
    // IoListenerWithState
	// ---------------------------------------------------------------

    IoListenerWithState::IoListenerWithState(GLFWwindow* window)
        : IoListener(window) {
        glfwGetFramebufferSize(window, &windowWidth, &windowHeight);
		keyDown.resize(GLFW_KEY_LAST, false);
		// keyWasDown.resize(GLFW_KEY_LAST, false);
    }
    bool IoListenerWithState::handleResize(int w, int h) {
        windowWidth  = w;
        windowHeight = h;
        return false;
    }
    bool IoListenerWithState::handleMouseMove(double x, double y) {
		mouseDx = x - lastMouseX;
		mouseDy = y - lastMouseY;
		if (lastMouseX < 0 or lastMouseY < 0) mouseDx = mouseDy = 0;
		lastMouseX = x;
		lastMouseY = y;
        return false;
    }
    bool IoListenerWithState::handleMouseButton(int btn, int act, int mod) {
		if (act == GLFW_PRESS) {
			double x=-1, y=-1;
			glfwGetCursorPos(window, &x, &y);
			if (btn == GLFW_MOUSE_BUTTON_LEFT) {
				leftClicked = true;
				lastLeftClickedMouseX = x;
				lastLeftClickedMouseY = y;
			}
			if (btn == GLFW_MOUSE_BUTTON_RIGHT) {
				rightClicked = true;
				lastRightClickedMouseX = x;
				lastRightClickedMouseY = y;
			}
		} else if (act == GLFW_RELEASE) {
			if (btn == GLFW_MOUSE_BUTTON_LEFT) {
				leftClicked = false;
				lastLeftClickedMouseX = -1;
				lastLeftClickedMouseY = -1;
			}
			if (btn == GLFW_MOUSE_BUTTON_RIGHT) {
				rightClicked = false;
				lastRightClickedMouseX = -1;
				lastRightClickedMouseY = -1;
			}
		}
        return false;
    }

    bool IoListenerWithState::handleScroll(double xoff, double yoff) {
        return false;
    }
    bool IoListenerWithState::handleKey(int key, int scan, int act, int mod) {
		if (act == GLFW_PRESS) {
			keyDown[key] = true;
			// keyWasDown[key] = true;
		} else if (act == GLFW_RELEASE) {
			keyDown[key] = false;
		}
        return false;
    }

}
