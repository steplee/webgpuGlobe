#include "app.h"
#include "entity/entity.h"
#include "entity/globe/globe.h"
#include "entity/fog/fog.h"
#include "entity/deferredCast/deferredCast.h"
#include "entity/primitive/primitive.h"
#include "entity/thickPrimitive/line.h"
#include "entity/thickPrimitive/point.h"
#include "entity/globe/cast.h"

#include "webgpuGlobe/geo/conversions.h"

#include <imgui.h>

namespace wg {
    namespace {
        struct SimpleApp : public App {

            inline SimpleApp(const AppOptions& opts)
                : App(opts) {

            }

            inline virtual void init() override {
                baseInit();

                spdlog::get("wg")->info("creating GlobeCamera.");
				// double p0[3] = {0, -4.5, 0};
				double p0[3] = {0, -2, 0};
				CameraIntrin intrin(appOptions.initialWidth, appOptions.initialHeight, 53.1 * M_PI / 180);
				// CameraIntrin intrin(appOptions.initialWidth, appOptions.initialHeight, 22 * M_PI / 180);
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
				// GlobeOptions gopts = parseArgs(appOptions.argv, appOptions.argc);
				GlobeOptions& gopts = appOptions.options;
				globe = make_tiff_globe(appObjects, gopts);
                spdlog::get("wg")->info("creating Globe... done");

                spdlog::get("wg")->info("creating Fog.");
				fog = std::make_shared<Fog>(appObjects, gopts, appOptions);
                spdlog::get("wg")->info("creating Fog... done");
                spdlog::get("wg")->info("creating DeferredCast.");
				deferredCast = std::make_shared<DeferredCast>(appObjects, gopts, appOptions);
                spdlog::get("wg")->info("creating DeferredCast... done");

                spdlog::get("wg")->info("creating Primitives.");
				prim1 = std::make_shared<PrimitiveEntity>();

				float prim1_verts[] = {
					 -77.122582, 38.992708, 2000,
					 -76.922582, 38.992708, 2000,
					 -76.922582, 38.792708, 2000,
					 -77.122582, 38.792708, 2000,
				};
				for (int i=0; i<4; i++) {
					prim1_verts[i*3+0] *= M_PI / 180;
					prim1_verts[i*3+1] *= M_PI / 180;
					prim1_verts[i*3+2] *= 1 / Earth::R1;
				}
				geodetic_to_ecef(prim1_verts, 4, prim1_verts);
				prim1->set(appObjects, PrimitiveData{
						.nverts = 4,
						.nindex = 0,
						.topo = WGPUPrimitiveTopology_LineStrip,
						.vertData = prim1_verts,
						.havePos = true
				});
				prim2 = std::make_shared<PrimitiveEntity>();
				primThick = std::make_shared<ThickLineEntity>();
				primThickPoints = std::make_shared<ThickPointEntity>();
                spdlog::get("wg")->info("creating Primitives... done");

            }

			inline void updatePrim2() {
				uint32_t prim2_inds[] = {0,1,2, 2,3,0};
				float prim2_verts[] = {
					 -77.122582, 38.992708, 2100,  1,0,0,.9f,
					 -76.922582, 38.992708, 2100,  0,1,0,.9f,
					 -76.922582, 38.792708, 2100,  0,1,0,.9f,
					 -77.122582, 38.792708, 2100,  0,1,1,.9f,
				};
				for (int i=0; i<4; i++) {
					prim2_verts[i*7+0] *= M_PI / 180;
					prim2_verts[i*7+1] *= M_PI / 180;
					prim2_verts[i*7+2] *= 1 / Earth::R1;
				}
				geodetic_to_ecef(prim2_verts, 4, prim2_verts, 7);
				static int cntr = 0;
				for (int i=0; i<4; i++) {
					prim2_verts[i*7+0] += cntr * 1.f / Earth::R1;
				}
				cntr++;

				prim2->set(appObjects, PrimitiveData{
						.nverts = 4,
						.nindex = 6,
						 // testing points & lines...
						.topo = (cntr / 32) % 4 != 0 ? WGPUPrimitiveTopology_LineStrip : WGPUPrimitiveTopology_PointList,
						.vertData = prim2_verts,
						.indexData = prim2_inds,
						.havePos = true,
						.haveColor = true
				});

				float thick_verts[4*8];
				for (int i=0; i<4; i++) {
					thick_verts[i*8+0] = prim2_verts[i*7+0];
					thick_verts[i*8+1] = prim2_verts[i*7+1];
					thick_verts[i*8+2] = prim2_verts[i*7+2];
					thick_verts[i*8+3] = 1.f + i*2.f;
					thick_verts[i*8+4] = prim2_verts[i*7+3];
					thick_verts[i*8+5] = prim2_verts[i*7+4];
					thick_verts[i*8+6] = prim2_verts[i*7+5];
					thick_verts[i*8+7] = prim2_verts[i*7+6];
				}
				primThick->set(appObjects, ThickLineData{
						.nverts = 4,
						// .topo = (cntr / 32) % 4 != 0 ? WGPUPrimitiveTopology_LineStrip : WGPUPrimitiveTopology_PointList,
						.topo = WGPUPrimitiveTopology_LineStrip,
						.vertData = thick_verts,
						.havePos = true,
						.haveColor = true
				});
				for (int i=0; i<4; i++) {
					thick_verts[i*8+3] = 4.f + i*2.f;
				}
				primThickPoints->set(appObjects, ThickPointData{
						.nverts = 4,
						.vertData = thick_verts,
						.havePos = true,
						.haveColor = true
				});

			}

			inline void updateCastStuff_(const float* mvp, int mask) {

				CastUpdate castUpdate;
				castUpdate.img = Image{};
				castUpdate.img->allocate(256,256,4);
				auto& img = *castUpdate.img;
				for (int y=0; y<256; y++) {
					for (int x=0; x<256; x++) {
						/*
						img.data()[y*256*4+x*4+0] = x;
						img.data()[y*256*4+x*4+1] = y;
						img.data()[y*256*4+x*4+2] = (x * 4) % 128 + (y * 4) % 128;
						img.data()[y*256*4+x*4+3] = 55;
						*/
						if (y == 128 or x == 128) {
							img.data()[y*256*4+x*4+0] = 155;
							img.data()[y*256*4+x*4+1] = 155;
							img.data()[y*256*4+x*4+2] = 155;
							img.data()[y*256*4+x*4+3] = 155;
						} else {
							img.data()[y*256*4+x*4+0] = 0;
							img.data()[y*256*4+x*4+1] = 0;
							img.data()[y*256*4+x*4+2] = 0;
							img.data()[y*256*4+x*4+3] = 0;
						}
					}
				}

				std::array<float,16> newCastMvp1;
				memcpy(newCastMvp1.data(), mvp, 16*4);
				castUpdate.castMvp1 = newCastMvp1;
				castUpdate.castColor1 = {{1.f,1.f,0.f,1.f}};


				// Vector3f p { 0.18549296, -0.7508647, 0.6417408 };
				// Vector3f p { 0.18158741, -0.76156366, 0.62079936 };
				Vector3f p { -77.034772*M_PI/180, 38.889463*M_PI/180, 40000/6e6 };
				geodetic_to_ecef(p.data(),1,p.data());
				float f[2] = {300, 300};
				float c[2] = {128,128};
				int wh[2] = {256,256};
				float near = 50 / 6e6;
				float far  = 50'000 / 6e6;

				Matrix<float,3,3,RowMajor> R;
				// Vector3f target = Vector3f::Zero();
				// Vector3f up = Vector3f::UnitZ();
				// lookAtR(R.data(), target.data(), p.data(), up.data());
				getEllipsoidalLtp(R.data(), p.data());

				R = R * Eigen::AngleAxisf(-180 * M_PI/180, -Vector3f::UnitX());
				Eigen::Matrix<double,3,3,RowMajor> Rd = R.cast<double>();
				Vector3d pd = p.cast<double>();
				wg::make_cast_matrix(newCastMvp1.data(), pd.data(), Rd.data(), f, c, wh, near, far);
				castUpdate.castMvp1 = newCastMvp1;




				castUpdate.mask = mask;

				// gpuResources.updateCastBindGroupAndResources(castUpdate);
				globe->updateCastStuff(castUpdate);
			}

			inline void updateCastStuff_2(const float* mvp) {
				DeferredCastData dcd;
				Vector3f p { -77.034772*M_PI/180, 38.889463*M_PI/180, 40000/6e6 };
				geodetic_to_ecef(p.data(),1,p.data());
				float f[2] = {300, 300};
				float c[2] = {128,128};
				int wh[2] = {256,256};
				float near = 50 / 6e6;
				float far  = 50'000 / 6e6;

				Matrix<float,3,3,RowMajor> R;
				// Vector3f target = Vector3f::Zero();
				// Vector3f up = Vector3f::UnitZ();
				// lookAtR(R.data(), target.data(), p.data(), up.data());
				getEllipsoidalLtp(R.data(), p.data());

				R = R * Eigen::AngleAxisf(-180 * M_PI/180, -Vector3f::UnitX());
				Eigen::Matrix<double,3,3,RowMajor> Rd = R.cast<double>();
				Vector3d pd = p.cast<double>();
				std::array<float,16> newCastMvp1;
				wg::make_cast_matrix(newCastMvp1.data(), pd.data(), Rd.data(), f, c, wh, near, far);
				memcpy(dcd.castMvp, newCastMvp1.data(), 4*4*4);

				Image img;
				img.allocate(256,256,4);
				for (int y=0; y<256; y++) {
					for (int x=0; x<256; x++) {
						if (y == 128 or x == 128) {
							img.data()[y*256*4+x*4+0] = 155;
							img.data()[y*256*4+x*4+1] = 155;
							img.data()[y*256*4+x*4+2] = 155;
							img.data()[y*256*4+x*4+3] = 155;
						} else {
							img.data()[y*256*4+x*4+0] = 0;
							img.data()[y*256*4+x*4+1] = 0;
							img.data()[y*256*4+x*4+2] = 0;
							img.data()[y*256*4+x*4+3] = 0;
						}
					}
				}

				dcd.castColor[0] = 1.f;
				dcd.castColor[1] = 1.f;
				dcd.castColor[2] = 1.f;
				dcd.castColor[3] = 1.f;

				deferredCast->setCastData(dcd);
				deferredCast->setCastTexture(img.data(), 256,256,4);


			}

			inline virtual void drawImgui() override {
				if (showImgui)
					ImGui::ShowDemoWindow();

				// Example: how to draw text using ImGUI.
				ImGui::Begin("full", nullptr, ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoDecoration);
				ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(100.f, 100.f), ImColor(255, 255, 0, 255), "Hello World", 0, 0.0f, 0);
				ImGui::End();
			}

            inline virtual void render() override {
                assert(entity);

				globeCamera->step(currentFrameData_->sceneData);


                SceneCameraData1 scd { globeCamera->lower(currentFrameData_->sceneData) };
				logger->info("eye {}, {}, {}", scd.eye[0], scd.eye[1], scd.eye[2]);

				// Write scene buf.
				auto &sceneBuf = globeCamera->sdr.buffer;
				appObjects.queue.writeBuffer(sceneBuf, 0, &scd, sizeof(decltype(scd)));


				if (castMove) {
					// WARNING: OFF
					// updateCastStuff_(scd.mvp, castMask);
				}


				if (0) {

					//
					// Render direct to screen. No fog.
					//

					auto rpe = currentFrameData_->commandEncoder.beginRenderPassForSurface(appObjects, *currentFrameData_);
					RenderState rs {
						// scd, currentFrameData_->commandEncoder, rpe, appObjects, *currentFrameData_,
						scd,
						globeCamera->intrin,
						currentFrameData_->commandEncoder, rpe, appObjects, *currentFrameData_,
					};

					sky->render(rs);
					globe->render(rs);
					entity2->render(rs);
					prim1->render(rs);
					updatePrim2();
					prim2->render(rs);
					primThick->render(rs);
					primThickPoints->render(rs);

					renderImguiFull(rpe);

					rpe.end();
					rpe.release();

				} else if (0) {

					//
					// Render to texture. Then add fog while rendering to screen.
					//

					{
						fog->beginPass(currentFrameData_->commandEncoder, getWindowSize().first, getWindowSize().second);
						RenderState rs {
							scd,
							globeCamera->intrin,
							currentFrameData_->commandEncoder, fog->rpe, appObjects, *currentFrameData_,
						};

						// sky->render(rs);
						globe->render(rs);
						entity2->render(rs);
						prim1->render(rs);
						updatePrim2();
						// prim2->render(rs);
						primThick->render(rs);
						primThickPoints->render(rs);
						fog->endPass();

					}
					
					{
						// logger->info("hae {}", scd.haeAlt);
						auto rpe2 = currentFrameData_->commandEncoder.beginRenderPassForSurface(appObjects, *currentFrameData_);
						RenderState rs { scd, globeCamera->intrin, currentFrameData_->commandEncoder, rpe2, appObjects, *currentFrameData_, };
						fog->renderAfterEndingPass(rs);

						renderImguiFull(rpe2);

						rpe2.end();
					}

				} else if (1) {
					
					updateCastStuff_2(scd.mvp);

					//
					// Render to texture. Then deferredCast while rendering to screen.
					//

					{
						deferredCast->beginPass(currentFrameData_->commandEncoder, getWindowSize().first, getWindowSize().second);
						RenderState rs {
							scd,
							globeCamera->intrin,
							currentFrameData_->commandEncoder, deferredCast->rpe, appObjects, *currentFrameData_,
						};

						// sky->render(rs);
						globe->render(rs);
						entity2->render(rs);
						prim1->render(rs);
						updatePrim2();
						// prim2->render(rs);
						primThick->render(rs);
						primThickPoints->render(rs);
						deferredCast->endPass();

					}
					
					{
						// logger->info("hae {}", scd.haeAlt);
						auto rpe2 = currentFrameData_->commandEncoder.beginRenderPassForSurface(appObjects, *currentFrameData_);
						RenderState rs { scd, globeCamera->intrin, currentFrameData_->commandEncoder, rpe2, appObjects, *currentFrameData_, };
						deferredCast->renderAfterEndingPass(rs);

						renderImguiFull(rpe2);

						rpe2.end();
					}
				}

            }

			inline virtual bool handleKey(int key, int scan, int act, int mod) override {
				if (key == GLFW_KEY_L and act == GLFW_PRESS) {
					globe->debugLevel = (globe->debugLevel + 1) % 4;
					logger->info("debugLevel {}", globe->debugLevel);
				}

				if (key == GLFW_KEY_C and act == GLFW_PRESS) {
					castMove = !castMove;
				}
				if (key == GLFW_KEY_V and act == GLFW_PRESS) {
					castMask = (castMask + 1) % 3;
				}
				if (key == GLFW_KEY_M and act == GLFW_PRESS) {
					showImgui = !showImgui;
				}

				return false;
			}

			inline virtual bool handleResize(int w, int h) override {
				if (globeCamera) globeCamera->intrin.updateSize_(w,h);
				return false;
			}

            // RenderPipeline pipeline;
            std::shared_ptr<Entity> entity;
            std::shared_ptr<Entity> entity2;
            std::shared_ptr<Entity> sky;
            std::shared_ptr<Globe> globe;
            std::shared_ptr<Fog> fog;
            std::shared_ptr<DeferredCast> deferredCast;

            std::shared_ptr<PrimitiveEntity> prim1;
            std::shared_ptr<PrimitiveEntity> prim2;
            std::shared_ptr<ThickLineEntity> primThick;
            std::shared_ptr<ThickPointEntity> primThickPoints;

            std::shared_ptr<GlobeCamera> globeCamera;

			int castMove = true;
			int castMask = 1;
			bool showImgui = true;
        };
    }

    std::shared_ptr<App> create_my_app(const AppOptions& appOpts) {
        return std::make_shared<SimpleApp>(appOpts);
    }
}
