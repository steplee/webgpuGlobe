#pragma once

#include "app/app.h"
#include "entity/entity.h"
#include "util/options.h"
#include "geo/earth.hpp"

#include <variant>

#include <Eigen/Core>
#include <Eigen/Geometry>

#include <deque>
#include <map>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

namespace wg {

    using namespace Eigen;


    struct NoTilesAvailableExecption : std::runtime_error {
        inline NoTilesAvailableExecption()
            : std::runtime_error("No tiles available right now") {
        }
    };

    struct __attribute__((packed)) PackedOrientedBoundingBox {
        Vector3f p;
        Quaternionf q;
        Vector3f extents;
        float geoError;
    };

	// For information about geometric and screen-space error, see the 3d tiles spec.
	// Even better they have a PDF somewhere giving a good intuition for it, as
	// well as it's definition in terms of object distance and camera intrinsics.
	// IIRC, geometric error is the error induced by not expanding a tile's children, or
	// put another way: by rendering a parent instead of all of it's children.
	//
	// Screen space error is that value (originally has units meters) transformed to make
	// it have units in pixels, which is what the actual open/close thresholds shall be
	// specified in.
	//
	// In this implementation, screen space error will be computed using the exterior
	// distance from the eye to the closest point of the bounding box for a tile.
	//
	// Let me write a little bit about sse to remind myself how it goes.
	// As we get closer to an object, the screen space error will grow.
	// So SSE will be inversely proportional to distance-from-eye.
	// When we are inside a bounding box it shall infinitely high.
	//

	// Special value returned by `computeSse` indicating that the bounding box failed
	// the frustum culling check (i.e. is not visible in scene).
	constexpr float kBoundingBoxNotVisible = -2.f;
	constexpr float kBoundingBoxContainsEye = -3.f; // We are inside bounding box, SSE would be infinite.


	// A concrete type, shared amongst all globe implementations.
    struct UnpackedOrientedBoundingBox {

        inline UnpackedOrientedBoundingBox() : terminal(false), root(false) {}
        UnpackedOrientedBoundingBox(const UnpackedOrientedBoundingBox&)            = default;
        UnpackedOrientedBoundingBox(UnpackedOrientedBoundingBox&&)                 = default;
        UnpackedOrientedBoundingBox& operator=(const UnpackedOrientedBoundingBox&) = default;
        UnpackedOrientedBoundingBox& operator=(UnpackedOrientedBoundingBox&&)      = default;

        UnpackedOrientedBoundingBox(const PackedOrientedBoundingBox& pobb);

        Matrix<float, 8, 3> pts;
		PackedOrientedBoundingBox packed;
        // float geoError;

        // Extra information:
        // This is sort of an ugly design, but it is efficient and fits perfectly.
        bool terminal : 1;
        bool root : 1;

        // Compute screen space error, while also doing frustum cull check.
        float computeSse(const Matrix4f& mvp, const Vector3f& eye, float tanHalfFovTimesHeight);
    };

    struct BaseCoordinate {
        uint64_t c;

        inline BaseCoordinate()
            : c(0) {
        }

        inline BaseCoordinate(uint64_t c)
            : c(c) {
        }

        inline bool operator==(const BaseCoordinate& o) const {
            return c == o.c;
        }
        inline bool operator<(const BaseCoordinate& o) const {
            return c < o.c;
        }
        inline bool operator>(const BaseCoordinate& o) const {
            return c > o.c;
        }
    };



	//
	// TODO:
	// Currently the application is designed s.t. all tiles' bounding boxes are loaded
	// into memory at the beginning.
	// This will not scale. Instead do something similar to Google Earth, where they have a
	// "BulkMetadata" tree that has data for all tile nodes in the next four levels and the bulk metadata
	// tree itself has 1/4 the number of layers as the tile tree.
	//

    template <class GlobeTypes>
	struct ObbMap {
		using Coordinate = typename GlobeTypes::Coordinate;
        static constexpr int MaxChildren = Coordinate::MaxChildren;

        // WARNING: I don't think this will be packed correctly for different compilers/arches. So write simple serialize/deserialze
        // funcs
        struct __attribute__((packed)) Item {
            Coordinate coord;
            PackedOrientedBoundingBox obb;
        };

        std::map<Coordinate, UnpackedOrientedBoundingBox> map;

		inline ObbMap(const std::string& loadFromPath, const GlobeOptions& opts) {
			if (logger = spdlog::get("obbMap"); logger == nullptr) logger = spdlog::stdout_color_mt("obbMap");

			// TODO: load it.
			{
				std::ifstream ifs(loadFromPath, std::ios_base::binary);

				while (ifs.good()) {
					Item item;
					size_t prior = ifs.tellg();
					ifs.read((char*)&item, sizeof(decltype(item)));
					size_t post = ifs.tellg();

					if (post - prior != sizeof(decltype(item))) {
						if (ifs.eof()) break;
						else throw std::runtime_error(fmt::format("failed to read an item ({} / {}), and not at end of file?", post-prior, sizeof(decltype(item))));
					}

					map[item.coord] = UnpackedOrientedBoundingBox{item.obb};
				}
			}

            logger->info("loaded {} items.", map.size());

            // This is sort of an ugly design (mutable/in-place change after construction), but it is efficient and fits perfectly.
            setRootInformation();
            setTerminalInformation();
        }

        inline ObbMap() {
            if (logger = spdlog::get("obbMap"); logger == nullptr) logger = spdlog::stdout_color_mt("obbMap");
            logger->info("Constructing empty obb map.", map.size());
        }

        // void dumpToFile(const std::string& path);

        // An item is a root if it is on level z=0, or if it's parent does not exist in the map.
        inline std::vector<Coordinate> getRoots() const {
            std::vector<Coordinate> out;
            for (const auto& it : map) {
                if (it.second.root) out.push_back(it.first);
            }
            return out;
        }

        inline auto find(const Coordinate& c) {
            return map.find(c);
        }
        inline auto end() {
            return map.end();
        }

        // Loop over map and mark any item without any children as `terminal`.
        inline void setTerminalInformation() {
            uint32_t nterminal = 0;

            for (auto& item : map) {
                bool haveChild = false;
                for (uint32_t childIndex = 0; childIndex < 4; childIndex++) {
                    Coordinate childCoord = item.first.child(childIndex);
                    if (map.find(childCoord) != map.end()) {
                        haveChild = true;
                        break;
                    }
                }
                if (!haveChild) {
                    item.second.terminal = true;
                    nterminal++;
                } else {
                    item.second.terminal = false;
				}
            }

            logger->info("marked {} / {} items terminal.", nterminal, map.size());
        }

        inline void setRootInformation() {
            uint32_t nroot = 0;
            logger->info("finding roots");

            for (auto& item : map) {
                const auto k = item.first;
                if (k.z() == 0) {
					logger->info("have root {} (z=0)", item.first);
                    item.second.root = true;
                    nroot++;
                } else if (map.find(k.parent()) == map.end()) {
					logger->info("have root {}", item.first);
                    item.second.root = true;
                    nroot++;
                } else {
                    item.second.root = false;
				}
            }

            logger->info("marked {} / {} items roots.", nroot, map.size());
        }

        std::shared_ptr<spdlog::logger> logger;
    };


	// Renders a 3d box showing extent of an OBB.
	// Not efficient because it reallocates a VBO every call.
	// What is best way of handling this in WebGPU? Because the map
	// operation is asynch and annoying to work with.
	struct InefficientBboxEntity : public Entity {

		InefficientBboxEntity(AppObjects& ao);

		void set(const UnpackedOrientedBoundingBox& uobb);

        virtual void render(const RenderState& rs) override;

        Buffer vbo;
        Buffer ibo;
		PipelineLayout pipelineLayout;
        RenderPipeline rndrPipe;
		int nindex;
		AppObjects& ao;
	};

    class Globe : public Entity {

    public:
        Globe(const Globe&)            = delete;
        Globe(Globe&&)                 = delete;
        Globe& operator=(const Globe&) = delete;
        Globe& operator=(Globe&&)      = delete;

        Globe(AppObjects& ao, const GlobeOptions& opts);

        virtual ~Globe();

    protected:
        AppObjects& ao;
        const GlobeOptions& opts;

    private:
        // std::shared_ptr<DataLoader> loader;
        // std::shared_ptr<Renderer> renderer;
    };

    std::shared_ptr<Globe> make_tiff_globe(AppObjects& ao, const GlobeOptions& opts);

}

