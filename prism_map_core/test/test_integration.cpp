#include <gtest/gtest.h>
#include "prism_map_core/grid2d_backend.hpp"
using namespace prism_map_core;
TEST(Integration, WallStaticMoverTransientDoorPeriodic) {
  GridMeta m; m.width=40; m.height=40; m.resolution=1.0; m.origin_x=0; m.origin_y=0;
  LayeredMapParams p; p.layer_interval=1; p.l_hit=1.0; p.l_miss=-1.0; p.survival_decay=1.0;
  p.graduate_prob=0.9; p.demote_prob=0.4; p.min_observations=5; p.prune_prob=0.05;
  p.enable_periodicity=true; p.periodic_amplitude_min=0.3;
  p.periodicity.period_windows=8; p.periodicity.n_harmonics=3;
  Grid2DBackend b(m, p);
  const Eigen::Vector3d sensor(0.5,0.5,0.0);
  const Eigen::Vector3d wall(25.5,0.5,0.0);                 // fixed wall cell (25,0)
  const Eigen::Vector3d door(10.5,0.5,0.0);                 // door cell (10,0): periodic
  for (int w=0; w<24; ++w) {
    Observation obs; obs.hits.push_back(wall);               // wall every window
    if ((w % 8) < 4) obs.hits.push_back(door);               // door occupied first half of period
    obs.hits.push_back(Eigen::Vector3d(5.5, 5.0 + w, 0.0));  // mover: new cell each window
    b.integrate(obs, sensor);
    b.tick();
  }
  EXPECT_TRUE(b.layered().isStatic(gridCellId(m,25,0)));                      // wall graduated
  EXPECT_EQ(b.layered().classify(gridCellId(m,10,0)), CellClass::Periodic);  // door is periodic, not static
  EXPECT_FALSE(b.layered().isStatic(gridCellId(m,10,0)));
  for (int w=0; w<24; ++w) { int gx,gy; ASSERT_TRUE(worldToGrid(m,5.5,5.0+w,gx,gy));
    EXPECT_FALSE(b.layered().isStatic(gridCellId(m,gx,gy))); }               // mover never static
}
