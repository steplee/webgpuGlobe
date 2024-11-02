#include "globe.h"

#include <sys/stat.h>

namespace wg {

		UnpackedOrientedBoundingBox::UnpackedOrientedBoundingBox(const PackedOrientedBoundingBox& pobb) {
#warning "TODO"
			// assert(false); // TODO:
		}
	
        float UnpackedOrientedBoundingBox::sse(const Matrix4f& mvp, const Vector3f& eye) {
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
