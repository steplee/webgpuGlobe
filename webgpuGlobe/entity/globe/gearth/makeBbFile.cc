#include "gearth.h"
#include <spdlog/spdlog.h>
#include <optional>

#include "geo/conversions.h"
#include "geo/earth.hpp"

#include "entity/entity.h"

#include "util/align3d.hpp"

#include <sys/stat.h>
#include <dirent.h>

#include "decode/rt_convert.hpp"
#include "rocktree.pb.h"
namespace rtpb = ::geo_globetrotter_proto_rocktree;

namespace {
    using namespace wg;
    using namespace wg::gearth;
	constexpr double R1         = (6378137.0);


	std::vector<std::string> list_dir(const std::string& path) {
		std::vector<std::string> out;

		DIR *d;
		struct dirent *dir;
		d = opendir(path.c_str());
		if (d)
		{
			while ((dir = readdir(d)) != NULL)
			{
				if(dir->d_type==DT_REG){
					out.push_back(fmt::format("{}/{}", path, dir->d_name));
				}
			}
			closedir(d);
		}
		return out;
	}

    static Matrix3f getSphericalLtp(const Vector3f& eye) {
		Vector3f f = eye.normalized();
		Vector3f r = -f.cross(Vector3f::UnitZ()).normalized();
		Vector3f u = f.cross(r).normalized();
		Matrix3f out;
		out.col(2) = f;
		out.col(1) = u;
		out.col(0) = r;
		return out;
	};

    bool file_exists(const std::string& path) {
        struct stat buf;
        int ret = stat(path.c_str(), &buf);
        if (ret != 0) return false;
        return buf.st_size > 0;
    }

	inline static std::optional<PackedOrientedBoundingBox> decode_obb(const std::string& key, const rtpb::NodeMetadata& nodeMeta, const Vector3d& headNodeCenter, float metersPerTexel) {
		if (!nodeMeta.has_oriented_bounding_box()) {
			fmt::print("item '{}' missing obb.\n", key);
			return {};
		}
		const auto &obbString = nodeMeta.oriented_bounding_box();

		assert(obbString.length() == 15);

		const uint8_t* bites = (const uint8_t*)obbString.data();

		Vector3d ctr0 {
			reinterpret_cast<const int16_t*>(bites+0)[0] * metersPerTexel,
			reinterpret_cast<const int16_t*>(bites+2)[0] * metersPerTexel,
			reinterpret_cast<const int16_t*>(bites+4)[0] * metersPerTexel
		};
		Vector3d ctr = ctr0 + headNodeCenter;

		Vector3f ext {
			reinterpret_cast<const uint8_t*>(bites+6)[0] * metersPerTexel,
			reinterpret_cast<const uint8_t*>(bites+7)[0] * metersPerTexel,
			reinterpret_cast<const uint8_t*>(bites+8)[0] * metersPerTexel
		};

		constexpr float pi = M_PI;
		Vector3f euler {
			reinterpret_cast<const uint16_t*>(bites+9)[0] *  pi/32768.f,
			reinterpret_cast<const uint16_t*>(bites+11)[0] *  pi/65536.f,
			reinterpret_cast<const uint16_t*>(bites+13)[0] *  pi/32768.f
		};
		float c0 = std::cos(euler[0]);
		float s0 = std::sin(euler[0]);
		float c1 = std::cos(euler[1]);
		float s1 = std::sin(euler[1]);
		float c2 = std::cos(euler[2]);
		float s2 = std::sin(euler[2]);

		if (key == "026") {
			spdlog::get("wg")->info("'{:>16s}': decode_obb(ctr: {}, r={:.5f}, ext: {}, mpt: {:.2f})", key, ctr.transpose(), ctr.norm()/R1, ext.transpose(), metersPerTexel);
			spdlog::get("wg")->info("ctr0: {}", ctr0.transpose());
			spdlog::get("wg")->info("mpt: {}", metersPerTexel);
		}

		Matrix3f R; R <<
			c0*c2-c1*s0*s2, c1*c0*s2+c2*s0, s2*s1,
			-c0*s2-c2*c1*s0, c0*c1*c2-s0*s2, c2*s1,
			s1*s0, -c0*s1, c1;
		// R.setIdentity();

		return PackedOrientedBoundingBox {
			(authalic_to_ecef_(ctr) / R1).cast<float>(),
			Quaternionf{R}.conjugate(),
			// ext/R1/2,
			ext/R1,
			// (float)(metersPerTexel * 255 / R1) * 20
			(float)(metersPerTexel * 255 / R1) / 255
		};

	}

    void make_bb_map(const std::string& outPath, const std::string& rootDir, const GlobeOptions& gopts) {
        std::vector<GearthBoundingBoxMap::Item> items;

		int ntotal = 0;
		int nmissingNode = 0;
		int ngood = 0;
		int nbadObb = 0;

		// `file_exist` may be slow, so instead build hashset of existing node files upfront.
		std::unordered_set<std::string> nodeFiles;
		auto nodeFilesVec = list_dir(fmt::format("{}/node", rootDir));
		for (auto nodeFile : nodeFilesVec) {
			std::string nkey = nodeFile.substr(nodeFile.rfind("/")+1);
			// fmt::print("key: {} from {}\n", nkey, nodeFile);
			nodeFiles.insert(nkey);
		}
		fmt::print("listed {} / {} node files\n", nodeFiles.size(), nodeFilesVec.size());

		auto bulks = list_dir(fmt::format("{}/bulk", rootDir));
		for (int i=0; i<bulks.size(); i++) {
		// for (const auto& bulkPath : bulks) {
			const auto &bulkPath = bulks[i];
			if (i % 2500 == 0)
				fmt::print("on bulk {} / {}\n", i, bulks.size());

			std::ifstream ifs(bulkPath, std::ios_base::binary);

			rtpb::BulkMetadata bulk;
			if (!bulk.ParseFromIstream(&ifs)) {
				fmt::print(" - [#decode_node_to_tile] ERROR: failed to parse bulk from istream '{}'!\n", bulkPath);
				continue;
			}

			assert(bulk.head_node_center().size() == 3);
			Vector3d headNodeCenter = Eigen::Map<const Vector3d>{ &bulk.head_node_center()[0] };
			// fmt::print("headNodeCenter: {}\n", headNodeCenter.transpose());
			assert(bulk.meters_per_texel().size() >= 1);
			const float* metersPerTexel = &bulk.meters_per_texel()[0];
			std::string headNodePath = bulk.head_node_key().path();

			// fmt::print("{} vs {}\n", bulkPath, headNodePath);

			for (int ni=0; ni<bulk.node_metadata().size(); ni++) {
				const auto nodeMeta = bulk.node_metadata()[ni];
				ntotal++;

				// Bulk stores prefix of full node key, node meta stores postfix of that prefix in this bespoke binary int.
				// Decode that and concat here to get full octree coordinate.
				uint32_t pathAndFlags = nodeMeta.path_and_flags();
				uint32_t rlevel = 1 + (pathAndFlags & 3);
				pathAndFlags >>= 2;
				std::string rpath;
				for (uint32_t i=0; i<rlevel; i++) {
					rpath += '0' + (pathAndFlags & 7);
					pathAndFlags >>= 3;
				}
				uint32_t flags = pathAndFlags;

				std::string key = fmt::format("{}{}", headNodePath, rpath);

				// if (!file_exists(fmt::format("{}/node/{}", rootDir, key))) {
				if (nodeFiles.find(key) == nodeFiles.end()) {
					// fmt::print("missing node file '{}'\n", fmt::format("{}/node/{}", rootDir, key));
					nmissingNode++;
					continue;
				}

				if (auto pobb_ = decode_obb(key, nodeMeta, headNodeCenter, metersPerTexel[rlevel-1]); pobb_.has_value()) {
					ngood++;
					float geoErrorOnLevelUnit = metersPerTexel[rlevel];
					if (nodeMeta.has_meters_per_texel()) {
						geoErrorOnLevelUnit = nodeMeta.has_meters_per_texel();
					}

					items.push_back(GearthBoundingBoxMap::Item {
						OctreeCoordinate { key },
						*pobb_,
					});
				} else nbadObb++;

			}

		}

		fmt::print("make_bb_map(ntotal={}, nmissingNode={}, nbadObb={}, ngood={})\n",
				ntotal, nmissingNode, nbadObb, ngood);

		// exit(1);

        std::ofstream ofs(outPath, std::ios_base::binary);
        for (const auto& item : items) {
			ofs.write((const char*)&item, sizeof(GearthBoundingBoxMap::Item));
		}
        size_t len = ofs.tellp();
        SPDLOG_INFO("[make_bb_map] wrote '{}', {} entries, {:>5.2f}MB, {}B / item", outPath, items.size(), static_cast<double>(len) / (1 << 20), len/items.size());

		{


		}

					/*
                    // TODO: Map points into ECEF. Then use DLT to find matrix that aligns them.
                    Matrix<float, S * S, 3> pts = getEcefPointsOfTile(tileTlbr, elevDset);

					Vector3f mid = pts.array().colwise().mean();

					Matrix3f R = getSphericalLtp(mid);

					pts = pts * R;

                    Vector3d lo                 = pts.array().colwise().minCoeff().cast<double>();
                    Vector3d hi                 = pts.array().colwise().maxCoeff().cast<double>();

                    Matrix<double, 4, 3> fourPts;
                    fourPts << lo(0), lo(1), lo(2), hi(0), lo(1), lo(2), lo(0), hi(1), lo(2), lo(0), lo(1), hi(2);


                    SolvedTransform T = align_box_dlt(fourPts);

					T.t = R.cast<double>() * T.t;
					T.q = Quaterniond { R.cast<double>() * T.q };


                    items.push_back(GearthBoundingBoxMap::Item {
                        OctreeCoordinate { wmTileLevel, y, x },
                        PackedOrientedBoundingBox { T.t.cast<float>(), T.q.cast<float>().normalized(), T.s.cast<float>(),
                                            geoErrorOnLevelUnit }
                    });
					*/
    }

}

namespace wg {
namespace gearth {

    void maybe_make_gearth_bb_file(const std::string& rootDir, const GlobeOptions& gopts) {
#ifdef __EMSCRIPTEN__
        SPDLOG_INFO("not making bb file, this is emscripten");
#else
        std::string bbPath = rootDir + "/webgpuGlobe.bb";

        if (file_exists(bbPath)) {
            SPDLOG_INFO("not making bb file, '{}' already exists", bbPath);
            return;
        }

        make_bb_map(bbPath, rootDir, gopts);
#endif
    }

}
}

