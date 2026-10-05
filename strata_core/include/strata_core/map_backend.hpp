#pragma once
#include <cstddef>
#include <vector>
#include <Eigen/Core>
namespace strata_core {
struct Observation { std::vector<Eigen::Vector3d> hits; };  // endpoints in MAP frame
class MapBackend {
 public:
  virtual ~MapBackend() = default;
  virtual void integrate(const Observation& obs, const Eigen::Vector3d& sensor_origin_map) = 0;
  virtual bool tick() = 0;                // count one integration; close a window every layer_interval
  virtual void closeWindows(int k) = 0;   // close k windows now, whatever the count (time-based windows)
  virtual std::size_t staticCellCount() const = 0;
  virtual std::size_t transientCellCount() const = 0;
};
}  // namespace strata_core
