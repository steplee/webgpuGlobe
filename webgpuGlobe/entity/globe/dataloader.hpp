#pragma once

#include "globe.h"

#include <condition_variable>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <deque>

// #define logTrace1(...) spdlog::get("tiffRndr")->trace( __VA_ARGS__ );
#define logTrace1(...) {};
// #define logTrace2(...) spdlog::get("tiffRndr")->trace( __VA_ARGS__ );
#define logTrace2(...) {};

namespace wg {

	//
	// NOTE: Split into `BaseDataLoader` and `DiskDataLoader`.
	//       Because later on two more impls of `BaseDataLoader` called `Http[Server|Client]Loader` will be created.
	//

    enum class LoadAction { LoadRoot, OpenChildren, CloseToParent };

	template <class GlobeTypes>
	struct BaseDataLoader {

		// -----------------------------------------------------------------------------------------------------
		// Type defs & aliases.
		// -----------------------------------------------------------------------------------------------------

		// using Tile = GlobeTypes::Tile;
		using TileData = typename GlobeTypes::TileData;

		using TheCoordinate = typename GlobeTypes::Coordinate;
		// using TheBoundingBoxMap = typename GlobeTypes::BoundingBoxMap;
		using TheBoundingBoxMap = BoundingBoxMap<GlobeTypes>;

		struct LoadDataRequest {
			void* src; // where this request was initiated from.
			int32_t seq;

			// When opening: the parent coordinate of which the four children shall be loaded.
			// When closing: the parent coordinate to load data for.
			TheCoordinate parentCoord;
			LoadAction action;
		};

		struct LoadDataResponse {
			int32_t seq;
			void* src;
			TheCoordinate parentCoord;
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

		public:

		TheBoundingBoxMap boundingBoxMap;

		inline virtual ~BaseDataLoader() {}

        virtual void pushRequests(std::vector<LoadDataRequest>&& reqs) =0;
        virtual std::deque<LoadDataResponse> pullResponses() =0;

		// -----------------------------------------------------------------------------------------------------
		// Misc.
		// -----------------------------------------------------------------------------------------------------

        // WARNING: This is called from the render thread -- not `this->thread`.
        inline std::vector<TheCoordinate> getRootCoordinates() {
            return boundingBoxMap.getRoots();
        }

	};

	template <class Derived, class GlobeTypes>
	struct DiskDataLoader : public BaseDataLoader<GlobeTypes> {

		using Super = BaseDataLoader<GlobeTypes>;
		using LoadDataRequest = typename Super::LoadDataRequest;
		using LoadDataResponse = typename Super::LoadDataResponse;
		using TileData = typename Super::TileData;
		using UpdateState = typename Super::UpdateState;
		using TheCoordinate = typename Super::TheCoordinate;
		using TheBoundingBoxMap = typename Super::TheBoundingBoxMap;


		// -----------------------------------------------------------------------------------------------------
		// Init / deinit
		// -----------------------------------------------------------------------------------------------------


        // Note that obbMap is initialized on the calling thread synchronously
        inline DiskDataLoader(const GlobeOptions& opts, const std::string& boundingBoxPath) {
			// Super::boundingBoxMap = std::make_unique<TheBoundingBoxMap>(boundingBoxPath, opts);
			Super::boundingBoxMap = loadBoundingBoxMap(opts, boundingBoxPath);
            stop   = false;
			if (spdlog::get("tiffLoader") == nullptr)
				logger = spdlog::stdout_color_mt("tiffLoader");
			else
				logger = spdlog::get("tiffLoader");
        }


        virtual inline ~DiskDataLoader() {
            stop = true;
            cv.notify_one();
            if (thread.joinable()) thread.join();
        }

		inline void start() {
            thread = std::thread(&DiskDataLoader::loop, this);
		}


		inline TheBoundingBoxMap loadBoundingBoxMap(const GlobeOptions& opts, const std::string& boundingBoxPath) {
			// A disk loader can just load from disk.
			return TheBoundingBoxMap(boundingBoxPath, opts);
		}


		// -----------------------------------------------------------------------------------------------------
		// Main loop.
		// -----------------------------------------------------------------------------------------------------

        inline void loop() {
            while (true) {
                if (stop) break;

                // Acquire requests results.
                std::deque<LoadDataRequest> qInCopied;
                {
                    std::unique_lock<std::mutex> lck(mtxIn);
                    cv.wait(lck, [this]() { return stop or qIn.size() > 0; });
                    logTrace1("woke to |qIn| = {}, stop={}", qIn.size(), stop.load());

                    if (stop) break;

                    // With the lock held, copy all requests.
                    qInCopied = std::move(this->qIn);
                }

                if (qInCopied.size() == 0) {
                    // FIXME: Improve this logic...
                    logTrace1("no input requests -- sleeping 100ms");
                    usleep(100'000);
                    continue;
                }

                // Load data.
                std::vector<LoadDataResponse> qOutLocal;
				qOutLocal.reserve(qInCopied.size());
                for (const auto& req : qInCopied) { qOutLocal.emplace_back(load(req)); }

                // Write results.
                {
                    std::unique_lock<std::mutex> lck(mtxOut);
                    logTrace1("appending {} results, now have {}", qOutLocal.size(), qOut.size() + qOutLocal.size());
                    for (auto& resp : qOutLocal) qOut.push_back(std::move(resp));
                }
            }
        }

        inline LoadDataResponse load(const LoadDataRequest& req) {
            std::vector<TileData> items;

            if (req.action == LoadAction::OpenChildren) {
                for (uint32_t childIndex = 0; childIndex < TheCoordinate::MaxChildren; childIndex++) {
                    TheCoordinate childCoord = req.parentCoord.child(childIndex);

                    auto boundingBoxIt                    = Super::boundingBoxMap.find(childCoord);

                    if (boundingBoxIt != Super::boundingBoxMap.end()) {
                        TileData item;
                        static_cast<Derived*>(this)->loadActualData(item, childCoord);
						item.coord = childCoord;
                        item.terminal = boundingBoxIt->second.terminal;
                        item.root     = boundingBoxIt->second.root;
                        items.push_back(std::move(item));
                    }
                }
				logTrace1("for OpenChildren action, pushed {} items", items.size());
            } else if (req.action == LoadAction::CloseToParent or req.action == LoadAction::LoadRoot) {
                auto boundingBoxIt = Super::boundingBoxMap.find(req.parentCoord);
                assert(boundingBoxIt != Super::boundingBoxMap.end());

                TileData item;
                static_cast<Derived*>(this)->loadActualData(item, req.parentCoord);
				item.coord = req.parentCoord;
                item.terminal = boundingBoxIt->second.terminal;
                item.root     = boundingBoxIt->second.root;
                items.push_back(std::move(item));
            }

            return LoadDataResponse {
                .seq = req.seq, .src = req.src, .parentCoord = req.parentCoord, .action = req.action, .items = std::move(items)
            };
        }

		// -----------------------------------------------------------------------------------------------------
		// Queue / de-queue work
		// -----------------------------------------------------------------------------------------------------

        // Called from main thread, typically.
        inline virtual void pushRequests(std::vector<LoadDataRequest>&& reqs) override {
            {
                std::unique_lock<std::mutex> lck(mtxIn);
                // What is the difference between the following two lines, and is one more correct?
                for (auto& req : reqs) qIn.push_back(std::move(req));
                // for (auto&& req : reqs) qIn.push_back(std::move(req));
            }
            cv.notify_one();
        }

        // Called from main thread, typically.
        inline virtual std::deque<LoadDataResponse> pullResponses() override {
            std::unique_lock<std::mutex> lck(mtxOut);
            return std::move(qOut);
        }


		// -----------------------------------------------------------------------------------------------------
		// Fields
		// -----------------------------------------------------------------------------------------------------


        std::deque<LoadDataRequest> qIn;
        std::deque<LoadDataResponse> qOut;

        std::condition_variable cv;
        std::mutex mtxIn, mtxOut;
        std::thread thread;
        std::atomic<bool> stop;
        std::shared_ptr<spdlog::logger> logger;

	};


}
