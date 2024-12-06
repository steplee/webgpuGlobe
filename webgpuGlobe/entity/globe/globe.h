#pragma once

#include "webgpuGlobe/app/app.h"
#include "webgpuGlobe/entity/entity.h"
#include "webgpuGlobe/util/options.h"
#include "webgpuGlobe/geo/earth.hpp"

#include "bounding_box.h"
#include "bounding_box_map.hpp"

#include <Eigen/Core>
#include <Eigen/Geometry>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

// #include <opencv2/core.hpp>

namespace wg {


	struct CastUpdate;

    using namespace Eigen;


    struct NoTilesAvailableExecption : std::runtime_error {
        inline NoTilesAvailableExecption()
            : std::runtime_error("No tiles available right now") {
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



	// Renders a 3d box showing extent of an OBB.
	// Not efficient because it reallocates a VBO every call.
	// What is best way of handling this in WebGPU? Because the map
	// operation is asynch and annoying to work with.
	// -> Answer: upload buffer using queue. Todo: make that change.
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

		uint8_t debugLevel = 0;

		// Update the `CastGpuResources` for this globe, or nullptr if not supported.
		virtual bool updateCastStuff(const CastUpdate& castUpdate) =0;

    protected:
        AppObjects& ao;
        const GlobeOptions& opts;

    private:
        // std::shared_ptr<DataLoader> loader;
        // std::shared_ptr<Renderer> renderer;
    };

    std::shared_ptr<Globe> make_tiff_globe(AppObjects& ao, const GlobeOptions& opts);

}

