#include "gdalDataset.h"
#include "geo/earth.hpp"

#include <opencv2/imgproc.hpp>
#include <opencv2/core/types_c.h>
// #include <opencv2/highgui.hpp>

#include <algorithm>

#include <spdlog/spdlog.h>

namespace {
// Convert the int16_t -> uint16_t.
// Change nodata value from -32768 to 0. Clamp min to 0.
// Clamp max to 8191, then multiply by 8.
// The final mult is done to get 8 ticks/meter value-precision, so that warps have higher accuracy.
// That means that the user should convert to float, then divide by 8 after accessing the dataset.
void transform_gmted(void* buf, int h, int w, int gdalType) {
	if (gdalType == GDT_Float32) {
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++) {
				float gmted_val = ((float*)buf)[y * w + x];

				if (gmted_val < 0) gmted_val = 0;
				if (gmted_val > 8191) gmted_val = 8191;

				float val = gmted_val * 1;

				((float*)buf)[y * w + x] = val;
			}
	} else {
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++) {
				int16_t gmted_val = ((int16_t*)buf)[y * w + x];

				if (gmted_val < 0) gmted_val = 0;
				if (gmted_val > 20000) gmted_val = 20000;

				uint16_t val = gmted_val * 1;

				((int16_t*)buf)[y * w + x] = val;
			}
	}
}

std::once_flag flag__;
}  // namespace

namespace wg {

GdalDataset::GdalDataset(const std::string& path, bool isTerrain) : isTerrain(isTerrain), path(path) {
	std::call_once(flag__, &GDALAllRegister);

	dset = (GDALDataset*)GDALOpen(path.c_str(), GA_ReadOnly);
	if (dset == nullptr) throw std::runtime_error("failed to open dataset: " + path);

	nbands = dset->GetRasterCount() >= 3 ? 3 : 1;
	assert(nbands <= 4);
	auto band = dset->GetRasterBand(0 + 1);
	for (int i = 0; i < nbands; i++) bands[i] = dset->GetRasterBand(1 + i);
	// std::cout << " - nbands: " << nbands << "\n";

	gdalType = dset->GetRasterBand(1)->GetRasterDataType();
	// fmt::print(" - file {} gdalType {} '{}'\n", path, gdalType, GDALGetDataTypeName(gdalType));
	if (not(gdalType == GDT_Byte or gdalType == GDT_Int16 or gdalType == GDT_Float32)) {
		SPDLOG_CRITICAL(" == ONLY uint8_t/int16_t/float32 dsets supported right now.");
		exit(1);
	}
	if (nbands == 3 and gdalType == GDT_Byte) {
		eleSize = 1;
	} else if (nbands == 1 and gdalType == GDT_Byte) {
		eleSize = 1;
	} else if (nbands == 1 and gdalType == GDT_Int16) {
		eleSize = 2;
	} else if (nbands == 1 and gdalType == GDT_Float32) {
		throw std::runtime_error(
			"geotiff is float32 input is not supported yet. I have yet to implement terrain conversion from f32 -> "
			"TERRAIN_2x8");
		eleSize = 4;
	} else assert(false);

	internalCvType = nbands == 1 ? CV_8UC1 : nbands == 3 ? CV_8UC3 : nbands == 4 ? CV_8UC4 : -1;
	assert(internalCvType != -1);
	if (isTerrain) {
		assert(gdalType == GDT_Int16);
		// internalCvType = CV_16UC1;
		internalCvType = CV_32FC1;
	}


	if (not isTerrain) {
		int blockSizeX, blockSizeY;
		bands[0]->GetBlockSize(&blockSizeX, &blockSizeY);
		SPDLOG_TRACE("dset '{}' :: block size : {} {}", path, blockSizeX, blockSizeY);
		// assert(blockSizeX == 256);
		// assert(blockSizeY == 256);
	}

	char *projStr;
	dset->GetSpatialRef()->exportToProj4(&projStr);
	SPDLOG_DEBUG("dset '{}' :: proj: '{}'", path, projStr);
	assert(std::string{projStr}.find("=merc") != std::string::npos);

	double g[6];
	dset->GetGeoTransform(g);
	RowMatrix3d native_from_pix_;
	native_from_pix_ << g[1], g[2], g[0], g[4], g[5], g[3], 0, 0, 1;
	RowMatrix3d pix_from_native_ = native_from_pix_.inverse();
	pix_from_native				 = pix_from_native_.topRows<2>();
	native_from_pix				 = native_from_pix_.topRows<2>();
	w							 = dset->GetRasterXSize();
	h							 = dset->GetRasterYSize();

	// Need gdal 3.10+ :(
	// SPDLOG_INFO(" - Dataset is thread-safe: {}", dset->IsThreadSafe());
	// exit(1);

	// Ensure that the dataset is projected and aligned to WM quadtree.
	if (not isTerrain) {
		double bestErr = 9e9;
		int matchedPixelLevel = -1;
		double matchedPixelSize = -1;
		Vector2d pt_a = native_from_pix * Vector3d {0,0,1};
		Vector2d pt_b = native_from_pix * Vector3d {1,0,1};
		double pixelScaleWm = (pt_b - pt_a).norm();
		for (int lvl=0; lvl<30; lvl++) {
			double levelCellScaleWm = (Earth::WebMercatorScale * 2) / (1 << lvl);
			double err = std::abs(levelCellScaleWm - pixelScaleWm);
			if (err < bestErr) {
				bestErr = err;
				matchedPixelLevel = lvl;
				matchedPixelSize = levelCellScaleWm;
			}
		}
		SPDLOG_INFO(" - From:");
		SPDLOG_INFO("          pixelScaleWm : {:.5f}", pixelScaleWm);
		SPDLOG_INFO("          matched level: {}", matchedPixelLevel);
		SPDLOG_INFO("             pixel size: {}, with err {}", matchedPixelSize, bestErr);
		SPDLOG_INFO("              rel error: {}", bestErr/matchedPixelSize);
		if (matchedPixelLevel != -1 and bestErr / matchedPixelLevel < .001) {
			SPDLOG_INFO(" - The dataset IS aligned to the WM quadtree.");
		} else {
			SPDLOG_INFO(" - The dataset IS NOT aligned to the WM quadtree.");
		}

		Vector4d tlbrWm;
		{
			RowMatrix42d pts;
			double dw = w, dh = h;
			pts << (native_from_pix * Vector3d{0, 0, 1.}).transpose(),
				(native_from_pix * Vector3d{dw, 0, 1.}).transpose(),
				(native_from_pix * Vector3d{dw, dh, 1.}).transpose(),
				(native_from_pix * Vector3d{0, dh, 1.}).transpose();
			tlbrWm = Vector4d{pts.col(0).minCoeff(), pts.col(1).minCoeff(), pts.col(0).maxCoeff(), pts.col(1).maxCoeff()};
		}
	}




}

GdalDataset::~GdalDataset() {
	if (dset) GDALClose(dset);
	dset = 0;
}

int GdalDataset::getNumOverviews() const { return bands[0]->GetOverviewCount(); }

Vector4d GdalDataset::getWm(const Vector4d& tlbrWm, cv::Mat& out) {
	RowMatrix42d pts;
	pts << (pix_from_native * Vector3d{tlbrWm(0), tlbrWm(1), 1.}).transpose(),
		(pix_from_native * Vector3d{tlbrWm(2), tlbrWm(1), 1.}).transpose(),
		(pix_from_native * Vector3d{tlbrWm(2), tlbrWm(3), 1.}).transpose(),
		(pix_from_native * Vector3d{tlbrWm(0), tlbrWm(3), 1.}).transpose();
	Vector4d tlbrPix{pts.col(0).minCoeff(), pts.col(1).minCoeff(), pts.col(0).maxCoeff(), pts.col(1).maxCoeff()};
	return getPix(tlbrPix, out);
}

Vector4d GdalDataset::getGlobalTileBoundsWm(int z, int y, int x) {
	// int zz = z - 8;
	int zz = z;
	// SPDLOG_INFO("getGlobalTileBoundsWm: {} {} {}\n", x,y,z);
	return Vector4d {
		(static_cast<double>(x  ) / (1 << zz) * 2 - 1) * Earth::WebMercatorScale,
		(static_cast<double>(y  ) / (1 << zz) * 2 - 1) * Earth::WebMercatorScale,
		(static_cast<double>(x+1) / (1 << zz) * 2 - 1) * Earth::WebMercatorScale,
		(static_cast<double>(y+1) / (1 << zz) * 2 - 1) * Earth::WebMercatorScale
	};
}

void GdalDataset::getGlobalTile(cv::Mat& out, uint32_t z, uint32_t y, uint32_t x) {
	Vector4d tlbr = getGlobalTileBoundsWm(z,y,x);
	getWm(tlbr, out);
}

Vector4d GdalDataset::getPix(const Vector4d& tlbrPix, cv::Mat& out) {
	// SPDLOG_INFO("getPix :: {} size {} {} c {}", tlbrPix.transpose(), out.rows, out.cols, out.channels());
	int outh = out.rows, outw = out.cols;

	int out_c = out.channels();
	if (nbands >= 3 and out_c == 1) out.create(outh, outw, CV_8UC3);

	Vector2d tl = tlbrPix.head<2>();
	Vector2d br = tlbrPix.tail<2>();
	if (tl(0) > br(0)) std::swap(tl(0), br(0));
	if (tl(1) > br(1)) std::swap(tl(1), br(1));
	int xoff = tl(0);
	int yoff = tl(1);
	// int xsize = (int)(.5 + br(0) - tl(0));
	// int ysize = (int)(.5 + br(1) - tl(1));
	int xsize = (int)(br(0) - tl(0));
	int ysize = (int)(br(1) - tl(1));
	if (xsize == 0) xsize++;
	if (ysize == 0) ysize++;

	auto gdalOutputType = gdalType;
	int eleSizeOut = 1;
	if (out.type() == CV_8UC1) gdalOutputType = GDT_Byte;
	if (out.type() == CV_8UC3) gdalOutputType = GDT_Byte;
	if (out.type() == CV_8UC4) gdalOutputType = GDT_Byte;
	if (out.type() == CV_16SC1) gdalOutputType = GDT_Int16, eleSizeOut=2;
	if (out.type() == CV_16SC3) gdalOutputType = GDT_Int16, eleSizeOut=2;
	if (out.type() == CV_16SC4) gdalOutputType = GDT_Int16, eleSizeOut=2;
	if (out.type() == CV_32FC1) gdalOutputType = GDT_Float32, eleSizeOut=4;

	GDALRasterIOExtraArg arg;
	INIT_RASTERIO_EXTRA_ARG(arg);
	arg.nVersion = RASTERIO_EXTRA_ARG_CURRENT_VERSION;
	arg.eResampleAlg = GRIORA_Bilinear;
	if (bilinearSampling) arg.eResampleAlg = GRIORA_Bilinear;
	else arg.eResampleAlg = GRIORA_NearestNeighbour;

	if (xoff > 0 and xoff + xsize < w and yoff > 0 and yoff + ysize < h) {
		// FIXME: use greater precision with bFloatingPointWindowValidity


		if (useSubpixelOffsets) {
			arg.dfXOff = tl(0);
			arg.dfYOff = tl(1);
			arg.dfXSize = br(0) - tl(0);
			arg.dfYSize = br(1) - tl(1);
			arg.bFloatingPointWindowValidity = 1;
			// SPDLOG_INFO(" - '{}' subpixel: {} {} {} {} \n", path, arg.dfXOff, arg.dfYOff, arg.dfXSize, arg.dfYSize);
		} else {
			arg.bFloatingPointWindowValidity = 0;
		}

		// auto err = dset->RasterIO(GF_Read, xoff, yoff, xsize, ysize, out.data, outw, outh, gdalType, nbands, nullptr,
								  // eleSize * nbands, eleSize * nbands * outw, eleSize, &arg);
		auto err = dset->RasterIO(GF_Read, xoff, yoff, xsize, ysize, out.data, outw, outh, gdalOutputType, nbands, nullptr,
								  eleSizeOut * nbands, eleSizeOut * nbands * outw, eleSizeOut, &arg);
		// SPDLOG_INFO(" - err: {}\n", err);

		// TODO If converting from other terrain then GMTED, must modify here
		if (isTerrain) transform_gmted((uint16_t*)out.data, outh, outw, gdalOutputType);

		/*
		if (isTerrain and out.type() == CV_32FC1) {
			out.flags = CV_16UC1;
			out.convertTo(out, CV_32FC1, 1./8.);
		}
		*/

		if (err != CE_None) return Vector4d::Zero();

	} else if (xoff + xsize >= 1 and xoff < w and yoff + ysize >= 1 and yoff < h) {
		// case where there is partial overlap
		// WARNING: This is not correct. Border tiles have artifacts at corners.
		Eigen::Vector4i inner{std::max(0, xoff), std::max(0, yoff), std::min(w - 1, xoff + xsize),
							  std::min(h - 1, yoff + ysize)};
		float			sx		= ((float)outw) / xsize;
		float			sy		= ((float)outh) / ysize;
		int				inner_w = inner(2) - inner(0), inner_h = inner(3) - inner(1);
		int				read_w = (inner(2) - inner(0)) * sx, read_h = (inner(3) - inner(1)) * sy;
		// printf(" - partial bbox: %dh %dw %dc\n", read_h, read_w, out.channels()); fflush(stdout);
		if (read_w <= 0 or read_h <= 0) return Vector4d::Zero();

		cv::Mat tmp;
		// tmp.create(read_h, read_w, internalCvType);
		tmp.create(read_h, read_w, out.type());

		// auto err = dset->RasterIO(GF_Read, inner(0), inner(1), inner_w, inner_h, tmp.data, read_w, read_h, gdalType,
								  // nbands, nullptr, eleSize * nbands, eleSize * nbands * read_w, eleSize * 1, nullptr);
		auto err = dset->RasterIO(GF_Read, inner(0), inner(1), inner_w, inner_h, tmp.data, read_w, read_h, gdalOutputType,
								  nbands, nullptr, eleSizeOut * nbands, eleSizeOut * nbands * read_w, eleSizeOut * 1, &arg);
		if (err != CE_None) return Vector4d::Zero();

		// TODO If converting from other terrain then GMTED, must modify here
		if (isTerrain) transform_gmted((uint16_t*)tmp.data, tmp.rows, tmp.cols, gdalOutputType);

		float in_pts[8]	 = {0, 0, sx * inner_w, 0, 0, sy * inner_h, sx * inner_w, sy * inner_h};
		float out_pts[8] = {sx * (inner(0) - xoff), sy * (inner(1) - yoff), sx * (inner(2) - xoff),
							sy * (inner(1) - yoff), sx * (inner(0) - xoff), sy * (inner(3) - yoff),
							sx * (inner(2) - xoff), sy * (inner(3) - yoff)};

		// xoff = inner(0); yoff = inner(1);
		// xsize = inner_w; ysize = inner_h;

		cv::Mat HH = cv::getPerspectiveTransform((const cv::Point2f*)in_pts,(const cv::Point2f*)out_pts);

		cv::warpPerspective(tmp, out, HH, cv::Size{out.cols, out.rows});

	} else {
		// memset(out.data, 0, out.rows*out.cols*out.channels());
		out = cv::Scalar{0};
		return Vector4d::Zero();
	}

	Vector2d tl_sampled = Vector2d{xoff, yoff};
	// Vector2d br_sampled = Vector2d { xoff+xsize, yoff+ysize };
	Vector2d br_sampled = Vector2d{xoff + xsize - 1, yoff + ysize - 1};
	// Vector2d br_sampled = Vector2d { xoff+xsize+1, yoff+ysize+1 };

	// Vector2d tl_sampled = Vector2d { xoff-.5, yoff-.5 };
	// Vector2d br_sampled = Vector2d { xoff+xsize+.5, yoff+ysize+.5 };

	// Vector2d br_sampled = pix2prj * Vector3d { xoff+xsize-1, yoff+ysize-1, 1 };
	if (tl_sampled(0) > br_sampled(0)) std::swap(tl_sampled(0), br_sampled(0));
	if (tl_sampled(1) > br_sampled(1)) std::swap(tl_sampled(1), br_sampled(1));
	return Vector4d{tl_sampled(0), tl_sampled(1), br_sampled(0), br_sampled(1)};
}


Vector4d GdalDataset::getWmTlbrOfDataset() {
	Eigen::Matrix<double,4,3> pts; pts <<
		0, 0, 1,
		w, 0, 1,
		w, h, 1,
		0, h, 1;
	Eigen::Matrix<double,4,2> pts1 = pts * native_from_pix.transpose();

	return Vector4d {
		pts1.col(0).minCoeff(),
		pts1.col(1).minCoeff(),
		pts1.col(0).maxCoeff(),
		pts1.col(1).maxCoeff(),
	};
}



ReprojectPoints::~ReprojectPoints() {
	if (prj) OCTDestroyCoordinateTransformation(prj);
}
ReprojectPoints::ReprojectPoints(int epsgFrom, int epsgTo) {
	OGRSpatialReference srFrom, srTo;
	srFrom.importFromEPSG(epsgFrom);
	srTo.importFromEPSG(epsgTo);

	// https://gdal.org/tutorials/osr_api_tut.html#crs-and-axis-order
	srFrom.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
	srTo.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
	prj = OGRCreateCoordinateTransformation(&srFrom, &srTo);
}

MatrixXd ReprojectPoints::operator()(MatrixXd&& x) {
	if (x.cols() == 1) x = x.transpose();
	if (x.cols() == 3)
		prj->Transform(x.rows(), x.data(), x.data() + x.rows(), x.data() + x.rows() * 2, nullptr);
	else if (x.cols() == 2)
		prj->Transform(x.rows(), x.data(), x.data() + x.rows(), nullptr);
	else {
		assert(false);
	}
	return x;
}

}
