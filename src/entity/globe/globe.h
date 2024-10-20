#pragma once

#include "app/app.h"
#include "entity/entity.h"

#include <variant>

#include <Eigen/Core>
#include <Eigen/Geometry>

#include <map>
#include <deque>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

// #include <spdlog/formatter.h>
// #include <spdlog/fmt/bundled/format.h>
#include "spdlog/fmt/ostr.h"


namespace wg {

    using namespace Eigen;

    // NOTE: Before trying to make the system generic (w/ virtual functions this time),
    //       I need to make sure that I can use texture arrays in WebGPU as I expect to.

    /*

// Handles to GPU objects.
struct ResidentTile { };

// CPU intermediate data. Returned by `DataLoader`
struct TileData { };

struct DataLoader {
    virtual ~DataLoader();
};

//
// The draw function could be a virtual one of the tile class.
// But by making it the responsibility of a new "renderer" class,
// virtual calls can be avoided (1 per frame rather than hundreds).
// Because one kind of renderder only ever creates one kind of tile,
// a reinterpret cast can be done.
//
// There may also extra state needed by some renderers than others, and it
// can be neatly put into this class.
//
//
struct Renderer {
    virtual ~Renderer();
};

//
// Responsible for rendering data of one source.
//
// TODO: Create a `MultiGlobe` class for multiplexing multiple data sources.
//

    */

    using GlobeOption = std::variant<std::string, int64_t, double, std::array<double, 3>>;

    struct GlobeOptions {
        std::unordered_map<std::string, GlobeOption> opts;

        inline double getDouble(const std::string& key) const {
            auto it = opts.find(key);
            assert(it != opts.end());
            return std::get<double>(it->second);
        }
        inline std::array<double, 3> getDouble3(const std::string& key) const {
            auto it = opts.find(key);
            assert(it != opts.end());
            return std::get<std::array<double, 3>>(it->second);
        }
        inline int64_t getInt(const std::string& key) const {
            auto it = opts.find(key);
            assert(it != opts.end());
            return std::get<int64_t>(it->second);
        }
        inline std::string getString(const std::string& key) const {
            auto it = opts.find(key);
            assert(it != opts.end());
            return std::get<std::string>(it->second);
        }
    };

    struct PackedOrientedBoundingBox {
        Vector3f p;
        Quaternionf q;
        Vector3f extents;
    };

    struct UnpackedOrientedBoundingBox {

        UnpackedOrientedBoundingBox(const UnpackedOrientedBoundingBox&)            = default;
        UnpackedOrientedBoundingBox(UnpackedOrientedBoundingBox&&)                 = default;
        UnpackedOrientedBoundingBox& operator=(const UnpackedOrientedBoundingBox&) = default;
        UnpackedOrientedBoundingBox& operator=(UnpackedOrientedBoundingBox&&)      = default;

        UnpackedOrientedBoundingBox(const PackedOrientedBoundingBox& pobb);

        Matrix<float, 8, 4> pts;

        // Extra information:
        // This is sort of an ugly design, but it is efficient and fits perfectly.
        bool terminal : 1;
        bool root : 1;

        // Compute screen space error.
        float sse(const Matrix4f& mvp, const Vector3f& eye);
    };

    struct QuadtreeCoordinate {
        uint64_t c;

        inline QuadtreeCoordinate() : c(0) {}

        inline QuadtreeCoordinate(uint64_t c)
            : c(c) {
        }
        inline QuadtreeCoordinate(uint32_t z, uint32_t y, uint32_t x) {
            c = (static_cast<uint64_t>(z) << 58) | (static_cast<uint64_t>(y) << 29) | x;
        }

        uint32_t z() const {
            return (c >> 58) & ((1 << 29) - 1);
        }
        uint32_t y() const {
            return (c >> 29) & ((1 << 29) - 1);
        }
        uint32_t x() const {
            return (c >> 0) & ((1 << 29) - 1);
        }

        inline bool operator==(const QuadtreeCoordinate& o) const {
            return c == o.c;
        }
        inline bool operator<(const QuadtreeCoordinate& o) const {
            return c < o.c;
        }
        inline bool operator>(const QuadtreeCoordinate& o) const {
            return c > o.c;
        }

        inline QuadtreeCoordinate parent() const {
            if (this->z() == 0) return QuadtreeCoordinate { 0, 0, 0 };
            return QuadtreeCoordinate { z() - 1, y() / 2, x() / 2 };
        }

        inline QuadtreeCoordinate child(uint32_t childIndex) const {
            assert(childIndex >= 0 and childIndex < 4);
            static constexpr uint32_t dx[4] = { 0, 1, 1, 0 };
            static constexpr uint32_t dy[4] = { 0, 0, 1, 1 };
            return QuadtreeCoordinate {
                z() + 1,
                y() + dy[childIndex],
                x() + dx[childIndex],
            };
        }
    };

    struct ObbMap {
        std::map<QuadtreeCoordinate, UnpackedOrientedBoundingBox> map;
        int maxChildren = 4;

        inline ObbMap(const GlobeOptions& opts) {
            logger = spdlog::stdout_color_mt("obbMap");

            logger->info("loaded {} items.", map.size());

            // This is sort of an ugly design (mutable/in-place change after construction), but it is efficient and fits perfectly.
            setRootInformation();
            setTerminalInformation();
        }

        // An item is a root if it is on level z=0, or if it's parent does not exist in the map.
        inline std::vector<QuadtreeCoordinate> getRoots() const {
            std::vector<QuadtreeCoordinate> out;
            for (const auto& it : map) {
				if (it.second.root) out.push_back(it.first);
            }
            return out;
        }

        inline auto find(const QuadtreeCoordinate& c) {
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
                    QuadtreeCoordinate childCoord = item.first.child(childIndex);
                    if (map.find(childCoord) != map.end()) {
                        haveChild = true;
                        break;
                    }
                }
                if (!haveChild) {
                    item.second.terminal = true;
                    nterminal++;
                }
            }

            logger->info("marked {} / {} items terminal.", nterminal, map.size());
        }

        inline void setRootInformation() {
            uint32_t nroot = 0;

            for (auto& item : map) {
                const auto k = item.first;
                if (k.z() == 0) {
                    item.second.root = true;
                    nroot++;
                } else if (map.find(k.parent()) == map.end()) {
                    item.second.root = true;
                    nroot++;
                }
            }

            logger->info("marked {} / {} items roots.", nroot, map.size());
        }

        std::shared_ptr<spdlog::logger> logger;
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



	template <> struct fmt::formatter<wg::QuadtreeCoordinate>: fmt::formatter<std::string_view> {
		auto format(wg::QuadtreeCoordinate c, fmt::format_context& ctx) const
			-> format_context::iterator {
				fmt::format_to(ctx.out(), "<{}: {}, {}>", c.z(), c.y(), c.x());
				return ctx.out();
			}
	};

