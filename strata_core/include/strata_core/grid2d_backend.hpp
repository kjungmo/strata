#pragma once
#include "strata_core/map_backend.hpp"
#include "strata_core/layered_map.hpp"
#include "strata_core/types.hpp"
namespace strata_core {
class Grid2DBackend : public MapBackend {
 public:
  Grid2DBackend(const GridMeta& meta, LayeredMapParams params);
  void integrate(const Observation& obs, const Eigen::Vector3d& sensor_origin_map) override;
  bool tick() override;
  std::size_t staticCellCount() const override { return layered_.staticCells().size(); }
  std::size_t transientCellCount() const override { return layered_.transientCells().size(); }
  GridMap toOccupancyGrid() const;
  const LayeredMap& layered() const { return layered_; }
  const GridMeta& meta() const { return meta_; }
 private:
  void raycastClear(int gx0, int gy0, int gx1, int gy1);
  GridMeta meta_;
  LayeredMap layered_;
};
}  // namespace strata_core
