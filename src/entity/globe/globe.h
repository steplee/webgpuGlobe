#pragma once

#include "app/app.h"
#include "entity/entity.h"

#include <variant>

#include <Eigen/Core>
#include <Eigen/Geometry>

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

        // Compute screen space error.
        float sse(const Matrix4f& mvp, const Vector3f& eye);
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
