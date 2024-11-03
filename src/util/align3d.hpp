#pragma once

#include <Eigen/Core>
#include <Eigen/Geometry>

#include "util/fmtEigen.h"

namespace {

	using namespace Eigen;

	inline Matrix3d crossMatrix(const Vector3d& t) {
		Matrix3d o; o <<
			0, -t(2), t(1),
		    t(2), 0, -t(0),
			-t(1), t(0), 0;
		return o;
	}

	struct SolvedTransform {
		Vector3d t;
		Array3d s;
		Quaterniond q;

		Affine3d transform() const {
			Affine3d out;
			out.translation() = t;
			out.linear() = q.toRotationMatrix() * s.matrix().asDiagonal();
			return out;
		}
	};

	//
	// Find the transformation that takes the points on a unit box ([-1,-1,-1] ... [1,1,1])
	// into the `b` points.
	//
	// This is a lot like the "Absolute Orientation Problem" (see Horn's SVD or quat soln),
	// and the "Perspective N point problem" (see Lu Orthogonal Iteration algo),
	// and "Wahba's problem".
	//
	// But here I use a simple euclidean error cost w/ Gauss-Newton minimization.
	// I use my notes / copied formulae from the Micro Lie Theory paper, with the simple
	// loss function:
	//
	//		e = b - R{q (+) dq} * s
	//
	//	e: error
	//	b: target points (centered)
	//
	//	WARNING: This works well only depending on initial scale vs orientation alignment.
	//	         Instead, use the algebraic impl below.
	//
#if 0
	SolvedTransform align_gn(
			const Matrix<double,8,3>& b) {

		Vector3d bmu = b.colwise().mean();
		Vector3d t = bmu;

		// The centered point sets.
		Matrix<double,8,3> bc = b.rowwise() - bmu.transpose();

		double A = -1, B = 1;
		// double A = 0, B = 1;
		Matrix<double,8,3> ac; ac <<
			A,A,A,
			B,A,A,
			B,B,A,
			A,B,A,
			A,A,B,
			B,A,B,
			B,B,B,
			A,B,B;

		Vector3d s { Vector3d::Constant(bc.rowwise().norm().mean() * .7) };
		Quaterniond q { Quaterniond::Identity() };

		using Vec6  = Matrix<double,6,1>;
		using Mat6  = Matrix<double,6,6>;
		using Mat36 = Matrix<double,3,6>;

		int iters = 20;
		for (int i=0; i<iters; i++) {
			double mse = 0;
			Vec6 Jtres { Vec6::Zero() };
			// Mat6 Hess  { Mat6::Identity() * 10 };
			// Mat6 Hess  { Mat6::Identity() * std::pow(.5, i) };
			
			Mat6 Hess  { Mat6::Zero() };

			if (i < iters/2)
				Hess.topLeftCorner<3,3>() += Matrix3d::Identity() * std::pow(150,2);
			else
				Hess.topLeftCorner<3,3>() += Matrix3d::Identity() * std::pow(90,2);

			if (i < iters/2)
				Hess.bottomRightCorner<3,3>() += Matrix3d::Identity() * std::pow(10,2);
			else
				Hess.bottomRightCorner<3,3>() += Matrix3d::Identity() * std::pow(1,2);

			Matrix3d Ri = q.toRotationMatrix();
			Matrix3d Si = s.asDiagonal();
			Matrix3d Ti = Ri * Si;
			Matrix<double,8,3> tac = ac * Ti.transpose();

			// spdlog::info("Ri:\n{}",Ri);
			// spdlog::info("Ti:\n{}",Ti);
			// spdlog::info("tac:\n{}",tac);

			for (int j=0; j<8; j++) {
				Vector3d residual { bc.row(j) - tac.row(j) };

				// Matrix3d J_dq = Ri * crossMatrix(residual);
				Matrix3d J_dq = -Ri * crossMatrix(ac.row(j));
				Matrix3d J_s  = Ri * ac.row(j).transpose().asDiagonal(); // important: flip as necessary
				// Matrix3d J_s  = Ri * ac.row(j).asDiagonal(); // important: flip as necessary
				// Matrix3d J_s  = Ti * ac.row(j).asDiagonal(); // important: flip as necessary, multiplicative
				// Matrix3d J_s  = Ri * crossMatrix(ac.row(j)) * s.asDiagonal();
				// Matrix3d J_s  = Ri;
				Mat36 J;
				J.leftCols<3>() = J_dq;
				J.rightCols<3>() = J_s;

				Jtres += J.transpose() * residual;
				Hess += J.transpose() * J;
				mse += residual.squaredNorm();
				spdlog::info("j {} :: res {} bc {} tac {}", j, residual.transpose(), bc.row(j), tac.row(j));
				// spdlog::info("J_s:\n{}", J_s);
				// spdlog::info("J_dq:\n{}", J_dq);
			}


			Vec6 d = Hess.ldlt().solve(Jtres);
			// Vec6 d = Hess.inverse() * (Jtres);

			double rmse = std::sqrt(mse / 8);
			spdlog::info("iter {}, pre-RMSE: {:>8.5f}", i, rmse);
			spdlog::info("q {} angle {}", q.coeffs().transpose(), AngleAxisd{q}.angle() * 180/M_PI);
			spdlog::info("s {}", s.transpose());
			spdlog::info("Hess\n{}", Hess);
			spdlog::info("Jtres {}", Jtres.transpose());
			spdlog::info("dq {} angle {}", d.head<3>().transpose(), d.head<3>().norm() * 180/M_PI);
			spdlog::info("ds {}", d.tail<3>().transpose());
			fmt::print("\n");

			q = q * AngleAxisd(.5*d.head<3>().norm(), d.head<3>().normalized());
			s = s + d.tail<3>();
			s = s.cwiseAbs();
		}


		return SolvedTransform { t,s,q };
	}
#else

	// WARNING: I may need to standardize the input...

	SolvedTransform align_box_dlt(
			const Matrix<double,4,3>& b) {

		// NOTE: This solves the problem with 12 DoF, but a 3d affine transform actually has only 9 DoF (3 trans, 3 ori, 3 scale).
		//       The algebraic solve requires having the 3 extra skew DoF, which we don't want.
		//       But the skew should be zero for well chosen inputs.
		//       Note that turning the 3x4 matrix back into the desired trans + quat + diagonal-scale actually requires
		//       an SVD, which is handled in Eigen's helper function `computeScalingRotation`

		Vector3d bmu = b.colwise().mean();
		Vector3d t = bmu;

		// The centered point sets.
		Matrix<double,4,3> bc = b.rowwise() - bmu.transpose();

		double A = -1, B = 1;
		// double A = 0, B = 1;
		Matrix<double,4,3> ac; ac <<
			A,A,A,
			B,A,A,
			A,B,A,
			A,A,B;


		Matrix<double,12,12> AA;
		Matrix<double,12,1> bb;
		AA.setZero();
		for (int i=0; i<4; i++) {
			Vector3d si = ac.row(i);
			for (int j=0; j<3; j++) {
				AA(i*3+j, j*4+0) = si(0);
				AA(i*3+j, j*4+1) = si(1);
				AA(i*3+j, j*4+2) = si(2);
				AA(i*3+j, j*4+3) = 1;
			}

			bb(i*3+0) = bc(i,0);
			bb(i*3+1) = bc(i,1);
			bb(i*3+2) = bc(i,2);
		}


		Matrix<double,12,1> soln = AA.fullPivLu().solve(bb);

		Matrix<double,3,4> TT;
		for (int i=0; i<12; i++) TT(i/4,i%4) = soln(i);

		// spdlog::info("T:\n{}", TT);

		SolvedTransform out;
		Affine3d T(TT);
		Matrix3d Rot;
		Matrix3d Scale;
		T.computeScalingRotation(&Scale, &Rot);

		if (0) {
		spdlog::info("target: {}", b.transpose());
		spdlog::info("A:\n{}", AA);
		spdlog::info("b: {}", bb.transpose());
		spdlog::info("soln err: {}", (AA * soln - bb).norm());
		spdlog::info("S:\n{}", Scale);
		spdlog::info("R:\n{}", Rot);
		throw std::runtime_error("stop");
		}

		out.t = T.translation() + t;
		out.q = Quaterniond { Rot };
		out.s = Scale.diagonal();




		return out;
	}
#endif

}
