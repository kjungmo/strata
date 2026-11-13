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
  void closeWindows(int k) override;
  std::size_t staticCellCount() const override { return layered_.staticCells().size(); }
  std::size_t transientCellCount() const override { return layered_.transientCells().size(); }
  std::size_t intermittentCellCount() const override { return layered_.intermittentCells().size(); }
  std::size_t cellCount() const override { return layered_.cellCount(); }
  std::size_t lastWindowTouched() const override { return layered_.lastWindowTouched(); }
  std::vector<Eigen::Vector3d> staticPoints() const;
  // Centres of the undecided voxels with statistic >= min_log_odds (LayeredMap::undecidedCells(min_log_odds)).
  // Not static: they carry no error bound (SPEC 3.6). Read-only.
  std::vector<Eigen::Vector3d> provisionalPoints(double min_log_odds) const;
  CellId voxelId(const Eigen::Vector3d& p) const;
  Eigen::Vector3d voxelCenter(CellId id) const;
  const LayeredMap& layered() const { return layered_; }
  // Forwards to LayeredMap::setWindowObserver (read-only window observer).
  void setWindowObserver(WindowObserver* observer) { layered_.setWindowObserver(observer); }
 private:
  double voxel_size_;
  LayeredMap layered_;
};
}  // namespace strata_core
