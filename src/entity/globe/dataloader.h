#pragma once

#include "globe.h"

#include <condition_variable>
#include <mutex>
#include <thread>
#include <unistd.h>

// #define logTrace1(...) spdlog::get("tiffRndr")->trace( __VA_ARGS__ );
#define logTrace1(...) {};

namespace wg {

    enum class LoadAction { LoadRoot, OpenChildren, CloseToParent };

	template <class Derived, class GlobeTypes>
	struct DataLoader {

		// -----------------------------------------------------------------------------------------------------
		// Type defs & aliases.
		// -----------------------------------------------------------------------------------------------------

		// using Tile = GlobeTypes::Tile;
		using TileData = typename GlobeTypes::TileData;

		using TheCoordinate = typename GlobeTypes::Coordinate;
		// using TheObbMap = typename GlobeTypes::ObbMap;
		using TheObbMap = ObbMap<GlobeTypes>;

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


		// -----------------------------------------------------------------------------------------------------
		// Init / deinit
		// -----------------------------------------------------------------------------------------------------


        // Note that obbMap is initialized on the calling thread synchronously
        inline DataLoader(const GlobeOptions& opts, const std::string& obbPath)
            : obbMap(obbPath, opts) {
            stop   = false;
            logger = spdlog::stdout_color_mt("tiffLoader");
        }

        inline ~DataLoader() {
            stop = true;
            cv.notify_one();
            if (thread.joinable()) thread.join();
        }

		inline void start() {
            thread = std::thread(&DataLoader::loop, this);
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

                    auto obbIt                    = obbMap.find(childCoord);

                    if (obbIt != obbMap.end()) {
                        TileData item;
                        static_cast<Derived*>(this)->loadActualData(item, childCoord);
						item.coord = childCoord;
                        item.terminal = obbIt->second.terminal;
                        item.root     = obbIt->second.root;
                        items.push_back(std::move(item));
                    }
                }
				logTrace1("for OpenChildren action, pushed {} items", items.size());
            } else if (req.action == LoadAction::CloseToParent or req.action == LoadAction::LoadRoot) {
                auto obbIt = obbMap.find(req.parentCoord);
                assert(obbIt != obbMap.end());

                TileData item;
                static_cast<Derived*>(this)->loadActualData(item, req.parentCoord);
				item.coord = req.parentCoord;
                item.terminal = obbIt->second.terminal;
                item.root     = obbIt->second.root;
                items.push_back(std::move(item));
            }

            return LoadDataResponse {
                .seq = req.seq, .src = req.src, .parentCoord = req.parentCoord, .action = req.action, .items = std::move(items)
            };
        }

		// -----------------------------------------------------------------------------------------------------
		// Misc.
		// -----------------------------------------------------------------------------------------------------

        // WARNING: This is called from the render thread -- not `this->thread`.
        inline std::vector<TheCoordinate> getRootCoordinates() {
            return obbMap.getRoots();
        }

		// -----------------------------------------------------------------------------------------------------
		// Queue / de-queue work
		// -----------------------------------------------------------------------------------------------------

        // Called from main thread, typically.
        inline void pushRequests(std::vector<LoadDataRequest>&& reqs) {
            {
                std::unique_lock<std::mutex> lck(mtxIn);
                // What is the difference between the following two lines, and is one more correct?
                for (auto& req : reqs) qIn.push_back(std::move(req));
                // for (auto&& req : reqs) qIn.push_back(std::move(req));
            }
            cv.notify_one();
        }

        // Called from main thread, typically.
        inline std::deque<LoadDataResponse> pullResponses() {
            std::unique_lock<std::mutex> lck(mtxOut);
            return std::move(qOut);
        }


		// -----------------------------------------------------------------------------------------------------
		// Fields
		// -----------------------------------------------------------------------------------------------------

        TheObbMap obbMap;

        std::deque<LoadDataRequest> qIn;
        std::deque<LoadDataResponse> qOut;

        std::condition_variable cv;
        std::mutex mtxIn, mtxOut;
        std::thread thread;
        std::atomic<bool> stop;
        std::shared_ptr<spdlog::logger> logger;

	};


}
