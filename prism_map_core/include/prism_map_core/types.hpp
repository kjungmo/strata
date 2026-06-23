#pragma once
#include <cstdint>
#include <vector>
#include <Eigen/Core>
#include <Eigen/Geometry>
namespace prism_map_core {
struct Pose2D { double x{0.0}; double y{0.0}; double yaw{0.0}; };
using Pose3D = Eigen::Isometry3d;
struct GridMeta { int width{0}; int height{0}; double resolution{0.05}; double origin_x{0.0}; double origin_y{0.0}; };
struct GridMap { GridMeta meta; std::vector<std::int8_t> data; };
using CellId = std::int64_t;
bool worldToGrid(const GridMeta& m, double wx, double wy, int& gx, int& gy);
void gridToWorld(const GridMeta& m, int gx, int gy, double& wx, double& wy);
CellId gridCellId(const GridMeta& m, int gx, int gy);
double normalizeAngle(double a);
Pose2D flatten(const Pose3D& T);
}  // namespace prism_map_core
