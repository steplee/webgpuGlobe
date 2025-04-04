#include "app.h"
#include "color_and_depth.h"
#include "webgpuGlobe/entity/entity.h"
#include "webgpuGlobe/camera/globe_camera.h"
#include "webgpuGlobe/camera/orthographic_camera.h"
#include "webgpuGlobe/entity/globe/globe.h"
#include "webgpuGlobe/entity/fog/fog.h"
#include "webgpuGlobe/entity/deferredCast/deferredCast.h"
#include "webgpuGlobe/entity/primitive/primitive.h"
#include "webgpuGlobe/entity/primitive/textured_primitive.h"
#include "webgpuGlobe/entity/primitive/instanced.h"
#include "webgpuGlobe/entity/thickPrimitive/line.h"
#include "webgpuGlobe/entity/thickPrimitive/point.h"
#include "webgpuGlobe/entity/globe/cast.h"

#include "webgpuGlobe/geo/conversions.h"

#include <imgui.h>
#include <opencv2/highgui.hpp>

// Super messy code, but I don't really care.

namespace wg {
    namespace {
        struct SimpleApp : public App {

			ColorAndDepthInfo cadi;

            inline SimpleApp(const AppOptions& opts)
                : App(opts) {

            }

            inline virtual void init() override {
                baseInit();

                spdlog::get("wg")->info("creating GlobeCamera.");
				// double p0[3] = {0, -4.5, 0};
				double p0[3] = {0, -2, 0};
				// CameraIntrin intrin(appOptions.initialWidth, appOptions.initialHeight, 22 * M_PI / 180);

				CameraIntrin intrin(appOptions.initialWidth, appOptions.initialHeight, 53.1 * M_PI / 180, 300/6e6, 9'000'000/6e6);
                globeCamera = std::make_shared<GlobeCamera>(intrin, appObjects, p0);

				// double R0[9] = {1,0,0, 0,0,-1, 0,1,0};
				// double R0[9] = {1,0,0, 0,0,1, 0,-1,0};
				// CameraIntrin intrin = CameraIntrin::ortho(appOptions.initialWidth,appOptions.initialHeight, -2,2, -2,2, .0001, 10);
                // globeCamera = std::make_shared<OrthographicCamera>(intrin, appObjects, p0, R0);

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

				try {
					globe1 = make_tiff_globe(appObjects, gopts);
					spdlog::get("wg")->info("creating tiff globe... done");
				} catch (std::runtime_error& ex) {
					spdlog::get("wg")->warn("Failed to create tiff globe. Will try google earth next");
					spdlog::get("wg")->warn("Original exception message: '{}'", ex.what());
				}

				try {
					globe2 = make_gearth_globe(appObjects, gopts);
					spdlog::get("wg")->info("creating gearth globe... done");
				} catch (std::runtime_error& ex) {
					spdlog::get("wg")->critical("Failed to create gearth globe. This is a fatal error: must be able to create tiff or google earth globe.");
					spdlog::get("wg")->warn("Original exception message: '{}'", ex.what());
				}

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
				primThickPoints2 = std::make_shared<ThickPointEntity>();
				instancedPrim = std::make_shared<InstancedPrimitiveEntity>();
                spdlog::get("wg")->info("creating Primitives... done");

				texPrim = std::make_shared<TexturedPrimitiveEntity>();

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

			inline void updatePrimTex() {
				uint32_t prim_inds[] = {0,1,2, 2,3,0};
				float prim_verts[] = {
					 -77.122582, 38.992708, 2100,  0, 0,   1,0,0,.9f,
					 -76.922582, 38.992708, 2100,  1, 0,   0,1,0,.9f,
					 -76.922582, 38.792708, 2100,  1, 1,   0,1,0,.9f,
					 -77.122582, 38.792708, 2100,  0, 1,   0,1,1,.9f,
				};
				for (int i=0; i<4; i++) {
					prim_verts[i*9+0] *= M_PI / 180;
					prim_verts[i*9+1] *= M_PI / 180;
					prim_verts[i*9+2] *= 1 / Earth::R1;
				}
				geodetic_to_ecef(prim_verts, 4, prim_verts, 9);
				static int cntr = 0;
				for (int i=0; i<4; i++) {
					prim_verts[i*9+0] += cntr * 1.f / Earth::R1;
				}
				cntr++;


				Image img;
				static int iter = 0;
				if (iter++ % 17 == 0) {
					img.allocate(256,256,4);
					for (int y=0; y<256; y++) {
						for (int x=0; x<256; x++) {
							for (int c=0; c<4; c++) {
								img.data()[y*256*4+x*4+0] = x;
								img.data()[y*256*4+x*4+1] = y;
								img.data()[y*256*4+x*4+2] = (iter+x+y) % 256;
								img.data()[y*256*4+x*4+3] = 128;
							}
						}
					}
				}

				texPrim->set(appObjects, TexturedPrimitiveData{
						.nverts = 4,
						.nindex = 6,
						.vertData = prim_verts,
						.indexData = prim_inds,
						.imgPtr = img.toPtr(),
						.havePos = true,
						.haveUv = true,
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
				if (globe1) globe1->updateCastStuff(castUpdate);
				if (globe2) globe2->updateCastStuff(castUpdate);
			}

			inline void updateCastStuff_2(const float* mvp) {
				DeferredCastData dcd;
				Vector3f p { -77.034772*M_PI/180, 38.889463*M_PI/180, 40000/6e6 };
				geodetic_to_ecef(p.data(),1,p.data());
				float f[2] = {300, 300};
				float c[2] = {128,128};
				int wh[2] = {256,256};
				float near = 250 / 6e6;
				float far  = 10'000 / 6e6;

				Matrix<float,3,3,RowMajor> R;
				// Vector3f target = Vector3f::Zero();
				// Vector3f up = Vector3f::UnitZ();
				// lookAtR(R.data(), target.data(), p.data(), up.data());
				getEllipsoidalLtp(R.data(), p.data());

				R = R * Eigen::AngleAxisf(-180 * M_PI/180, -Vector3f::UnitX());
				Eigen::Matrix<double,3,3,RowMajor> Rd = R.cast<double>();
				Vector3d pd = p.cast<double>();
				// pd -= Vector3d{.17, -.75, .62};
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

			inline void updateInstancedThing() {
				static bool set=false;

				// if (!set) {
				if (true) {
					set=true;

					std::vector<float> uboData;
					for (int i=0; i<4; i++) {
						for (int j=0; j<4; j++) {
							for (int k=0; k<4; k++) {
								float v = 0;
								if (j == k) v = 1; // eye
								if (j == 1 and k == 3) v = i*.01f; // y translation
								if (j == 3) v = (i+1)*.2f; // color white
								uboData.push_back(v);
							}
						}
					}

					uint32_t prim_inds[] = {0,1,2, 2,3,0};
					float prim_verts[] = {
						-77.122582, 38.992708, 4100,    1,0,0,.9f,
						-76.922582, 38.992708, 4100,    0,1,0,.9f,
						-76.922582, 38.792708, 4100,    0,1,0,.9f,
						-77.122582, 38.792708, 4100,    0,1,1,.9f,
					};
					for (int i=0; i<4; i++) {
						prim_verts[i*7+0] *= M_PI / 180;
						prim_verts[i*7+1] *= M_PI / 180;
						prim_verts[i*7+2] *= 1 / Earth::R1;
					}
					geodetic_to_ecef(prim_verts, 4, prim_verts, 7);
					static int cntr = 0;
					for (int i=0; i<4; i++) {
						// prim_verts[i*7+0] += cntr * 1.f / Earth::R1;
					}
					cntr++;

					instancedPrim->set(appObjects,InstancedPrimitiveData{
						.nverts = 4,
						.nindex = 6,
						.topo = WGPUPrimitiveTopology_TriangleList,
						.cullMode = WGPUCullMode_None,
						.vertData = prim_verts,
						.indexData = prim_inds,
						.havePos = true,
						.haveColor = true,
						.instancingUpdate=InstancingUpdate{
							.n = 4,
							.uboSizePerItem=16,
							.uboDataSize=uboData.size()*4,
							.uboData=uboData.data()
						}});
				}

			}

			inline void updateCameraTarget() {
				if (1) {
					globeCamera->clearTarget();
				} else {
					static int cntr = 0;
					double p[] = { -77.122582, 38.992708 + (cntr++)*.0001, 2000, };
					p[0] *= M_PI / 180;
					p[1] *= M_PI / 180;
					p[2] *= 1 / Earth::R1;
					geodetic_to_ecef(p, 1, p);
					Matrix<float,3,3,RowMajor> R;
					float pf[3] = {(float)p[0], (float)p[1], (float)p[2]};
					getEllipsoidalLtp(R.data(), pf);
					Quaterniond q { R.cast<double>() };
					// double q[] = {0,0,0,1};
					globeCamera->setTarget(cntr%32==0, p, q.coeffs().data());

					// Draw a point of what we're following to verify it looks good.
					float thick_verts[1*8];
					thick_verts[0] = pf[0];
					thick_verts[1] = pf[1];
					thick_verts[2] = pf[2];
					thick_verts[3] = 9.5f;
					thick_verts[4] = 1.0f;
					thick_verts[5] = 0.1f;
					thick_verts[6] = 1.0f;
					thick_verts[7] = 1.0f;
					primThickPoints2->set(appObjects, ThickPointData{
							.nverts = 1,
							.vertData = thick_verts,
							.havePos = true,
							.haveColor = true
					});
				}
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

				updateCameraTarget();

				globeCamera->step(currentFrameData_->sceneData);


                SceneCameraData1 scd { globeCamera->lower(currentFrameData_->sceneData) };
				logger->info("eye {}, {}, {}", scd.eye[0], scd.eye[1], scd.eye[2]);

				// Write scene buf.
				auto &sceneBuf = globeCamera->sdr.buffer;
				appObjects.queue.writeBuffer(sceneBuf, 0, &scd, sizeof(decltype(scd)));

				updateInstancedThing();


				if (castMove) {
					// WARNING: OFF
					updateCastStuff_(scd.mvp, castMask);
				}


				if (1) {

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
					if ((showGlobe & 1) and globe1) globe1->render(rs);
					if ((showGlobe & 2) and globe2) globe2->render(rs);
					entity2->render(rs);
					prim1->render(rs);
					updatePrim2();
					updatePrimTex();
					// prim2->render(rs);
					texPrim->render(rs);
					primThick->render(rs);
					primThickPoints->render(rs);
					primThickPoints2->render(rs);

					renderImguiFull(rpe);

					rpe.end();
					rpe.release();

					cadi.queueRead(appObjects, rs.camData.imvp, globeCamera->intrin, currentFrameData_->surfaceTex.texture, mainDepthTexture, rs.cmdEncoder);

					// NOTE: Shows OLD frame
					/*
					cadi.mapAndCopyToMat(appObjects);
					cv::imshow("color",cadi.color);
					cv::imshow("depth",cadi.depth);
					Vector3f ctr = cadi.accessUnitEcefPoint(globeCamera->intrin.w/2, globeCamera->intrin.h/2);
					logger->info("center at {} {} {}", ctr[0], ctr[1], ctr[2]);
					cv::waitKey(1);
					*/

				} else if (1) {

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
						if (globe1) globe1->render(rs);
						if (globe2) globe2->render(rs);
						entity2->render(rs);
						prim1->render(rs);
						updatePrim2();
						updatePrimTex();
						// prim2->render(rs);
						// texPrim->render(rs);
						primThick->render(rs);
						primThickPoints->render(rs);
						primThickPoints2->render(rs);
						instancedPrim->render(rs);
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
						if (globe1) globe1->render(rs);
						if (globe2) globe2->render(rs);
						entity2->render(rs);
						prim1->render(rs);
						updatePrim2();
						updatePrimTex();
						// prim2->render(rs);
						texPrim->render(rs);
						primThick->render(rs);
						primThickPoints->render(rs);
						primThickPoints2->render(rs);
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
					if (globe1) globe1->debugLevel = (globe1->debugLevel + 1) % 4;
					if (globe2) globe2->debugLevel = (globe2->debugLevel + 1) % 4;
					// logger->info("debugLevel {}", globe->debugLevel);
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
				if (key == GLFW_KEY_N and act == GLFW_PRESS) {
					showGlobe = (showGlobe + 1) % 3;
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
            std::shared_ptr<Globe> globe1;
            std::shared_ptr<Globe> globe2;
			int showGlobe = 3;
            std::shared_ptr<Fog> fog;
            std::shared_ptr<DeferredCast> deferredCast;

            std::shared_ptr<PrimitiveEntity> prim1;
            std::shared_ptr<PrimitiveEntity> prim2;
            std::shared_ptr<TexturedPrimitiveEntity> texPrim;
            std::shared_ptr<ThickLineEntity> primThick;
            std::shared_ptr<ThickPointEntity> primThickPoints;
            std::shared_ptr<ThickPointEntity> primThickPoints2;
            std::shared_ptr<InstancedPrimitiveEntity> instancedPrim;

            std::shared_ptr<GlobeCamera> globeCamera;
            // std::shared_ptr<OrthographicCamera> globeCamera;

			int castMove = true;
			int castMask = 1;
			bool showImgui = true;
        };
    }

    std::shared_ptr<App> create_my_app(const AppOptions& appOpts) {
        return std::make_shared<SimpleApp>(appOpts);
    }
}
