#pragma once

#include <opencv2/imgcodecs.hpp>
// #include <fmt/core.h>
#include <spdlog/spdlog.h>

#include "rt_convert.hpp"
#include "rt_decode.h"
#include "rocktree.pb.h"

namespace rtpb = ::geo_globetrotter_proto_rocktree;

namespace wg {
namespace gearth {
	namespace {

/*
 * These functions come from my python implementation of the original javascript code.
 * I just sort of translated it, so not particularly clean.
 */

// Will advance the input pointer!
inline int unpackVarInt(uint8_t*& b) {
	int c = 0;
	int d = 1;
	while(1) {
		uint8_t e = *b;
		b++;
		c += (e&0x7f) * d;
		d <<= 7;
		if ((e & 0x80) == 0) return c;
	}
}

// Almost directly from the 'client' code in the gearth repo
int unpackNormalsStepOne(const rtpb::NodeData& nodeData, std::vector<uint8_t>& partialNormals) {
	auto f1 = [](int v, int l) {
		if (4 >= l)
			return (v << l) + (v & (1 << l) - 1);
		if (6 >= l) {;
			auto r = 8 - l;
			return (v << l) + (v << l >> r) + (v << l >> r >> r) + (v << l >> r >> r >> r);
		}
		return -(v & 1);
	};
	auto f2 = [](double c) {
		auto cr = (int)round(c);
		if (cr < 0) return 0;
		if (cr > 255) return 255;
		return cr;
	};
	assert(nodeData.has_for_normals());
	auto input = nodeData.for_normals();
	auto data = (uint8_t*)input.data();
	auto size = input.size();
	assert(size > 2);
	auto count = *(uint16_t*)data;
	assert(count * 2 == size - 3);
	int s = data[2];
	data += 3;

	partialNormals.resize(3*count);
	
	for (auto i = 0; i < count; i++) {
		double a = f1(data[0 + i], s) / 255.0;
		double f = f1(data[count + i], s) / 255.0;
			
		double b = a, c = f, g = b + c, h = b - c;
		int sign = 1;

		if (!(.5 <= g && 1.5 >= g && -.5 <= h && .5 >= h)) {
			sign = -1;
			if (.5 >= g) {
				b = .5 - f;
				c = .5 - a;
			} else {
				if (1.5 <= g) {
					b = 1.5 - f;
					c = 1.5 - a;
				} else {
					if (-.5 >= h) {
						b = f - .5;
						c = a + .5;
					} else {
						b = f + .5;
						c = a - .5;
					}
				}
			}
			g = b + c;
			h = b - c;
		}
		
		a = fmin(fmin(2 * g - 1, 3 - 2 * g), fmin(2 * h + 1, 1 - 2 * h)) * sign;
		b = 2 * b - 1;
		c = 2 * c - 1;
		auto m = 127 / sqrt(a * a + b * b + c * c);

		partialNormals[3 * i + 0] = f2(m * a + 127);
		partialNormals[3 * i + 1] = f2(m * b + 127);
		partialNormals[3 * i + 2] = f2(m * c + 127);		
	}
	return 3*count;
}
int unpackNormalsStepTwo(const rtpb::Mesh& mesh, std::vector<RtPackedVertex>& verts, const std::vector<uint8_t>& partialNormals) {
	auto normals = mesh.normals();
	uint8_t *new_normals = NULL;
	int count = 0;
	if (mesh.has_normals() && partialNormals.size()) {
		count = normals.size() / 2;		
		auto input = (uint8_t*)normals.data();
		for (auto i = 0; i < count; ++i) {
			int j = input[i] + (input[count + i] << 8);
			assert(3 * j + 2 < partialNormals.size());
			verts[i].nx = partialNormals[3 * j + 0];
			verts[i].ny = partialNormals[3 * j + 1];
			verts[i].nz = partialNormals[3 * j + 2];
			verts[i].extra = 0;			
		}
	} else {
		count = (mesh.vertices().size() / 3) * 8;
		for (auto i = 0; i < count; ++i) {
			verts[i].nx = 127;
			verts[i].ny = 127;
			verts[i].nz = 127;
			verts[i].extra = 0;
		}
	}
	return 4 * count;
}

void computeNormals(const rtpb::Mesh& mesh, std::vector<RtPackedVertex>& vs) {
	std::vector<Vector3f> nacc(vs.size());

	for (int i=0; i<vs.size(); i++) {
		Vector3f n = nacc[i].normalized() * 255;
	}

}


inline bool decode_node_to_tile(
		std::ifstream &ifs,
		DecodedCpuTileData& dtd, bool forceTriList) {

	rtpb::NodeData nd;
	if (!nd.ParseFromIstream(&ifs)) {
		fmt::print(" - [#decode_node_to_tile] ERROR: failed to parse istream!\n");
		return true;
	}

	std::vector<uint8_t> partialNormals;
	unpackNormalsStepOne(nd, partialNormals);

	int n_mesh = nd.meshes_size();
	int out_index = 0;
	dtd.meshes.resize(n_mesh);
	for (int i=0; i<n_mesh; i++) {
		auto& mesh = nd.meshes(i);
		auto& md = dtd.meshes[out_index];
		bool bad = false;

		int nv = mesh.vertices().length() / 3;
		md.vert_buffer_cpu.resize(nv);

		// Decode verts
		const std::string& verts = mesh.vertices();
		uint8_t * v_data = (uint8_t*) verts.data();
		for (int i=0; i<3; i++) {
			uint8_t acc = 0;
			for (int j=0; j<nv; j++) {
				acc = acc + v_data[i*nv+j];
				// (&md.vert_buffer_cpu[j].x)[i] = acc;
				// NOTE TODO XXX SWAP Y Z
				if (i==0) md.vert_buffer_cpu[j].x = acc;
				if (i==1) md.vert_buffer_cpu[j].y = acc;
				if (i==2) md.vert_buffer_cpu[j].z = acc;
			}
		}

		unpackNormalsStepTwo(mesh, md.vert_buffer_cpu, partialNormals);
		// computeNormals(mesh, md.vert_buffer_cpu);


		uint8_t* data = (uint8_t*) mesh.texture_coordinates().data();
		auto u_mod = 1 + *(uint16_t*)(data+0);
		auto v_mod = 1 + *(uint16_t*)(data+2);
		// fmt::print(" - UV MOD {} {}\n", u_mod, v_mod);
		data += 4;
		auto u=0,v=0;
		for (int i=0; i<nv; i++) {
			u = (u + data[nv*0 + i] + (data[nv*2 + i] << 8)) % u_mod;
			v = (v + data[nv*1 + i] + (data[nv*3 + i] << 8)) % v_mod;
			md.vert_buffer_cpu[i].u = u;
			md.vert_buffer_cpu[i].v = v;
		}


		md.uvOffset[0] = 0.5;
		md.uvOffset[1] = 0.5;
		md.uvScale[0] = 1.0 / u_mod;
		md.uvScale[1] = 1.0 / v_mod;

		if (mesh.uv_offset_and_scale_size() == 4) {
			md.uvOffset[0] = mesh.uv_offset_and_scale(0);
			md.uvOffset[1] = mesh.uv_offset_and_scale(1);
			md.uvScale[0] = mesh.uv_offset_and_scale(2);
			md.uvScale[1] = mesh.uv_offset_and_scale(3);
		} else {
			// md.uvOffset[1] -= 1.0 / md.uvScale[1];
			// md.uvScale[1] *= -1.0;
		}
		// fmt::print(" - UV SO {} {} {} {}\n", md.uvScale[0], md.uvScale[1], md.uvOffset[0], md.uvOffset[1]);

		// if (nv >= RtCfg::maxVerts) fmt::print(" - WARNING: nv {} / {}\n", nv, RtCfg::maxVerts);




		// Normals
		auto& ns_bites = mesh.normals();
		auto &fns = md.tmp_buffer;
		// TODO


		// Inds
		auto& indices = mesh.indices();
		uint8_t* ptr = (uint8_t*) indices.data();
		uint32_t strip_len = unpackVarInt(ptr);
		md.ind_buffer_cpu.resize(strip_len);
		int j=0, zeros=0;
		while (j<strip_len) {
			int v = unpackVarInt(ptr);

			md.ind_buffer_cpu[j] = (uint16_t)(zeros - v);

			if (md.ind_buffer_cpu[j] >= nv) {
				fmt::print(" - ind {}/{} was invalid, pointed to vert {} / {}\n",j,strip_len, md.ind_buffer_cpu[j],nv);
			}

			if (v==0) zeros += 1;
			j += 1;
		}

		// Octant & layer bounds (?)
		/*
				void unpackOctantMaskAndOctantCountsAndLayerBounds(const std::string packed, const uint16_t *indices, int indices_len, uint8_t *vertices, int vertices_len, int layer_bounds[10])
				{
					// todo: octant counts
					auto offset = 0;
					auto len = unpackVarInt(packed, &offset);
					auto idx_i = 0;
					auto k = 0;
					auto m = 0;

					for (auto i = 0; i < len; i++) {
						if (0 == i % 8) {
							assert(m < 10);
							layer_bounds[m++] = k;
						}
						auto v = unpackVarInt(packed, &offset);
						for (auto j = 0; j < v; j++) {
							auto idx = indices[idx_i++];
							assert(0 <= idx && idx < indices_len);
							auto vtx_i = idx;
							assert(0 <= vtx_i && vtx_i < vertices_len / sizeof(vertex_t));
							((vertex_t *)vertices)[vtx_i].w = i & 7;
						}
						k += v;
					}

					for (; 10 > m; m++) layer_bounds[m] = k;
				}
			*/

		{
			uint8_t* ptr = (uint8_t*) mesh.layer_and_octant_counts().data();
			auto len = unpackVarInt(ptr);
			auto idx_i = 0;
			auto k=0,m=0;
			for (auto i=0; i<len and not bad; i++) {
				if (i%8 == 0) {
					assert(m<10);
					md.layerBounds[m++] = k;
				}
				auto v = unpackVarInt(ptr);
				for (auto j=0; j<v; j++) {
					auto idx = md.ind_buffer_cpu[idx_i++];
					auto vi = idx;
					if (not (vi>=0 and vi<nv)) {
						fmt::print(" - bad decode: {}/{} {}/{} vi {} idx {} nv {}\n", i,len,j,v, vi,idx,nv);
						bad = true;
						break;
						//return true;
					}
					// assert(vi >= 0 and vi < nv);
					md.vert_buffer_cpu[vi].w = i & 7;
				}
				k += v;
			}
			for (; 10 > m; m++) md.layerBounds[m] = k;
			if (md.layerBounds[3] < 1) fmt::print(" - truncating {} to {} lb len {}, k {}, m {}\n", md.ind_buffer_cpu.size(), md.layerBounds[3], len,k,m);
			// bad |= md.layerBounds[3] == 0;
			// if (bad) continue;
			md.ind_buffer_cpu.resize(md.layerBounds[3]);
		}

		if (forceTriList) {
			std::vector<uint16_t> oldInds { std::move(md.ind_buffer_cpu) };
			for (int i=0; i<oldInds.size()-2; i++) {
				if (i % 2 == 0) {
					md.ind_buffer_cpu.push_back(oldInds[i+0]);
					md.ind_buffer_cpu.push_back(oldInds[i+1]);
					md.ind_buffer_cpu.push_back(oldInds[i+2]);
				} else {
					md.ind_buffer_cpu.push_back(oldInds[i+1]);
					md.ind_buffer_cpu.push_back(oldInds[i+0]);
					md.ind_buffer_cpu.push_back(oldInds[i+2]);
				}
			}
		}


		if(0) for (int j=0; j<nv; j++) {
			fmt::print(" - vert {} : {} {} {} {}\n", j,
					md.vert_buffer_cpu[j].x, md.vert_buffer_cpu[j].y,
					md.vert_buffer_cpu[j].z, md.vert_buffer_cpu[j].w);
		}


		// Texture
		md.texSize[0] = md.texSize[1] = md.texSize[2] = 0;
		if (mesh.texture_size() > 0) {
			auto& tex = mesh.texture(0);
			if (tex.format() != rtpb::Texture::JPG) {
				printf(" - texture had non-JPG format, which is not supported.\n");
				bad |= true;
			} else {
				int dc = 4;
				md.texSize[0] = tex.height();
				md.texSize[1] = tex.width();
				md.texSize[2] = dc;
				// if (md.texSize[0] > RtCfg::maxTextureEdge) printf(" - texture had larger size then allowed : %u / %u\n", (uint32_t)md.texSize[0], (uint32_t)RtCfg::maxTextureEdge);
				// if (md.texSize[1] > RtCfg::maxTextureEdge) printf(" - texture had larger size then allowed : %u / %u\n", (uint32_t)md.texSize[1], (uint32_t)RtCfg::maxTextureEdge);

				// TODO: Decode jpeg using STB lib
				// md.img_buffer_cpu.resize(md.texSize[0]*md.texSize[1]*md.texSize[2]);
				md.img_buffer_cpu.resize(md.texSize[0]*md.texSize[1]*md.texSize[2], 255);

				cv::Mat tmpMat = cv::imdecode(cv::InputArray{tex.data(0).data(), tex.data(0).size()}, cv::IMREAD_UNCHANGED);

				// STBIDEF stbi_uc *stbi_load_from_memory   (stbi_uc           const *buffer, int len   , int *x, int *y, int *channels_in_file, int desired_channels);
				int w,h,c;
				w = tmpMat.cols;
				h = tmpMat.rows;
				c = tmpMat.channels();
				// uint8_t* tmp = stbi_load_from_memory((const uint8_t*)tex.data(0).data(), tex.data(0).length(), &w,&h,&c, 3);
				// uint8_t* tmp = stbi_load_from_memory((const uint8_t*)tex.data(0).data(), tex.data(0).length(), &w,&h,&c, dc);
				if (tmpMat.empty()) {
					fmt::print(" [decode] Warning: failed to decode jpeg of size {} {} {}!\n", md.texSize[0],md.texSize[1],md.texSize[2]);
				} else {
					// memcpy(md.img_buffer_cpu.data(), tmp, w*h*dc);
					for (int y=0; y<h; y++) for (int x=0; x<w; x++) for (int i=0; i<c; i++)
						md.img_buffer_cpu[y*w*4+x*4+i] = tmpMat.data[y*w*c+x*c+i];

					if (w!=tex.width() or h!=tex.height()) {
						fmt::print(" [decode] Warning: decoded size did not match pb size: {} {} vs {} {}\n", md.texSize[0], md.texSize[1], h,w);
					}
				}

			}

		}

		if (not bad) {
			out_index++;
		} else
			fmt::print(" - [#decode_node_to_tile] skipping mesh {}/{}, it was bad for some reason!\n", i, n_mesh);
	}
	if (out_index < dtd.meshes.size())
		dtd.meshes.resize(out_index);

	Matrix4d globeFromMesh1;
	for (int i=0; i<16; i++) globeFromMesh1(i) = nd.matrix_globe_from_mesh(i);

	if (0) {
		Matrix4d globeFromMesh2 = convert_authalic_to_geodetic(globeFromMesh1);
		for (int i=0; i<16; i++) dtd.modelMat[i] = globeFromMesh2(i);
	} else {
		for (int i=0; i<16; i++) dtd.modelMat[i] = globeFromMesh1(i);
		// fmt::print(" - ModelMat:\n{}\n", globeFromMesh1);
	}


	// Signify that we don't have the data (it is in the bulk metadata)
	dtd.metersPerTexel = -1;

	return false;
}


}
}
}
