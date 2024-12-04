#pragma once
#include <cmath>

namespace wg {
void geodetic_to_ecef(double* out, int n, const double* llh);

void unit_wm_to_geodetic(double* out, int n, const double* xyz);

void unit_wm_to_ecef(double* out, int n, const double* xyz);

void ecef_to_geodetic(double* out, int n, const double* x);




void geodetic_to_ecef(float* out, int n, const float* llh, int stride=3);

void unit_wm_to_geodetic(float* out, int n, const float* xyz, int stride=3);

void unit_wm_to_ecef(float* out, int n, const float* xyz, int stride=3);

void ecef_to_geodetic(float* out, int n, const float* x);

void wgs84_normal(double *out, const double* xyz);

void ltp(double *out, const double* xyz);
}
