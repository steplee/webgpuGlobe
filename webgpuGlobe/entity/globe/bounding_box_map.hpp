#pragma once

#include "bounding_box.h"
#include "globe.h"
#include <fstream>
#include <spdlog/sinks/stdout_color_sinks.h>

// #include <map>
#include <unordered_map>

namespace wg {


    template <class GlobeTypes>
	struct BoundingBoxMap {
		using Coordinate = typename GlobeTypes::Coordinate;
		using EncodedCoordinate = typename GlobeTypes::Coordinate::EncodedCoordinate;
        static constexpr int MaxChildren = Coordinate::MaxChildren;

        // WARNING: I don't think this will be packed correctly for different compilers/arches. So write simple serialize/deserialze
        // funcs
        struct __attribute__((packed)) Item {
            EncodedCoordinate coord;
            PackedOrientedBoundingBox obb;
        };

        // std::map<Coordinate, UnpackedOrientedBoundingBox> map;
        std::unordered_map<Coordinate, UnpackedOrientedBoundingBox> map;

		inline BoundingBoxMap(const std::string& loadFromPath, const GlobeOptions& opts) {
			if (logger = spdlog::get("bbMap"); logger == nullptr) logger = spdlog::stdout_color_mt("bbMap");

			// TODO: load it.
			{
				std::ifstream ifs(loadFromPath, std::ios_base::binary);

				while (ifs.good()) {
					Item item;

					size_t prior = ifs.tellg();
					ifs.read((char*)&item, sizeof(decltype(item)));
					size_t post = ifs.tellg();

					if (post - prior != sizeof(decltype(item))) {
						if (ifs.eof()) break;
						else throw std::runtime_error(fmt::format("failed to read an item ({} / {}), and not at end of file?", post-prior, sizeof(decltype(item))));
					}

					Coordinate coord { item.coord };
					map[coord] = UnpackedOrientedBoundingBox{item.obb};
				}
			}

            logger->info("loaded {} items.", map.size());

            // This is sort of an ugly design (mutable/in-place change after construction), but it is efficient and fits perfectly.
            setRootInformation();
            setTerminalInformation();
        }

        inline BoundingBoxMap() {
            if (logger = spdlog::get("bbMap"); logger == nullptr) logger = spdlog::stdout_color_mt("bbMap");
            logger->info("Constructing empty obb map.", map.size());
        }

        // void dumpToFile(const std::string& path);

        // An item is a root if it is on level z=0, or if it's parent does not exist in the map.
        inline std::vector<Coordinate> getRoots() const {
            std::vector<Coordinate> out;
            for (const auto& it : map) {
                if (it.second.root) out.push_back(it.first);
            }
            return out;
        }

        inline auto find(const Coordinate& c) {
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
                for (uint32_t childIndex = 0; childIndex < 8; childIndex++) {
                    Coordinate childCoord = item.first.child(childIndex);
                    if (map.find(childCoord) != map.end()) {
                        haveChild = true;
                        break;
                    }
                }
                if (!haveChild) {
                    item.second.terminal = true;
                    nterminal++;
                } else {
                    item.second.terminal = false;
				}
            }

            logger->info("marked {} / {} items terminal.", nterminal, map.size());
        }

        inline void setRootInformation() {
            uint32_t nroot = 0;
            logger->info("finding roots");

            for (auto& item : map) {
                const auto k = item.first;
                if (k.isBaseLevel()) {
					logger->info("have root {} (z=0)", item.first);
                    item.second.root = true;
                    nroot++;
                } else if (map.find(k.parent()) == map.end()) {
					logger->info("have root {}", item.first);
                    item.second.root = true;
                    nroot++;
                } else {
                    item.second.root = false;
				}
            }

            logger->info("marked {} / {} items roots.", nroot, map.size());
        }

        std::shared_ptr<spdlog::logger> logger;
    };
}
