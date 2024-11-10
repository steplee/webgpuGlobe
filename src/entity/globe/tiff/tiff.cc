#include "app/shader.h"
#include "entity/globe/globe.h"
#include "entity/globe/quadtree.h"
#include "entity/globe/webgpu_utils.hpp"
#include "tiff.h"
#include "dataloader.hpp"

#include "gpu/resources.h"

#include "util/gdalDataset.h"
#include "util/fmtEigen.h"
#include "geo/conversions.h"


#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <condition_variable>
#include <mutex>
#include <thread>

#include <unistd.h>


namespace wg {
enum class TileState { OpeningChildrenAsParent, OpeningAsChild, OpeningAsParent, ClosingToParent, SteadyLeaf, SteadyInterior, SteadyLeafWantsToClose };
}


template <> struct fmt::formatter<wg::TileState>: formatter<string_view> {
  // parse is inherited from formatter<string_view>.

  auto format(wg::TileState c, format_context& ctx) const
    -> format_context::iterator {
		using namespace wg;
		if (c == TileState::OpeningAsParent) fmt::format_to(ctx.out(), "OpeningAsParent");
		if (c == TileState::OpeningAsChild) fmt::format_to(ctx.out(), "OpeningAsChild");
		if (c == TileState::ClosingToParent) fmt::format_to(ctx.out(), "ClosingToParent");
		if (c == TileState::OpeningChildrenAsParent) fmt::format_to(ctx.out(), "OpeningChildrenAsParent");
		if (c == TileState::SteadyLeaf) fmt::format_to(ctx.out(), "SteadyLeaf");
		if (c == TileState::SteadyInterior) fmt::format_to(ctx.out(), "SteadyInterior");
		if (c == TileState::SteadyLeafWantsToClose) fmt::format_to(ctx.out(), "SteadyLeafWantsToClose");
		return fmt::format_to(ctx.out(), "");
	}
};

// #define logTrace(...) spdlog::get("tiffRndr")->trace( __VA_ARGS__ );
#define logTrace(...) {};


namespace wg {
namespace tiff {



	using tiff::TileData;
	using tiff::GpuTileData;
	using UpdateState = TiffDataLoader::UpdateState;
	using LoadDataRequest = TiffDataLoader::LoadDataRequest;
	using LoadDataResponse = TiffDataLoader::LoadDataResponse;


	/*
    enum class LoadAction { LoadRoot, OpenChildren, CloseToParent };

	struct Tile;
    struct LoadDataRequest {
        Tile* src; // where this request was initiated from.
        int32_t seq;

        // When opening: the parent coordinate of which the four children shall be loaded.
        // When closing: the parent coordinate to load data for.
        QuadtreeCoordinate parentCoord;
        LoadAction action;
    };

    struct LoadDataResponse {
        int32_t seq;
        Tile* src;
        QuadtreeCoordinate parentCoord;
        LoadAction action;
        std::vector<TileData> items;
    };

	struct UpdateState {
		Matrix4f mvp;
		Vector3f eye;
		float tanHalfFovTimesHeight;

		std::vector<LoadDataRequest> requests;
		int32_t seq = 0;
	};
	*/




    void maybe_make_tiff_obb_file(const std::string& tiffPath, const GlobeOptions& gopts);

    // NOTE:
    // Just wrote code for 2d texture array of layers.
    // But we need to let each draw call know what index of the array to use.
    // WebGPU does not have push constant.
    // Can I do this with `firstInstance` and reading the instance id in shader?
    // This could be done in batch fashion too with one draw call using drawIndirect






    // Remember that we must model the states corresponding to all interior nodes as well as leaves.
    // Then, a tile can be:
    //
    //       opening_children_as_parent      |
    //       closing                         | Leaf
    //       steady                          | States
    //       steady_wants_to_close           |
    //
    //       opening_as_child                | Unloaded
    //       opening_as_parent               | States
    //
    //       root                            | Root/Interior
    //       interior                        | States
    //
    // Note that `SteadyLeafWantsToClose` is different from `Steady` and from `Closing` because a tile cannot close until
    // all other 3 sibiling also want to.
    // Not until all four children enter the `SteadyLeafWantsToClose` state, will they all then be transferred to the `Closing`
    // state and the `LoadRequest` queued.
    //
    // When a parent _decides_ to open (entering the `opening_children_as_parent`),
    // it will allocate all children objects -- of course they will not have data loaded
    // yet.
    //
    // When a parent _decides_ to open, it also shall "take" a resource index from
    // the `GpuResources` free list.
    // But is this indeed how it ought to work?
    //
    // Once a tile is in a non-steady state, it cannot goto any other state until
    // the data is loaded.
    // So if we zoom in (requiring opening some node), then zoom out before it has loaded,
    // it cannot undo the opening state / load request. It must wait until the data is loaded,
    // then immediately unload.
    //
    // #error "tile state should NOT be an enum -- better expressed with multiple fields."

    //
    // NOTE: Well maybe not -- can just store 'root' field in tile obj
    /*
    struct TileState {
            bool openingAsChild;
            bool openingAsParent;
            bool root;
    };
    */



    struct Tile;


	// struct DataLoader;

    struct Tile {

        inline Tile(const QuadtreeCoordinate& coord, Tile* parent, TileState state, const UnpackedOrientedBoundingBox& obb)
            : coord(coord)
            , parent(parent)
            , state(state)
            , obb(obb)
			, sse(0)
		{
			for (int i=0; i<4; i++) children[i] = nullptr;
        }

        QuadtreeCoordinate coord;

        TileState state;

        Tile* parent                  = nullptr;
        std::array<Tile*, 4> children = { nullptr };
        int nchildren                 = 0;

        UnpackedOrientedBoundingBox obb;
		float sse;

        GpuTileData gpuTileData;

		const float sseOpenThresh = 4.f;

        inline void update(const RenderState& rs, GpuResources& res, UpdateState& updateState) {
            // If leaf:
            //    compute sse
            //    if   sse < closeThresh: goto SteadyLeafWantsToClose
            //    elif sse > openThresh : open()
			//    else goto SteadyLeaf
			// If interior:
			//    compute sse
			//    if all children want to close and not isRoot:
			//        queue load parent, goto OpeningAsParent, set children state Closing

			// NOTE: isSteadyLeaf() is true if `wantsToClose`, BUT not if already initiated closing `ClosingToParent`
			if (isSteadyLeaf()) {
				sse = obb.computeSse(updateState.mvp, updateState.eye, updateState.tanHalfFovTimesHeight);

				if (sse > sseOpenThresh or sse == kBoundingBoxContainsEye) {
					if (isTerminal()) {
						spdlog::get("tiffRndr")->info("cannot open a terminal node");
						state = TileState::SteadyLeaf;
					} else {
						state = TileState::OpeningChildrenAsParent;
						spdlog::get("tiffRndr")->info("push OpenChildren request at {} from sse {:>.2f}", coord, sse);
						updateState.requests.push_back(LoadDataRequest{
								.src = this,
								.seq = updateState.seq++,
								.parentCoord = coord,
								.action = LoadAction::OpenChildren
								});
					}
				} else if ((sse >= 0 and sse < .7f) or sse == kBoundingBoxNotVisible) {
					if (isRoot()) {
						spdlog::get("tiffRndr")->info("cannot close a root");
						state = TileState::SteadyLeaf;
					} else {
						state = TileState::SteadyLeafWantsToClose;
					}
				} else {
					state = TileState::SteadyLeaf;
				}
			}

			// else if (isInterior()) {
			else if (state == TileState::SteadyInterior) {
				assert(nchildren > 0);

				for (int i=0; i<nchildren; i++) children[i]->update(rs, res, updateState);

				bool allChildrenWantClose = true;
				// for (auto& c : children) if (c->state != TileState::SteadyLeafWantsToClose) allChildrenWantClose = false;
				for (int i=0; i<nchildren; i++) if (children[i]->state != TileState::SteadyLeafWantsToClose) allChildrenWantClose = false;

				if (allChildrenWantClose) {

					sse = obb.computeSse(updateState.mvp, updateState.eye, updateState.tanHalfFovTimesHeight);

					// WARNING: Why is this necessary? Is there a bug with sse computation?
					if (sse > sseOpenThresh) {
						spdlog::get("tiffRndr")->info("not ClosingToParent {} because parent sse is too high {:>.2f}", coord, sse);
					} else {
						spdlog::get("tiffRndr")->info("push CloseToParent request at {} (my sse {:.2f})", coord, sse);
						updateState.requests.push_back(LoadDataRequest{
								.src = this,
								.seq = updateState.seq++,
								.parentCoord = coord,
								.action = LoadAction::CloseToParent
								});

						state = TileState::OpeningAsParent;
						for (int i=0; i<nchildren; i++) children[i]->state = TileState::ClosingToParent;
						
						spdlog::get("tiffRndr")->info("parent {} going from SteadyInterior -> OpeningAsParent", coord);
					}
				}
			}

        }

        inline void recvOpenLoadedData(LoadDataResponse&& resp, GpuResources& res, TiffObbMap& obbMap) {
            if (resp.action == LoadAction::OpenChildren) {
                assert(state == TileState::OpeningChildrenAsParent);
                assert(resp.parentCoord == coord);
				// Allocate children and load the data.

				// int32_t seq;
				// Tile* src;
				// QuadtreeCoordinate parentCoord;
				// LoadAction action;
				// std::vector<TileData> items;
				// assert(resp.items.size() == 4);
                spdlog::get("tiffRndr")->info("recv open {} children data for {}", resp.items.size(), resp.parentCoord);
				nchildren = resp.items.size();
				for (int i=0; i<resp.items.size(); i++) {
					assert(children[i] == nullptr);
					auto childCoord = resp.items[i].coord;
					children[i] = new Tile(childCoord, this, TileState::SteadyLeaf, obbMap.map[childCoord]);

					children[i]->loadFrom(resp.items[i], res);
				}

				state = TileState::SteadyInterior;
				unload(res);

            } else if (resp.action == LoadAction::CloseToParent) {

				assert(resp.parentCoord == coord);
				assert(nchildren > 0);
				assert(state == TileState::OpeningAsParent);
				for (int i=0; i<nchildren; i++) assert(children[i]->state == TileState::ClosingToParent);
				assert(resp.items.size() == 1);

				for (int i=0; i<nchildren; i++) {
					children[i]->unload(res);
					delete children[i];
					children[i] = nullptr;
				}
				nchildren = 0;

				loadFrom(resp.items[0], res);
                logTrace("load parent to close children {}", resp.parentCoord);
				state = TileState::SteadyLeaf;

            } else if (resp.action == LoadAction::LoadRoot) {

                spdlog::get("tiffRndr")->info("root recvOpenLoadedData (for {})", resp.parentCoord);

                assert(resp.items.size() == 1);
                auto& tileData  = resp.items[0];

				loadFrom(tileData, res);

				state = TileState::SteadyLeaf;
            }
        }

		inline void loadFrom(const TileData& tileData, GpuResources& res) {
				createVbo_(gpuTileData.vbo, res.ao, tileData.vertexData);
				createIbo_(gpuTileData.ibo, res.ao, (const uint8_t*)tileData.indices.data(), tileData.indices.size() * sizeof(uint16_t));
				gpuTileData.nindex = tileData.indices.size();

                uint32_t textureArrayIndex = res.takeTileInd();
				// textureArrayIndex = 0;
				gpuTileData.textureArrayIndex = textureArrayIndex;
				assert(textureArrayIndex >= 0 and textureArrayIndex < MAX_TILES);
                logTrace("loadFrom() :: img shape {} {} {} :: vbo size {} ninds {}", tileData.img.rows, tileData.img.cols, tileData.img.channels(), tileData.vertexData.size(), gpuTileData.nindex);
				uploadTex_(res.sharedTex, res.ao, textureArrayIndex, tileData.img.data, tileData.img.total() * tileData.img.elemSize(), tileData.img.cols, tileData.img.rows, tileData.img.channels());
		}

		inline void unload(GpuResources& res) {
            logTrace("unload() {}", coord);
			assert(gpuTileData.textureArrayIndex >= 0);
			gpuTileData.vbo = {};
			gpuTileData.ibo = {};
			res.returnTileInd(gpuTileData.textureArrayIndex);
			gpuTileData.textureArrayIndex = -1;
		}

        inline bool shouldDraw() const {
            return state == TileState::SteadyLeaf || state == TileState::SteadyLeafWantsToClose || state == TileState::ClosingToParent || state == TileState::ClosingToParent
                   || state == TileState::OpeningChildrenAsParent;
        }
        inline bool isSteadyLeaf() const {
            // return state == TileState::Steady || state == TileState::SteadyLeafWantsToClose || state == TileState::Closing;
            return state == TileState::SteadyLeaf || state == TileState::SteadyLeafWantsToClose;
        }

        inline bool isRoot() const {
            return parent == nullptr;
        }
        inline bool isInterior() const {
            return nchildren > 0;
        }
        inline bool isTerminal() const {
			return obb.terminal;
        }

        inline void render(const RenderState& rs) {
            if (shouldDraw()) {
				if (sse != kBoundingBoxNotVisible) {

					// draw ...
					// spdlog::get("tiffRndr")->trace("render ready leaf {} inds {}", coord, gpuTileData.nindex);

					// rs.pass.setRenderPipeline(rndrPipe);
					// rs.pass.setBindGroup(0, rs.appObjects.getSceneBindGroup());
					rs.pass.setVertexBuffer(0, gpuTileData.vbo, 0, gpuTileData.vbo.getSize());
					rs.pass.setIndexBuffer(gpuTileData.ibo, WGPUIndexFormat_Uint16, 0, gpuTileData.ibo.getSize());
					// rs.pass.drawIndexed(gpuTileData.nindex);
					rs.pass.drawIndexed(gpuTileData.nindex, 1, 0, 0, gpuTileData.textureArrayIndex);
				} else {
					logTrace("cull!");
				}

            } else if (isInterior()) {
                for (int i = 0; i < nchildren; i++) children[i]->render(rs);
            } else {
                spdlog::get("tiffRndr")->warn("non shouldDraw/isInterior ?");
            }
        }

        inline void renderBb(const RenderState& rs, InefficientBboxEntity* bboxEntity) {
            if (shouldDraw()) {
				bboxEntity->set(obb);
				bboxEntity->render(rs);
			} else {
				for (int i=0; i<nchildren; i++) children[i]->renderBb(rs, bboxEntity);
			}
		}

		inline int print(int depth=0) {
			std::string space = "";
			for (int i=0; i<depth; i++) space += "        ";
            spdlog::get("tiffRndr")->info("{}| Tile {} state {} sse {}", space, coord, state, sse);
			int n = 1;
			for (int i=0; i<nchildren; i++) n += children[i]->print(depth+1);
			return n;
		}
    };


    struct TiffGlobe : public Globe {

        TiffGlobe(AppObjects& ao, const GlobeOptions& opts)
            : Globe(ao, opts)
            , gpuResources(ao, opts)
            , loader(opts) {

            logger = spdlog::stdout_color_mt("tiffRndr");

			bboxEntity = std::make_shared<InefficientBboxEntity>(ao);
            createAndWaitForRootsToLoad_();
        }

        ~TiffGlobe() {
            for (auto root : roots) delete root;
        }

        inline virtual void render(const RenderState& rs) override {


			rs.pass.setRenderPipeline(gpuResources.mainPipelineAndLayout);
			rs.pass.setBindGroup(0, rs.appObjects.getSceneBindGroup());
			rs.pass.setBindGroup(1, gpuResources.sharedBindGroup);

			// Not needed if RenderState actually contains all of this data.
			UpdateState updateState;
			updateState.mvp = Map<const Matrix4f> { rs.camData.mvp };
			updateState.eye = Map<const Vector3f> { rs.camData.eye };
			updateState.tanHalfFovTimesHeight = rs.intrin.fy;

			auto responses = loader.pullResponses();
			if (responses.size())
				logger->debug("recv {} data loader responses", responses.size());
            for (auto& resp : responses) {
				Tile* src = reinterpret_cast<Tile*>(resp.src);
				src->recvOpenLoadedData(std::move(resp), gpuResources, loader.obbMap);
			}

			
			logger->info("|time| begin update");
            for (auto tile : roots) { tile->update(rs, gpuResources, updateState); }
			logger->info("|time| finish update");

			if (updateState.requests.size()) loader.pushRequests(std::move(updateState.requests));

			// print();

			logger->info("|time| begin render");
            for (auto tile : roots) { tile->render(rs); }
			logger->info("|time| finish render");

            // if (bboxEntity) for (auto tile : roots) { tile->renderBb(rs, bboxEntity.get()); }
        }

        inline void createAndWaitForRootsToLoad_() {
            auto rootCoordinates = loader.getRootCoordinates();

            for (const auto& c : rootCoordinates) {
                Tile* tile     = new Tile(c, nullptr, TileState::OpeningAsChild, loader.obbMap.find(c)->second);
                roots.push_back(tile);
            }

            {
                std::vector<LoadDataRequest> reqs;
                for (int i = 0; i < roots.size(); i++) {
                    LoadDataRequest req;
                    req.src         = roots[i];
                    req.seq         = seq++;
                    req.parentCoord = roots[i]->coord;
                    req.action      = LoadAction::LoadRoot;
                    reqs.push_back(req);
                }

                loader.pushRequests(std::move(reqs));
            }

            // Wait for ALL requests to finish.
            std::deque<LoadDataResponse> responses;
            while (responses.size() < roots.size()) {
                usleep(5'000);
                auto responses_ = loader.pullResponses();
                for (auto&& r : responses_) responses.emplace_back(std::move(r));
                logger->info("have {} / {} root datas loaded.", responses.size(), roots.size());
            }

            for (auto& resp : responses) {
				auto src = reinterpret_cast<Tile*>(resp.src);
				src->recvOpenLoadedData(std::move(resp), gpuResources, loader.obbMap);
			}

            logger->info("createAndWaitForRootsToLoad_ is done.");
        }

		inline void print() {
			logger->info("|time| begin print");
			int n = 0;
            for (auto tile : roots) { n += tile->print(); }
			logger->info("Have {} nodes", n);
			logger->info("|time| end print");
		}

        uint32_t seq; // load data sequence counter

        GpuResources gpuResources;

        std::vector<Tile*> roots;

        TiffDataLoader loader;
        std::shared_ptr<spdlog::logger> logger;
        std::shared_ptr<InefficientBboxEntity> bboxEntity;
    };


}

    std::shared_ptr<Globe> make_tiff_globe(AppObjects& ao, const GlobeOptions& opts) {
		tiff::maybe_make_tiff_obb_file(opts.getString("tiffPath"), opts);
        return std::make_shared<tiff::TiffGlobe>(ao, opts);
    }
}
