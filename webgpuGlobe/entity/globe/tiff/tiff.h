#pragma once

#include "../globe.h"
#include "../quadtree.h"

// #include <opencv2/core.hpp>
#include "util/image.h"


namespace wg {

	namespace tiff {

		constexpr static int32_t MAX_TILES = 1024;

		struct GpuTileData {
			Buffer ibo;
			Buffer vbo;
			int32_t textureArrayIndex = -1;
			uint32_t nindex = 0;
		};

		struct TileData {
			QuadtreeCoordinate coord;
			// cv::Mat img;
			Image img;

			std::vector<uint8_t> vertexData;
			std::vector<uint16_t> indices;

			// Non gpu data, but feedback from DataLoader none-the-less
			bool terminal = false;
			bool root     = false;
		};


		struct TiffTypes {
			using Coordinate = QuadtreeCoordinate;

			using GpuTileData = tiff::GpuTileData;
			using TileData = tiff::TileData;
			// using Tile = tiff::Tile;
		};

		using TiffBoundingBoxMap = BoundingBoxMap<TiffTypes>;
	}
}
