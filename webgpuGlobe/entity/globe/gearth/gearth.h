#pragma once

#include "../globe.h"
#include "./octree.h"
#include "decode/rt_decode.h"

// #include <opencv2/core.hpp>
#include "webgpuGlobe/util/image.h"


namespace wg {

	namespace gearth {

		constexpr static int32_t MAX_TILES = 1024;

		struct GpuTileData {
			Buffer ibo;
			Buffer vbo;
			int32_t textureArrayIndex = -1;
			uint32_t nindex = 0;
		};

		struct TileData {

			OctreeCoordinate coord;
			// cv::Mat img;
			// Image img;


			std::vector<float> model; // 4x4 model mat

			// struct OneTileData {
				// std::vector<uint8_t> vertexData;
				// std::vector<uint16_t> indices;
			// };
			// std::vector<OneTileData> innerDatas; // usually length 1, sometimes more.
			DecodedCpuTileData dtd;

			// Non gpu data, but feedback from DataLoader none-the-less
			bool terminal = false;
			bool root     = false;
		};


		struct GearthTypes {
			using Coordinate = OctreeCoordinate;

			using GpuTileData = gearth::GpuTileData;
			using TileData = gearth::TileData;
			// using Tile = gearth::Tile;
		};

		using GearthBoundingBoxMap = BoundingBoxMap<GearthTypes>;
	}
}
