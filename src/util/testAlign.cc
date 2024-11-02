#include "align3d.hpp"


int main() {

	double A = -1, B = 1;
	// double A = 0, B = 1;

#if 0
	Matrix<double,8,3> a; a <<
			A,A,A,
			B,A,A,
			B,B,A,
			A,B,A,
			A,A,B,
			B,A,B,
			B,B,B,
			A,B,B;

	A = 5000.5, B = 6000.1;
	// A = .5, B = 1;
	Quaterniond qb { AngleAxisd(5 * M_PI/180, Vector3d::UnitX()) };
	Matrix<double,8,3> b; b <<
			A,A,A,
			B,A,A,
			B,B,A,
			A,B,A,
			A,A,B,
			B,A,B,
			B,B,B,
			A,B,B;
	b = b * qb.toRotationMatrix().transpose();
#else

	Matrix<double,4,3> a; a <<
			A,A,A,
			B,A,A,
			A,B,A,
			A,A,B;

	// A = 5000.5, B = 6000.1;
	Quaterniond qb { AngleAxisd(5 * M_PI/180, Vector3d::UnitX()) };
	A = 6e6 + 1, B = 6e6 + 2;
	A += .1;
	Matrix<double,4,3> b; b <<
			A,A,A,
			B,A,A,
			A,B,A,
			A,A,B;
	// b = b * qb.toRotationMatrix().transpose();
#endif

	// auto res = align_gn(b);
	auto res = align_box_dlt(b);
	spdlog::info("b original:\n{}", b);
	spdlog::info("qb: {}", qb.coeffs().transpose());
	spdlog::info("T t: {}", res.t.transpose());
	spdlog::info("T s: {}", res.s.transpose());
	spdlog::info("T q: {}", res.q.coeffs().transpose());
	spdlog::info("a -> b final:\n{}", (res.transform() * a.transpose()).transpose());
	spdlog::info("rmse: {:>12.5f}", (b - (res.transform() * a.transpose()).transpose()).rowwise().squaredNorm().mean());

	return 0;
}
