#pragma once

#include <memory>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

#include "wrappers.hpp"
#include "webgpuGlobe/util/options.h"

namespace wg {

	using RenderPipelineCache = std::unordered_map<std::string, std::shared_ptr<RenderPipelineWithLayout>>;

    struct App;
    struct AppOptions;
    std::shared_ptr<App> create_my_app(const AppOptions&);

    struct IoListener {
		GLFWwindow* window;
		inline IoListener(GLFWwindow* window) : window(window) {}
        virtual ~IoListener() {}
        inline virtual bool handleResize(int w, int h) {
            return false;
        };
        inline virtual bool handleMouseMove(double x, double y) {
            return false;
        };
        inline virtual bool handleMouseButton(int btn, int act, int mod) {
            return false;
        };
        inline virtual bool handleScroll(double xoff, double yoff) {
            return false;
        };
        inline virtual bool handleKey(int key, int scan, int act, int mod) {
            return false;
        };
    };
	struct IoListenerWithState : public IoListener {

		bool leftClicked = false, rightClicked = false;
		bool shiftDown = false, ctrlDown = false, altDown = false;

		// bool keyDown[128] = {false};
		// bool keyWasDown[128] = {false};
		std::vector<bool> keyDown;
		std::vector<bool> keyWasDown;

		double mouseDx = 0;
		double mouseDy = 0;
		double lastMouseX = -1;
		double lastMouseY = -1;
		double lastLeftClickedMouseX = -1;
		double lastLeftClickedMouseY = -1;
		double lastRightClickedMouseX = -1;
		double lastRightClickedMouseY = -1;
		int windowWidth = 0, windowHeight = 0;

		IoListenerWithState(GLFWwindow* window);
        virtual bool handleResize(int w, int h) override;
        virtual bool handleMouseMove(double x, double y) override;
        virtual bool handleMouseButton(int btn, int act, int mod) override;
        virtual bool handleScroll(double xoff, double yoff) override;
        virtual bool handleKey(int key, int scan, int act, int mod) override;
	};

    struct AppObjects {
		GLFWwindow* window = nullptr;
        Instance instance;
        Adapter adapter;
        Device device;
        Queue queue;


        Surface surface;
        WGPUTextureFormat surfaceColorFormat        = WGPUTextureFormat_Undefined;
        WGPUTextureFormat surfaceDepthStencilFormat = WGPUTextureFormat_Undefined;

		BindGroupLayout *sceneBindGroupLayoutPtr = nullptr;
		inline BindGroupLayout& getSceneBindGroupLayout(bool required=true) {
			if (required) assert(sceneBindGroupLayoutPtr != nullptr);
			return *sceneBindGroupLayoutPtr;
		}

		BindGroup *sceneBindGroupPtr = nullptr;
		inline BindGroup& getSceneBindGroup(bool required=true) {
			if (required) assert(sceneBindGroupPtr != nullptr);
			return *sceneBindGroupPtr;
		}
		
		RenderPipelineCache renderPipelineCache;
    };

    struct AppOptions {
        int initialWidth = 1440, initialHeight = 960;
        std::string title, windowTitle;
        WGPUPresentMode presentMode = WGPUPresentMode_Fifo;
		bool headless = false;

		// const char** argv;
		// int argc;
		GlobeOptions options;
    };

    struct SceneData {
        float dt;
        float elapsedTime;
        int frameNumber;

        float sun[4];
        float haze;
		int wh[2];
    };

    struct FrameData {
        SurfaceTexture surfaceTex;
        TextureView surfaceTexView;
        TextureView surfaceDepthStencilView;
        CommandEncoder commandEncoder;
        SceneData sceneData;

        inline FrameData(SurfaceTexture&& surfaceTex, TextureView&& surfaceTexView, TextureView&& surfaceDepthStencilView,
                         CommandEncoder&& commandEncoder, const SceneData& sceneData)
            : surfaceTex(std::move(surfaceTex))
            , surfaceTexView(std::move(surfaceTexView))
            , surfaceDepthStencilView(std::move(surfaceDepthStencilView))
            , commandEncoder(std::move(commandEncoder))
            , sceneData(sceneData) {
        }
    };

    class App {
    public:
        inline App(const AppOptions& appOptions)
            : appOptions(appOptions) {
        }

        inline virtual ~App() {
        }

        bool shouldQuit() const;

        AppOptions appOptions;
        AppObjects appObjects;

        virtual void renderFrame();

        // User code goes here
        virtual void init() {
        }
		virtual void destroy();

    protected:
        std::shared_ptr<spdlog::logger> logger;

		inline void setSceneBindGroupLayout(BindGroupLayout& bgl) {
			if (appObjects.sceneBindGroupLayoutPtr != nullptr) assert(false && "sceneBindGroupLayoutPtr is already set.");
			appObjects.sceneBindGroupLayoutPtr = &bgl;
		}
		inline void setSceneBindGroup(BindGroup& bg) {
			if (appObjects.sceneBindGroupPtr != nullptr) assert(false && "sceneBindGroupPtr is already set.");
			appObjects.sceneBindGroupPtr = &bg;
		}

		std::pair<int,int> getWindowSize();

        virtual void baseInit();
        virtual void initWebgpu();
        virtual void initSurface(int w, int h);
        virtual void initHandlers();
        virtual void initImgui();

        virtual void beginFrame(); // Sets `currentFrameData_`
        virtual void render();
		void beginImguiFrame();
		void endImguiFrame(WGPURenderPassEncoder rpe);
        virtual void endFrame(); // Deletes `currentFrameData_`
        virtual void drawImgui(); // override to customize imgui
		void renderImguiFull(WGPURenderPassEncoder rpe); // the function to call to render imgui fully in one call.

        // Probably never should the user override.
        virtual void handleResize_(int w, int h);
        virtual void handleMouseMove_(double x, double y);
        virtual void handleMouseButton_(int btn, int act, int mod);
        virtual void handleScroll_(double xoff, double yoff);
        virtual void handleKey_(int key, int scan, int act, int mod);

        // User overridable.
        virtual bool handleResize(int w, int h);
        virtual bool handleMouseMove(double x, double y);
        virtual bool handleMouseButton(int btn, int act, int mod);
        virtual bool handleScroll(double xoff, double yoff);
        virtual bool handleKey(int key, int scan, int act, int mod);

        std::vector<std::shared_ptr<IoListener>> ioListeners;

        GLFWwindow* window = nullptr;

        std::unique_ptr<FrameData> currentFrameData_;
		int windowWidth, windowHeight;

		Texture mainDepthTexture;
		bool justChangedSize = false;
		void *imguiContext = nullptr;
    };

}
