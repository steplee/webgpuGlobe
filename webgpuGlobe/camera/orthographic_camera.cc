#include "orthographic_camera.h"
#include <Eigen/Core>
#include <Eigen/Geometry>

#include "util/fmtEigen.h"

namespace wg {

using namespace Eigen;

using RowMatrix4f = Eigen::Matrix<float, 4, 4, RowMajor>;
using RowMatrix3f = Eigen::Matrix<float, 3, 3, RowMajor>;
using RowMatrix3d = Eigen::Matrix<double, 3, 3, RowMajor>;

OrthographicCamera::OrthographicCamera(const CameraIntrin &intrin,
                                       AppObjects &ao,
                                       const double startingLocation[3],
                                       const double startingRotation[9])

    : InteractiveCamera(intrin, ao) {
  for (int i = 0; i < 3; i++)
    p[i] = startingLocation[i];
  Map<const RowMatrix3d> R{startingRotation};
  Map<Quaterniond> q_{q};
  q_ = R;
}

SceneCameraData1 OrthographicCamera::lower(const SceneData &sd) {
  SceneCameraData1 out;

  Map<Matrix4f> mvp(out.mvp);
  Map<Matrix4f> imvp(out.imvp);
  Map<Matrix4f> mv(out.mv);
  Map<Vector3f> eye(out.eye);
  Map<Vector4f> colorMult(out.colorMult);
  Map<Vector2f> wh(out.wh);
  Map<Vector4f> sun(out.sun);
  float &haeAlt = out.haeAlt;
  float &haze = out.haze;
  float &time = out.time;
  float &dt = out.dt;

  Map<const Vector3d> eye_(this->p);
  Map<const Quaterniond> q_(this->q);

  Matrix4d mv_;
  mv_.row(3) << 0, 0, 0, 1;
  mv_.topLeftCorner<3, 3>() = q_.toRotationMatrix().transpose();
  mv_.topRightCorner<3, 1>() = -(q_.conjugate() * eye_);

  Matrix4f proj_;
  intrin.proj(proj_.data());

  // mvp.setIdentity();
  // mv.setIdentity();
  mv = mv_.cast<float>();
  mvp = proj_ * mv_.cast<float>();
  imvp = mvp.inverse();

  wh(0) = sd.wh[0];
  wh(1) = sd.wh[1];

  eye = eye_.cast<float>();

  haeAlt = 0;

  colorMult.setConstant(1);
  sun.setZero();

  haze = time = dt = 0;
  dt = lastSceneData.dt;
  time = lastSceneData.elapsedTime;

  return out;
}

void OrthographicCamera::step(const SceneData &sd) {}

} // namespace wg
