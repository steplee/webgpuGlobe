#include "app.h"
#include "entity/entity.h"
#include "entity/globe/globe.h"

namespace wg {
    namespace {
        struct SimpleApp : public App {

            inline SimpleApp(const AppOptions& opts)
                : App(opts) {

            }

            inline virtual void init() override {
                baseInit();

                spdlog::get("wg")->info("creating GlobeCamera.");
				double p0[3] = {0, -4.5, 0};
				CameraIntrin intrin(appOptions.initialWidth, appOptions.initialHeight, 53.1 * M_PI / 180);
                globeCamera = std::make_shared<GlobeCamera>(intrin, appObjects, p0);
				setSceneBindGroupLayout(globeCamera->getBindGroupLayout());
				setSceneBindGroup(globeCamera->getBindGroup());
				ioListeners.push_back(globeCamera);

                spdlog::get("wg")->info("creating SimpleTri.");
                // entity = createSimpleTri(appObjects);
                entity = createSimpleTri2(appObjects);
                spdlog::get("wg")->info("creating Ellipsoid.");
                entity2 = createEllipsoid(appObjects, 32, 32);

                spdlog::get("wg")->info("creating Sky.");
				sky = createSky(appObjects);
                spdlog::get("wg")->info("creating Sky... done");

                spdlog::get("wg")->info("creating Globe.");
				GlobeOptions gopts = parseArgs(appOptions.argv, appOptions.argc);
				globe = make_tiff_globe(appObjects, gopts);
                spdlog::get("wg")->info("creating Globe... done");

            }

            inline virtual void render() override {
                assert(entity);

				globeCamera->step(currentFrameData_->sceneData);

                auto rpe = currentFrameData_->commandEncoder.beginRenderPassForSurface(appObjects, *currentFrameData_);

                SceneCameraData1 scd { globeCamera->lower(currentFrameData_->sceneData) };

				// Write scene buf.
				auto &sceneBuf = globeCamera->sdr.buffer;
				appObjects.queue.writeBuffer(sceneBuf, 0, &scd, sizeof(decltype(scd)));


                RenderState rs {
                    // scd, currentFrameData_->commandEncoder, rpe, appObjects, *currentFrameData_,
					scd,
					globeCamera->intrin,
                    currentFrameData_->commandEncoder, rpe, appObjects, *currentFrameData_,
                };

				sky->render(rs);
                entity->render(rs);
                entity2->render(rs);
                globe->render(rs);

                rpe.end();
            }

			inline virtual bool handleKey(int key, int scan, int act, int mod) override {
				if (key == GLFW_KEY_L and act == GLFW_PRESS) {
					globe->debugLevel = (globe->debugLevel + 1) % 4;
					logger->info("debugLevel {}", globe->debugLevel);
				}
				return false;
			}

            // RenderPipeline pipeline;
            std::shared_ptr<Entity> entity;
            std::shared_ptr<Entity> entity2;
            std::shared_ptr<Entity> sky;
            std::shared_ptr<Globe> globe;

            std::shared_ptr<GlobeCamera> globeCamera;
        };
    }

    std::shared_ptr<App> create_my_app(const AppOptions& appOpts) {
        return std::make_shared<SimpleApp>(appOpts);
    }
}
