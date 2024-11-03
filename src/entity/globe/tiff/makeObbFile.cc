#include "entity/globe/globe.h"
#include "util/gdalDataset.h"

#include "geo/conversions.h"
#include "geo/earth.hpp"

#include "entity/entity.h"

#include "util/align3d.hpp"

#include <sys/stat.h>

namespace {
    using namespace wg;

    static Matrix3d getLtp(const Vector3d& eye) {
		Vector3d f = eye.normalized();
		Vector3d r = -f.cross(Vector3d::UnitZ());
		Vector3d u = f.cross(r);
		Matrix3d out;
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

    constexpr int S = 2;
    Matrix<float, S * S, 3> getEcefPointsOfTile(const Vector4d& wmTlbr, GdalDataset& elevDset) {

        cv::Mat elevBuf;
        elevBuf.create(S, S, CV_16UC1);
        elevDset.getWm(wmTlbr, elevBuf);
        int16_t* elevData = (int16_t*)elevBuf.data;

        Vector4d tlbrUwm   = wmTlbr.array() / Earth::WebMercatorScale;

        Matrix<float, S * S, 3, RowMajor> out;

        for (int32_t yy = 0; yy < S; yy++) {
            for (int32_t xx = 0; xx < S; xx++) {
                int32_t ii = ((S - 1 - yy) * S) + xx;

                float xxx_ = static_cast<float>(xx) / static_cast<float>(S - 1);
                float yyy_ = static_cast<float>(yy) / static_cast<float>(S - 1);
                float xxx  = xxx_ * tlbrUwm(0) + (1 - xxx_) * tlbrUwm(2);
                float yyy  = yyy_ * tlbrUwm(1) + (1 - yyy_) * tlbrUwm(3);

                float z_   = (elevData[(yy)*elevBuf.cols + xx]);
				if (z_ < -1000) z_ = 0;
                // float zzz  = z_ / 8.f / Earth::WebMercatorScale;
                float zzz = z_ / Earth::WebMercatorScale;

                // SPDLOG_INFO("sample elev {} at {}", z_, wmTlbr.transpose());
                out.row(ii) << xxx, yyy, zzz;
            }
        }

        unit_wm_to_ecef(out.data(), S * S, out.data(), 3);
        return out;
    }

    void make_obb_map(const std::string& outPath, const std::string& colorPath, const GlobeOptions& gopts) {
        std::vector<ObbMap::Item> items;

        GdalDataset colorDset(colorPath);
        Vector4d dsetTlbr    = colorDset.getWmTlbrOfDataset();
        double dsetPixelSize = ((dsetTlbr(2) - dsetTlbr(0)) / colorDset.w);

        GdalDataset elevDset(gopts.getString("dtedPath"));

        // double WmDiameter    = 6.371e6 * M_PI * 2;
        double WmDiameter    = Earth::WebMercatorScale * 2;
        double wmLevelDouble = std::log2(WmDiameter / dsetPixelSize);

        uint32_t wmLevel     = std::floor(wmLevelDouble + .25);
        uint32_t wmLevel0    = wmLevel;

        double pixelSize     = WmDiameter / (1 << wmLevel);
        double tileSize      = pixelSize * 256;

        SPDLOG_INFO("[make_obb_map] choose deepest WM level {} ({:>5.2f} m/pix)", wmLevel, pixelSize);

        int tilesOnLastLevel = -1;

        // Generate tree.
        while (true) {
            int tilesOnLevel = 0;

            if (wmLevel < 8) {
                SPDLOG_WARN("[make_obb_map] stopping on pix level {}, too low", wmLevel);
                break;
            }
            uint32_t wmTileLevel = wmLevel - 8;

            Vector4d levelTlbrd  = dsetTlbr;

            Vector4i levelTlbr {
                (int)std::floor((dsetTlbr(0) / WmDiameter + .5) * (1 << wmTileLevel)),
                (int)std::floor((dsetTlbr(1) / WmDiameter + .5) * (1 << wmTileLevel)),
                // (int)std::ceil((dsetTlbr(2) / WmDiameter + .5) * (1 << wmTileLevel)),
                // (int)std::ceil((dsetTlbr(3) / WmDiameter + .5) * (1 << wmTileLevel)),
                1 + (int)std::floor((dsetTlbr(2) / WmDiameter + .5) * (1 << wmTileLevel)),
                1 + (int)std::floor((dsetTlbr(3) / WmDiameter + .5) * (1 << wmTileLevel)),
            };
            SPDLOG_INFO("[make_obb_map] on level {}, have dtlbr: {}", wmLevel, levelTlbrd.transpose());
            SPDLOG_INFO("[make_obb_map] on level {}, have itlbr: {}", wmLevel, levelTlbr.transpose());

            tilesOnLevel = (levelTlbr(2) - levelTlbr(0)) * (levelTlbr(3) - levelTlbr(1));
            SPDLOG_INFO("[make_obb_map] on level {}, have {} tiles", wmLevel, fmt::group_digits(tilesOnLevel));

            if (tilesOnLevel == 0) break;
            if (tilesOnLevel == tilesOnLastLevel) {
                SPDLOG_DEBUG("[make_obb_map] stopping prior to level {}, because num tiles ({}) is same as last level. This seems like a "
                             "good (lazy) stop criteria",
                             wmLevel, tilesOnLevel);
				break;
            }

            if (tilesOnLevel <= 32) {
                SPDLOG_WARN("[make_obb_map] stopping for testing...");
				break;
            }

            float geoErrorOnLevel = 1; // TODO:

            for (uint32_t y = levelTlbr(1); y < levelTlbr(3); y++) {
                for (uint32_t x = levelTlbr(0); x < levelTlbr(2); x++) {

                    Vector4d tileTlbr {
                        (static_cast<double>(x) / (1 << wmTileLevel) - .5) * WmDiameter,
                        (static_cast<double>(y) / (1 << wmTileLevel) - .5) * WmDiameter,
                        (static_cast<double>(x + 1) / (1 << wmTileLevel) - .5) * WmDiameter,
                        (static_cast<double>(y + 1) / (1 << wmTileLevel) - .5) * WmDiameter,
                    };
                    // SPDLOG_INFO("[make_obb_map] tile: {}", tileTlbr.transpose());

                    // TODO: Map points into ECEF. Then use DLT to find matrix that aligns them.
                    Matrix<float, S * S, 3> pts = getEcefPointsOfTile(tileTlbr, elevDset);

                    Vector3d lo                 = pts.array().colwise().minCoeff().cast<double>();
                    Vector3d hi                 = pts.array().colwise().maxCoeff().cast<double>();

                    Matrix<double, 4, 3> fourPts;
					// Matrix3d R = getLtp(pts.row(0));
                    fourPts << lo(0), lo(1), lo(2), hi(0), lo(1), lo(2), lo(0), hi(1), lo(2), lo(0), lo(1), hi(2);

                    SolvedTransform T = align_box_dlt(fourPts);

					// spdlog::get("wg")->info("from T.e:\n{}", T.s.transpose());
					// throw std::runtime_error("stop");

                    items.push_back(ObbMap::Item {
                        QuadtreeCoordinate { wmTileLevel, y, x },
                        PackedOrientedBoundingBox { T.t.cast<float>(), T.q.cast<float>().normalized(), T.s.cast<float>(),
                                            geoErrorOnLevel }
                    });
                }
            }

            if (wmTileLevel <= 0) {
                SPDLOG_DEBUG("[make_obb_map] stopping on pix level {}, too low", wmLevel);
                break;
            }
            tilesOnLastLevel = tilesOnLevel;
            wmLevel--;
        }

        std::ofstream ofs(outPath, std::ios_base::binary);
        for (const auto& item : items) {
			ofs.write((const char*)&item, sizeof(ObbMap::Item));
		}
        size_t len = ofs.tellp();
        SPDLOG_INFO("[make_obb_map] wrote '{}', {} entries, {:>5.2f}MB, {}B / item", outPath, items.size(), static_cast<double>(len) / (1 << 20), len/items.size());
    }

}

namespace wg {

    void maybe_make_tiff_obb_file(const std::string& tiffPath, const GlobeOptions& gopts) {
        std::string obbPath = tiffPath + ".bb";

        if (file_exists(obbPath)) {
            SPDLOG_INFO("not making obb file, '{}' already exists", obbPath);
            return;
        }

        // ObbMap obbMap = make_obb_map(tiffPath, gopts);
        // obbMap.dumpToFile(obbPath);
        make_obb_map(obbPath, tiffPath, gopts);
    }

}