#pragma once

#include <cmath>
#include <Eigen/Core>
#include <Eigen/Dense>

namespace {
	using namespace Eigen;

const double one_div_pi = 1 / M_PI;

namespace EarthGeodetic {
	const double R1         = (6378137.0);
	const double R2         = (6356752.314245179);
	const double R1_inv     = (1. / 6378137.0);
	const double WGS84_F    = (1. / 298.257223563);
	const double WGS84_D    = (R2 / R1);
	const double a          = 1;
	const double b          = R2 / R1;
	const double a2         = a * a;
	const double b2         = b * b;
	const double e2         = 1 - (b * b / a * a);
	const double ae2        = a * e2;
	const double b2_over_a2 = b2 / a2;
}

namespace EarthAuthalic {
    const double R = 6371010.0;
    const double R_inv = 1. / R;
	const double a          = 1;
	const double b          = a;
}

inline Vector3d authalic_to_ecef_(const Vector3d& a) {
	// Convert authalic to lat/lng/alt
	/*
    z,y,x = z/Earth.R,y/Earth.R,x/Earth.R

    lng = np.arctan2(y,x)
    p = np.sqrt(x*x+y*y)
    lat = np.arctan2(z,p)

    # rn = Earth.na / np.sqrt(1-Earth.ne2*(np.sin(lat)) ** 2)
    # sinabslat, coslat = np.sin(abs(lat)), np.cos(lat)
    # alt = (abs(z) + p - rn * (coslat + (1-Earth.ne2) * sinabslat)) / (coslat + sinabslat)
    alt = np.linalg.norm((x,y,z)) - 1.0
    # print(lng,lat,np.linalg.norm((x,y,z)))

    return np.array((np.rad2deg(lng), np.rad2deg(lat), alt*Earth.R))
	*/
	double x = a(0) / EarthAuthalic::R;
	double y = a(1) / EarthAuthalic::R;
	double z = a(2) / EarthAuthalic::R;
	double lng = std::atan2(y,x);
	double p = std::sqrt(x*x+y*y);
	double lat = std::atan2(z,p);
	double alt = std::sqrt(x*x+y*y+z*z) - 1.0;

	// Map geodetic to ecef
	double       cos_phi = std::cos(lat), cos_lamb = std::cos(lng);
	double       sin_phi = std::sin(lat), sin_lamb = std::sin(lng);
	double       n_phi = EarthGeodetic::a / std::sqrt(1 - EarthGeodetic::e2 * sin_phi * sin_phi);

	Vector3d out = EarthGeodetic::R1 * Vector3d{
			(n_phi + alt) * cos_phi * cos_lamb,
			(n_phi + alt) * cos_phi * sin_lamb,
			(EarthGeodetic::b2_over_a2 * n_phi + alt) * sin_phi
	};

	// fmt::print(" - auth->geo :: {} => {} {} {} => {}\n", a.transpose(), lng,lat,alt, out.transpose());

	return out;
}

inline Matrix4d convert_authalic_to_geodetic(const Matrix4d& T0) {
	Vector3d a = (T0 * Vector4d{0,0,0,1}).hnormalized();
	Vector3d b = (T0 * Vector4d{0,0,255,1}).hnormalized();
	Vector3d c = (T0 * Vector4d{255,0,0,1}).hnormalized();
	Vector3d d = (T0 * Vector4d{0,255,0,1}).hnormalized();
	Vector3d a2 = authalic_to_ecef_(a);
	Vector3d b2 = authalic_to_ecef_(b);
	Vector3d c2 = authalic_to_ecef_(c);
	Vector3d d2 = authalic_to_ecef_(d);

	// a2 = a + Vector3d { 0, 0, 1000 };
	// b2 = b + Vector3d { 0, 0, 1000 };
	// c2 = c + Vector3d { 0, 0, 1000 };
	// d2 = d + Vector3d { 0, 0, 1000 };

	Matrix<double,12,12> A; A <<
		a(0), a(1), a(2), 0,    0,    0,    0,    0,    0,    1, 0, 0,
		0,    0,    0,    a(0), a(1), a(2), 0,    0,    0,    0, 1, 0,
		0,    0,    0,    0,    0,    0,    a(0), a(1), a(2), 0, 0, 1,
		b(0), b(1), b(2), 0,    0,    0,    0,    0,    0,    1, 0, 0,
		0,    0,    0,    b(0), b(1), b(2), 0,    0,    0,    0, 1, 0,
		0,    0,    0,    0,    0,    0,    b(0), b(1), b(2), 0, 0, 1,
		c(0), c(1), c(2), 0,    0,    0,    0,    0,    0,    1, 0, 0,
		0,    0,    0,    c(0), c(1), c(2), 0,    0,    0,    0, 1, 0,
		0,    0,    0,    0,    0,    0,    c(0), c(1), c(2), 0, 0, 1,
		d(0), d(1), d(2), 0,    0,    0,    0,    0,    0,    1, 0, 0,
		0,    0,    0,    d(0), d(1), d(2), 0,    0,    0,    0, 1, 0,
		0,    0,    0,    0,    0,    0,    d(0), d(1), d(2), 0, 0, 1;
	Matrix<double,12,1> bb;
	bb.segment<3>(0) = a2;
	bb.segment<3>(3) = b2;
	bb.segment<3>(6) = c2;
	bb.segment<3>(9) = d2;

	// fmt::print(" - Design Matrix:\n{} b: {}\n", A, bb.transpose());

	// Matrix<double,12,1> t1 = A.ldlt().solve(bb);
	Matrix<double,12,1> t1 = A.fullPivLu().solve(bb);
	// Matrix4d T1; T1 << t1(0), t1(1), t1(2), t1(3), t1(4), t1(5), t1(6), t1(7), t1(8), t1(9), t1(10), t1(11), 0,0,0,1;
	Matrix4d T1;
	for (int y=0; y<3; y++) for (int x=0; x<3; x++)
		T1(y,x) = t1(y*3+x);
	T1(0,3) = t1(9);
	T1(1,3) = t1(10);
	T1(2,3) = t1(11);
	T1.row(3) << 0,0,0,1;

	// fmt::print(" - Ax-b: {}\n", (A * t1 - bb).transpose());
	// fmt::print(" - T1\n{}\n", T1);

	return T1 * T0;

}
}

