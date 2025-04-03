#pragma once


#include "../dataloader.hpp"
#include "../globe.h"

#include "gearth.h"
#include "decode/rt_decode.hpp"

#include <opencv2/imgproc.hpp>
#include "geo/conversions.h"


namespace wg {
namespace gearth {

	using GenericGearthDataLoader = BaseDataLoader<GearthTypes>;

    struct DiskGearthDataLoader : public DiskDataLoader<DiskGearthDataLoader, GearthTypes> {


        // Note that obbMap is initialized on the calling thread synchronously
        inline DiskGearthDataLoader(const GlobeOptions& opts)
            : DiskDataLoader(opts, opts.getString("gearthPath") + "/webgpuGlobe.bb") {

			root = opts.getString("gearthPath");
			if (root.length() and root.back() == '/') root.pop_back();

			colorMult = opts.getDouble("colorMult");
			start();

        }

        inline virtual ~DiskGearthDataLoader() {
        }


        inline void loadActualData(TileData& item, const TheCoordinate& c) {
            // Set img.
            // Set vertexData.
            // Set indices.

			std::string path { fmt::format("{}/node/{}", root, c.s) };
	std::ifstream ifs(path);


	// fmt::print(" - Decoding {}\n", fname);
	if (decode_node_to_tile(ifs, item.dtd, false)) {
		fmt::print(" - [#loadTile] decode '{}' failed, skipping tile.\n", path);
		// tile->loaded = true;
		// return dtd.meshes.size();
	}

	if (item.dtd.meshes.size() == 0) {
		// tile->loaded = true; // loaded, but empty
		return;
	}

	uint32_t total_meshes = item.dtd.meshes.size();

	Eigen::Map<Eigen::Matrix4d> mm(item.dtd.modelMat);
	constexpr double R1         = (6378137.0);
	constexpr double R1i = 1.0 / R1;
	Eigen::Matrix4d scale_m; scale_m <<
		R1i, 0, 0, 0,
		0, R1i, 0, 0,
		0, 0, R1i, 0,
		0, 0, 0, 1;
	mm = scale_m * mm;

	item.model.resize(16);
	for (int i=0; i<16; i++) item.model[i] = mm(i/4, i%4);

			/*
			Vector4d tlbrWm = c.getWmTlbr();
			logTrace1("wm tlbr {}", tlbrWm.transpose());


			constexpr uint32_t E = 8;


			cv::Mat mat0, dtedMat;
			mat0.create(256,256, CV_8UC3);
			// dtedMat.create(E,E, CV_16UC1);
			dtedMat.create(E,E, CV_32FC1);
			colorDset->getWm(tlbrWm, mat0);
			if (colorMult != 1) {
				cv::addWeighted(mat0, colorMult, mat0, 0, 0, mat0);
			}


			Vector4d elevTlbrWm { tlbrWm };
			// adapt to gdal raster model FIXME: improve this?
			double ww = elevTlbrWm(2) - elevTlbrWm(0), hh = elevTlbrWm(3) - elevTlbrWm(1);
			elevTlbrWm(2) += (ww) / (E);
			elevTlbrWm(3) += (hh) / (E);
			// elevTlbrWm(0) -= (ww) / (E);
			// elevTlbrWm(1) -= (hh) / (E);
			dtedDset->getWm(elevTlbrWm, dtedMat);

			Image img;
			img.allocate(256,256,4);
			cv::Mat mat1(256, 256, CV_8UC4, img.data());
			cv::cvtColor(mat0, mat1, cv::COLOR_RGB2RGBA);

			for (int y=0; y<mat1.rows; y++) {
				for (int x=0; x<mat1.rows; x++) {
					mat1.data[y*mat1.step + x*4 + 3] = 255;
				}
			}

			item.img = std::move(img);

			item.indices.reserve((E-1)*(E-1)*3*2);
			for (uint16_t y=0; y < E-1; y++) {
				for (uint16_t x=0; x < E-1; x++) {
					uint16_t a = (y  ) * E + (x  );
					uint16_t b = (y  ) * E + (x+1);
					uint16_t c = (y+1) * E + (x+1);
					uint16_t d = (y+1) * E + (x  );

					item.indices.push_back(a);
					item.indices.push_back(b);
					item.indices.push_back(c);

					item.indices.push_back(c);
					item.indices.push_back(d);
					item.indices.push_back(a);
				}
			}

			const float* elevData = (const float*) dtedMat.data;
			// const int16_t* elevData = (const int16_t*) dtedMat.data;

			Vector4d tlbrUwm   = tlbrWm.array() / Earth::WebMercatorScale;
			Matrix<float, E * E, 3, RowMajor> positions;
			for (uint16_t y=0; y < E; y++) {
				for (uint16_t x=0; x < E; x++) {

					float xx_ = static_cast<float>(x) / static_cast<float>(E - 1);
					float yy_ = static_cast<float>(y) / static_cast<float>(E - 1);

					// Inset, may be helpful for debugging.
					// xx_ = xx_ * .9f + .05f, yy_ = yy_ * .9f + .05f;

					float xx  = (1 - xx_) * tlbrUwm(0) + xx_ * tlbrUwm(2);
					float yy  = (1 - yy_) * tlbrUwm(1) + yy_ * tlbrUwm(3);
					// float zz_   = (elevData[y*dtedMat.cols + x]);
					float zz_   = (elevData[(E-y-1)*dtedMat.cols + x]);
					if (zz_ < -1000) zz_ = 0;
					float zz = zz_ / Earth::WebMercatorScale;

					int32_t ii = ((E - 1 - y) * E) + x;
					// int32_t ii = (y * E) + x;
					positions.row(ii) << xx, yy, zz;
				}
			}

			unit_wm_to_ecef(positions.data(), E * E, positions.data(), 3);
			// spdlog::get("gearthRndr")->info("mapped ECEF coords:\n{}", positions);

			std::vector<float> verts;
			int vertWidth = 3+2+3;
			verts.resize(E*E*vertWidth * sizeof(float));
			for (uint32_t y=0; y < E; y++) {
				for (uint32_t x=0; x < E; x++) {
					uint32_t i = y*E+x;

					verts[i*vertWidth + 0] = positions(i,0);
					verts[i*vertWidth + 1] = positions(i,1);
					verts[i*vertWidth + 2] = positions(i,2);
					// verts[i*vertWidth + 3] = 1.f - static_cast<float>(x) / static_cast<float>(E - 1);
					// verts[i*vertWidth + 4] = 1.f - static_cast<float>(y) / static_cast<float>(E - 1);
					verts[i*vertWidth + 3] = static_cast<float>(x) / static_cast<float>(E - 1);
					verts[i*vertWidth + 4] = static_cast<float>(y) / static_cast<float>(E - 1);
					verts[i*vertWidth + 5] = 0;
					verts[i*vertWidth + 6] = 0; // todo: compute normals.
					verts[i*vertWidth + 7] = 0;
				}
			}

			size_t vertexBufSize = verts.size() * sizeof(float);
			item.vertexData.resize(vertexBufSize);
			std::memcpy(item.vertexData.data(), verts.data(), vertexBufSize);
			*/
        }


		// std::shared_ptr<GdalDataset> colorDset;
		// std::shared_ptr<GdalDataset> dtedDset;
		double colorMult = 1;
		std::string root;
    };

}
}
