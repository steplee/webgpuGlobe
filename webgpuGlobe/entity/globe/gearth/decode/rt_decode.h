#pragma once
#include <cstdint>
#include <vector>

namespace wg {
namespace gearth {

	struct __attribute__((packed)) RtPackedVertex {
		uint8_t x,y,z,w;
		uint16_t u,v;
		uint8_t nx,ny,nz;
		uint8_t extra;
	};
	static_assert(sizeof(RtPackedVertex) == 12);

	struct __attribute__((packed)) RtUnpackedVertex {
		float x,y,z,w;
		float u,v;
		float nx,ny,nz,extra;
	};
	static_assert(sizeof(RtUnpackedVertex) == 10*4);

	struct DecodedCpuTileData {
		alignas(16) double modelMat[16];
		struct MeshData {
			// std::vector<RtPackedVertex> vert_buffer_cpu;
			std::vector<RtUnpackedVertex> vert_buffer_cpu;
			std::vector<uint16_t> ind_buffer_cpu;
			std::vector<uint8_t> img_buffer_cpu;
			std::vector<uint8_t> tmp_buffer;
			uint32_t texSize[3];
			float uvOffset[2];
			float uvScale[2];
			int layerBounds[10];
		};
		std::vector<MeshData> meshes;
		float metersPerTexel;
	};

}
}
