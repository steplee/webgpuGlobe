#include "tiff.h"

#include "util/gdalDataset.h"

#include "geo/conversions.h"
#include "geo/earth.hpp"

#include "entity/entity.h"

#include "util/align3d.hpp"

#include <sys/stat.h>

namespace {
    using namespace wg;
    using namespace wg::tiff;

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

    constexpr int S = 2;
    Matrix<float, S * S, 3> getEcefPointsOfTile(const Vector4d& wmTlbr, GdalDataset& elevDset) {

        cv::Mat elevBuf;
        elevBuf.create(S, S, CV_16SC1);
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

    void make_bb_map(const std::string& outPath, const std::string& colorPath, const GlobeOptions& gopts) {
        std::vector<TiffBoundingBoxMap::Item> items;

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

        SPDLOG_INFO("[make_bb_map] choose deepest WM level {} ({:>5.2f} m/pix)", wmLevel, pixelSize);

        int tilesOnLastLevel = -1;

        // Generate tree.
        while (true) {
            int tilesOnLevel = 0;

            if (wmLevel < 8) {
                SPDLOG_WARN("[make_bb_map] stopping on pix level {}, too low", wmLevel);
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
            SPDLOG_INFO("[make_bb_map] on level {}, have dtlbr: {}", wmLevel, levelTlbrd.transpose());
            SPDLOG_INFO("[make_bb_map] on level {}, have itlbr: {}", wmLevel, levelTlbr.transpose());

            tilesOnLevel = (levelTlbr(2) - levelTlbr(0)) * (levelTlbr(3) - levelTlbr(1));
            SPDLOG_INFO("[make_bb_map] on level {}, have {} tiles", wmLevel, fmt::group_digits(tilesOnLevel));

            if (tilesOnLevel == 0) break;
            if (tilesOnLevel == tilesOnLastLevel) {
                SPDLOG_DEBUG("[make_bb_map] stopping prior to level {}, because num tiles ({}) is same as last level. This seems like a "
                             "good (lazy) stop criteria",
                             wmLevel, tilesOnLevel);
				break;
            }

			/*
            if (tilesOnLevel <= 32) {
                SPDLOG_WARN("[make_bb_map] stopping for testing...");
				break;
            }
			*/

            float geoErrorOnLevelMeters = (1/(M_PI*2*2)) * Earth::R1 / (1 << wmTileLevel); // TODO:
			geoErrorOnLevelMeters *= .5f;
            float geoErrorOnLevelUnit = geoErrorOnLevelMeters / Earth::R1;

            for (uint32_t y = levelTlbr(1); y < levelTlbr(3); y++) {
                for (uint32_t x = levelTlbr(0); x < levelTlbr(2); x++) {

                    Vector4d tileTlbr {
                        (static_cast<double>(x) / (1 << wmTileLevel) - .5) * WmDiameter,
                        (static_cast<double>(y) / (1 << wmTileLevel) - .5) * WmDiameter,
                        (static_cast<double>(x + 1) / (1 << wmTileLevel) - .5) * WmDiameter,
                        (static_cast<double>(y + 1) / (1 << wmTileLevel) - .5) * WmDiameter,
                    };
                    // SPDLOG_INFO("[make_bb_map] tile: {}", tileTlbr.transpose());

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

					// spdlog::get("wg")->info("from T.e:\n{}", T.s.transpose());
					// throw std::runtime_error("stop");

                    items.push_back(TiffBoundingBoxMap::Item {
                        QuadtreeCoordinate { wmTileLevel, y, x },
                        PackedOrientedBoundingBox { T.t.cast<float>(), T.q.cast<float>().normalized(), T.s.cast<float>(),
                                            geoErrorOnLevelUnit }
                    });
                }
            }

            if (wmTileLevel <= 0) {
                SPDLOG_DEBUG("[make_bb_map] stopping on pix level {}, too low", wmLevel);
                break;
            }
            tilesOnLastLevel = tilesOnLevel;
            wmLevel--;
        }

        std::ofstream ofs(outPath, std::ios_base::binary);
        for (const auto& item : items) {
			ofs.write((const char*)&item, sizeof(TiffBoundingBoxMap::Item));
		}
        size_t len = ofs.tellp();
        SPDLOG_INFO("[make_bb_map] wrote '{}', {} entries, {:>5.2f}MB, {}B / item", outPath, items.size(), static_cast<double>(len) / (1 << 20), len/items.size());
    }

}

namespace wg {
namespace tiff {

    void maybe_make_tiff_bb_file(const std::string& tiffPath, const GlobeOptions& gopts) {
#ifdef __EMSCRIPTEN__
        SPDLOG_INFO("not making bb file, this is emscripten");
#else
        std::string bbPath = tiffPath + ".bb";

        if (file_exists(bbPath)) {
            SPDLOG_INFO("not making bb file, '{}' already exists", bbPath);
            return;
        }

        make_bb_map(bbPath, tiffPath, gopts);
#endif
    }

}
}
