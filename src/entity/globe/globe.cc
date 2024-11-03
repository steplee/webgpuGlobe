#include "globe.h"

#include <sys/stat.h>

#include "util/fmtEigen.h"

namespace wg {

		UnpackedOrientedBoundingBox::UnpackedOrientedBoundingBox(const PackedOrientedBoundingBox& pobb) : packed(pobb) {

			Matrix<float, 8, 3> pts0; pts0 <<
				-1, -1, -1,
				 1, -1, -1,
				 1,  1, -1,
				-1,  1, -1,
				-1, -1,  1,
				 1, -1,  1,
				 1,  1,  1,
				-1,  1,  1;

			// Affine3f T = Affine3f::fromPositionOrientationScale(packed.q.toRotationMatrix() * S , packed.p);
			Affine3f T;
			T.fromPositionOrientationScale(packed.p, packed.q.toRotationMatrix(), packed.extents);

			pts = (T * pts0.transpose()).transpose();
            // spdlog::get("wg")->info("created obb pts:\n{}",pts.rowwise() - pts.row(0));
            // spdlog::get("wg")->info("from T:\n{}", T.linear());
            // spdlog::get("wg")->info("from T.q:\n{}", packed.q.coeffs().transpose());
            // spdlog::get("wg")->info("from T.e:\n{}", packed.extents.transpose());
			// throw std::runtime_error("stop");


#warning "TODO"
			// assert(false); // TODO:
		}

		static float sdBox(const Vector3f& eye, const Vector3f& extents) {
			Vector3f q = eye.cwiseAbs() - extents;
			return q.array().max(Array3f::Zero()).matrix().norm() + std::min(0.f, q.maxCoeff());
		}

		static float sdBox_obb(const Vector3f& eye, const Vector3f& extents, const Vector3f& ctr, const Quaternionf& q) {
			Vector3f eye1 = q.conjugate() * eye - ctr;
			return sdBox(eye1, extents);
		}
	
        float UnpackedOrientedBoundingBox::computeSse(const Matrix4f& mvp, const Vector3f& eye) {

			float exteriorDistance = sdBox_obb(eye, packed.extents, packed.p, packed.q);

            spdlog::get("wg")->debug("computeSse exteriorDistance: {}", exteriorDistance);

			if (exteriorDistance <= 0) {
				return kBoundingBoxContainsEye;
			}

			assert(false);
			return 0;
		}

		Globe::Globe(AppObjects& ao, const GlobeOptions& opts) : ao(ao), opts(opts) {
		}

        Globe::~Globe() {
		}











		ObbMap::ObbMap(const std::string& loadFromPath, const GlobeOptions& opts) {
			if (logger = spdlog::get("obbMap"); logger == nullptr) logger = spdlog::stdout_color_mt("obbMap");

			// TODO: load it.
			{
				std::ifstream ifs(loadFromPath, std::ios_base::binary);

				// std::map<QuadtreeCoordinate, UnpackedOrientedBoundingBox> map;
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

		/*
		void ObbMap::dumpToFile(const std::string& path) {

			// TODO: ...
			Vector3f p;
			Quaternionf q;
			Vector3f extents;
			float geoError;
			std::ofstream ofs(path, std::ios_base::binary);
			for (const auto& kv : map) {
				ofs fucked.
			}
		
			struct stat buf;
			int ret = stat(path.c_str(), &buf);
			SPDLOG_INFO("created obb file '{}', {} entries, {:.1f}MB disk size", path.c_str(), map.size(), buf.st_size / (1<<20));
		}
		*/
}
