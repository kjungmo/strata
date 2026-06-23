#pragma once
#include <vector>
#include "strata_core/map_backend.hpp"
#include "strata_core/layered_map.hpp"
namespace strata_core {
class Voxel3DBackend : public MapBackend {
 public:
  Voxel3DBackend(double voxel_size, LayeredMapParams params);
  void integrate(const Observation& obs, const Eigen::Vector3d& sensor_origin_map) override;
  bool tick() override;
  std::size_t staticCellCount() const override { return layered_.staticCells().size(); }
  std::size_t transientCellCount() const override { return layered_.transientCells().size(); }
  std::vector<Eigen::Vector3d> staticPoints() const;
  CellId voxelId(const Eigen::Vector3d& p) const;
  Eigen::Vector3d voxelCenter(CellId id) const;
  const LayeredMap& layered() const { return layered_; }
 private:
  double voxel_size_;
  LayeredMap layered_;
};
}  // namespace strata_core
