#include "app/app.h"

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
	spdlog::set_level(spdlog::level::trace);

#if defined(WGPU)
		wgpuSetLogCallback([](WGPULogLevel level, char const * message, void * userdata) {
				printf("wgpu %d: %s\n", level,message);
				// fflush(stdout);
				// spdlog::get("wgpu")->info("{}: {}", level, message);
		}, 0);
		if (getenv("WGPU_LOG")) {
			wgpuSetLogLevel(WGPULogLevel_Trace);
		} else {
			wgpuSetLogLevel(WGPULogLevel_Warn);
		}
#endif


}

int main(int argc, const char** argv) {

	setup_logging();

	AppOptions aopts;
	aopts.argv = argv;
	aopts.argc = argc;

	auto app = create_my_app(aopts);
	app->init();

	while (!app->shouldQuit()) {
		app->renderFrame();
		usleep(30'000);
	}
	// while (!app.shouldQuit()) usleep(100'000);

	return 0;
}
