#include "strata_core/types.hpp"
#include <cmath>
namespace strata_core {
bool worldToGrid(const GridMeta& m, double wx, double wy, int& gx, int& gy){
  const double fx=(wx-m.origin_x)/m.resolution, fy=(wy-m.origin_y)/m.resolution;
  if(fx<0.0||fy<0.0) return false; gx=static_cast<int>(fx); gy=static_cast<int>(fy);
  return gx<m.width && gy<m.height;
}
void gridToWorld(const GridMeta& m, int gx, int gy, double& wx, double& wy){
  wx=m.origin_x+(gx+0.5)*m.resolution; wy=m.origin_y+(gy+0.5)*m.resolution;
}
CellId gridCellId(const GridMeta& m, int gx, int gy){ return static_cast<CellId>(gy)*m.width+gx; }
double normalizeAngle(double a){ while(a>M_PI)a-=2*M_PI; while(a<-M_PI)a+=2*M_PI; return a; }
Pose2D flatten(const Pose3D& T){
  Pose2D p; p.x=T.translation().x(); p.y=T.translation().y();
  const Eigen::Vector3d e = T.rotation().eulerAngles(0,1,2);  // roll,pitch,yaw
  p.yaw = normalizeAngle(e.z()); return p;
}
}  // namespace strata_core
