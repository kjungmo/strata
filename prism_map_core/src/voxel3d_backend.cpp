#include "prism_map_core/voxel3d_backend.hpp"
#include <algorithm>
#include <cmath>
namespace prism_map_core {
namespace { constexpr std::int64_t kOff=1<<20; constexpr std::int64_t kBits=21;
inline std::int64_t vc(double w,double s){ return (std::int64_t)std::floor(w/s); } }
Voxel3DBackend::Voxel3DBackend(double voxel_size, LayeredMapParams params)
  : voxel_size_(voxel_size), layered_(params) {}
CellId Voxel3DBackend::voxelId(const Eigen::Vector3d& p) const {
  const std::int64_t vx=vc(p.x(),voxel_size_)+kOff, vy=vc(p.y(),voxel_size_)+kOff, vz=vc(p.z(),voxel_size_)+kOff;
  return (vx<<(2*kBits))|(vy<<kBits)|vz;
}
Eigen::Vector3d Voxel3DBackend::voxelCenter(CellId id) const {
  const std::int64_t mask=(1LL<<kBits)-1;
  const std::int64_t vz=(id&mask)-kOff, vy=((id>>kBits)&mask)-kOff, vx=((id>>(2*kBits))&mask)-kOff;
  return {(vx+0.5)*voxel_size_,(vy+0.5)*voxel_size_,(vz+0.5)*voxel_size_};
}
void Voxel3DBackend::integrate(const Observation& obs, const Eigen::Vector3d& sensor_origin_map){
  for(const auto& h: obs.hits){
    const Eigen::Vector3d d=h-sensor_origin_map; const double len=d.norm();
    if(len>1e-6){ const int steps=(int)(len/(voxel_size_*0.5)); const Eigen::Vector3d st=d/std::max(steps,1);
      for(int i=1;i<steps;++i) layered_.observeMiss(voxelId(sensor_origin_map+st*i)); }
    layered_.observeHit(voxelId(h));
  }
}
bool Voxel3DBackend::tick(){ return layered_.tick(); }
std::vector<Eigen::Vector3d> Voxel3DBackend::staticPoints() const {
  std::vector<Eigen::Vector3d> pts; for(CellId id: layered_.staticCells()) pts.push_back(voxelCenter(id)); return pts;
}
}  // namespace prism_map_core
