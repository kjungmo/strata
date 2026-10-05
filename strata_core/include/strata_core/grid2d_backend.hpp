#pragma once
#include <cstdint>
#include <vector>
#include "strata_core/map_backend.hpp"
#include "strata_core/layered_map.hpp"
#include "strata_core/types.hpp"
namespace strata_core {
class Grid2DBackend : public MapBackend {
 public:
  Grid2DBackend(const GridMeta& meta, LayeredMapParams params);
  void integrate(const Observation& obs, const Eigen::Vector3d& sensor_origin_map) override;
  bool tick() override;
  void closeWindows(int k) override;
  std::size_t staticCellCount() const override { return layered_.staticCells().size(); }
  std::size_t transientCellCount() const override { return layered_.transientCells().size(); }
  // Renders static 100, periodic 75, transient obstacle 50 (hit in the open window,
  // last observed as a hit, or live evidence leaning occupied, p > 0.5), free 0 (a ray
  // has cleared the cell and its last observation was free), never observed -1. The
  // bits live outside the classifier, so a pruned cell keeps them: free means "last
  // observed free", not "currently free", and a cell last seen hit stays 50 out of view.
  GridMap toOccupancyGrid() const;
  bool observedFree(int gx, int gy) const;
  const LayeredMap& layered() const { return layered_; }
  const GridMeta& meta() const { return meta_; }
 private:
  void raycastClear(int gx0, int gy0, int gx1, int gy1);
  void onWindowClosed();
  GridMeta meta_;
  LayeredMap layered_;
  // One byte per grid cell: kFree once a ray cleared the cell; kHitOpen / kMissOpen if
  // hit / cleared in the open window; kHitLast if the last closed window that observed
  // the cell saw a hit (a window that both hits and clears a cell counts as a hit).
  static constexpr std::uint8_t kFree = 1, kHitOpen = 2, kHitLast = 4, kMissOpen = 8;
  std::vector<std::uint8_t> flags_;
  std::vector<CellId> touched_open_;  // ids carrying kHitOpen or kMissOpen
};
}  // namespace strata_core
