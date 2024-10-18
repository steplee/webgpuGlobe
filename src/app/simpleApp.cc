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
				double p0[3] = {0, -1.5, 0};
				CameraIntrin intrin(appOptions.initialWidth, appOptions.initialHeight, 45 * 180 / M_PI);
                globeCamera = std::make_shared<GlobeCamera>(intrin, appObjects, p0);
				setSceneBindGroupLayout(globeCamera->getBindGroupLayout());
				setSceneBindGroup(globeCamera->getBindGroup());
				ioListeners.push_back(globeCamera);

                spdlog::get("wg")->info("creating SimpleTri.");
                // entity = createSimpleTri(appObjects);
                entity = createSimpleTri2(appObjects);
                spdlog::get("wg")->info("creating Ellipsoid.");
                entity2 = createEllipsoid(appObjects, 32, 32);

                spdlog::get("wg")->info("creating Globe.");
				globe = make_tiff_globe(appObjects, {});
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
                    currentFrameData_->commandEncoder, rpe, appObjects, *currentFrameData_,
                };

                entity->render(rs);
                entity2->render(rs);

                rpe.end();
            }

            // RenderPipeline pipeline;
            std::shared_ptr<Entity> entity;
            std::shared_ptr<Entity> entity2;
            std::shared_ptr<Entity> globe;

            std::shared_ptr<GlobeCamera> globeCamera;
        };
    }

    std::shared_ptr<App> create_my_app(const AppOptions& appOpts) {
        return std::make_shared<SimpleApp>(appOpts);
    }
}
