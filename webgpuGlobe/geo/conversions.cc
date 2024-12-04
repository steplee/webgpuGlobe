#include "conversions.h"
#include "earth.hpp"
#include <Eigen/Core>

namespace wg {
    using namespace Earth;

    void geodetic_to_ecef(double* out, int n, const double* llh) {
        for (int i = 0; i < n; i++) {
            const double aa = llh[i * 3 + 0], bb = llh[i * 3 + 1], cc = llh[i * 3 + 2];
            double cos_phi = std::cos(bb), cos_lamb = std::cos(aa);
            double sin_phi = std::sin(bb), sin_lamb = std::sin(aa);
            double n_phi   = a / std::sqrt(1 - e2 * sin_phi * sin_phi);

            out[i * 3 + 0] = (n_phi + cc) * cos_phi * cos_lamb;
            out[i * 3 + 1] = (n_phi + cc) * cos_phi * sin_lamb;
            out[i * 3 + 2] = (b2_over_a2 * n_phi + cc) * sin_phi;
        }
    }

    void unit_wm_to_geodetic(double* out, int n, const double* xyz) {
        for (int i = 0; i < n; i++) {
            out[i * 3 + 0] = xyz[i * 3 + 0] * M_PI;
            out[i * 3 + 1] = std::atan(std::exp(xyz[i * 3 + 1] * M_PI)) * 2 - M_PI / 2;
            out[i * 3 + 2] = xyz[i * 3 + 2] * M_PI;
        }
    }

    void unit_wm_to_ecef(double* out, int n, const double* xyz) {
        // OKAY: both unit_wm_to_geodetic and geodetic_to_ecef can operate in place.
        unit_wm_to_geodetic(out, n, xyz);
        geodetic_to_ecef(out, n, out);
    }

    void geodetic_to_ecef(float* out, int n, const float* llh, int stride) {
        for (int i = 0; i < n; i++) {
            const float aa = llh[i * stride + 0], bb = llh[i * stride + 1], cc = llh[i * stride + 2];
            float cos_phi = std::cos(bb), cos_lamb = std::cos(aa);
            float sin_phi = std::sin(bb), sin_lamb = std::sin(aa);
            float n_phi         = a / std::sqrt(1 - static_cast<float>(e2) * sin_phi * sin_phi);

            out[i * stride + 0] = (n_phi + cc) * cos_phi * cos_lamb;
            out[i * stride + 1] = (n_phi + cc) * cos_phi * sin_lamb;
            out[i * stride + 2] = (static_cast<float>(b2_over_a2) * n_phi + cc) * sin_phi;
        }
    }

    void unit_wm_to_geodetic(float* out, int n, const float* xyz, int stride) {
        for (int i = 0; i < n; i++) {
            out[i * stride + 0] = xyz[i * stride + 0] * static_cast<float>(M_PI);
            out[i * stride + 1]
                = std::atan(std::exp(xyz[i * stride + 1] * static_cast<float>(M_PI))) * 2 - static_cast<float>(M_PI) / 2;
            out[i * stride + 2] = xyz[i * stride + 2] * static_cast<float>(M_PI);
        }
    }

    void unit_wm_to_ecef(float* out, int n, const float* xyz, int stride) {
        // OKAY: both unit_wm_to_geodetic and geodetic_to_ecef can operate in place.
        unit_wm_to_geodetic(out, n, xyz, stride);
        geodetic_to_ecef(out, n, out, stride);
    }

    void wgs84_normal(double* out, const double* xyz) {
        using namespace Eigen;
        Vector3d p { xyz[0], xyz[1], xyz[2] };
        Vector3d n = ((p.array() / Eigen::Array3d { Earth::R1, Earth::R1, Earth::R2 })
                      * Eigen::Array3d { 1. / Earth::R1, 1. / Earth::R1, 1. / Earth::R2 })
                         .matrix()
                         .normalized();
		out[0] = n[0];
		out[1] = n[1];
		out[2] = n[2];
    }

	void ecef_to_geodetic(double* out, int n, const double* x) {
		for (int i = 0; i < n; i++) {
			const double xx = x[i * 3 + 0], yy = x[i * 3 + 1], zz = x[i * 3 + 2];

			out[i * 3 + 0] = std::atan2(yy, xx);

			double k  = 1. / (1. - e2);
			double z  = zz;
			double z2 = z * z;
			double p2 = xx * xx + yy * yy;
			double p  = std::sqrt(p2);
			for (int j = 0; j < 2; j++) {
				const double c_i = std::pow(((1 - e2) * z2) * (k * k) + p2, 1.5) / e2;
				k                = (c_i + (1 - e2) * z2 * pow(k, 3)) / (c_i - p2);
			}
			out[i * 3 + 1] = std::atan2(k * z, p);

			double rn        = a / std::sqrt(1. - e2 * pow(sin(out[i * 3 + 1]), 2.));
			double sinabslat = sin(abs(out[i * 3 + 1]));
			double coslat    = cos(out[i * 3 + 1]);
			out[i * 3 + 2]   = (abs(z) + p - rn * (coslat + (1 - e2) * sinabslat)) / (coslat + sinabslat);

			// Never allow nan.
			if (std::isnan(out[i*3+0]) or std::isnan(out[i*3+1]) or std::isnan(out[i*3+2])) {
				out[i*3+0] = out[i*3+1] = out[i*3+2] = 0;
			}
		}
	}

	void ecef_to_geodetic(float* out, int n, const float* x) {
		for (int i = 0; i < n; i++) {
			const float xx = x[i * 3 + 0], yy = x[i * 3 + 1], zz = x[i * 3 + 2];

			out[i * 3 + 0] = std::atan2(yy, xx);

			float k  = 1. / (1. - static_cast<float>(e2));
			float z  = zz;
			float z2 = z * z;
			float p2 = xx * xx + yy * yy;
			float p  = std::sqrt(p2);
			for (int j = 0; j < 2; j++) {
				const float c_i = std::pow(((1 - static_cast<float>(e2)) * z2) * (k * k) + p2, 1.5f) / static_cast<float>(e2);
				k                = (c_i + (1 - static_cast<float>(e2)) * z2 * pow(k, 3)) / (c_i - p2);
			}
			out[i * 3 + 1] = std::atan2(k * z, p);

			float rn        = a / std::sqrt(1. - static_cast<float>(e2) * pow(sin(out[i * 3 + 1]), 2.f));
			float sinabslat = sin(abs(out[i * 3 + 1]));
			float coslat    = cos(out[i * 3 + 1]);
			out[i * 3 + 2]   = (abs(z) + p - rn * (coslat + (1 - static_cast<float>(e2)) * sinabslat)) / (coslat + sinabslat);

			// Never allow nan.
			if (std::isnan(out[i*3+0]) or std::isnan(out[i*3+1]) or std::isnan(out[i*3+2])) {
				out[i*3+0] = out[i*3+1] = out[i*3+2] = 0;
			}
		}
	}


}
