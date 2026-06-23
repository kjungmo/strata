#include <gtest/gtest.h>
#include "strata_core/grid2d_backend.hpp"
using namespace strata_core;
static GridMeta meta(){GridMeta m;m.width=20;m.height=20;m.resolution=1.0;m.origin_x=0;m.origin_y=0;return m;}
static LayeredMapParams P(){LayeredMapParams p;p.layer_interval=1;p.l_hit=1.0;p.l_miss=-1.0;p.survival_decay=1.0;
  p.graduate_prob=0.85;p.demote_prob=0.4;p.min_observations=3;p.prune_prob=0.05;p.enable_periodicity=false;return p;}

TEST(Grid2DBackend, HitMarksEndpointAndClearsRay) {
  Grid2DBackend b(meta(), P());
  Observation obs; obs.hits.push_back(Eigen::Vector3d(5.5,0.5,0.0));
  b.integrate(obs, Eigen::Vector3d(0.5,0.5,0.0));
  const GridMeta m=meta();
  EXPECT_NE(b.layered().classify(gridCellId(m,5,0)), CellClass::Unknown);   // endpoint seen
  EXPECT_FALSE(b.layered().isStatic(gridCellId(m,2,0)));                    // cleared cell not static
}
TEST(Grid2DBackend, RepeatedHitGraduates) {
  Grid2DBackend b(meta(), P());
  Observation obs; obs.hits.push_back(Eigen::Vector3d(5.5,0.5,0.0));
  for(int i=0;i<3;++i){ b.integrate(obs, Eigen::Vector3d(0.5,0.5,0.0)); b.tick(); }
  EXPECT_TRUE(b.layered().isStatic(gridCellId(meta(),5,0)));
  EXPECT_EQ(b.staticCellCount(), 1u);
}
TEST(Grid2DBackend, RenderOccupancyGrid) {
  Grid2DBackend b(meta(), P());
  Observation obs; obs.hits.push_back(Eigen::Vector3d(5.5,0.5,0.0));
  for(int i=0;i<3;++i){ b.integrate(obs, Eigen::Vector3d(0.5,0.5,0.0)); b.tick(); }
  GridMap g=b.toOccupancyGrid();
  ASSERT_EQ((int)g.data.size(), g.meta.width*g.meta.height);
  EXPECT_EQ(g.data[gridCellId(g.meta,5,0)], 100);
}
TEST(Grid2DBackend, SixDofEndpointProjectsToPlane) {  // z is ignored by the grid
  Grid2DBackend b(meta(), P());
  Observation obs; obs.hits.push_back(Eigen::Vector3d(5.5,0.5,3.7));  // elevated hit
  b.integrate(obs, Eigen::Vector3d(0.5,0.5,1.2));
  EXPECT_NE(b.layered().classify(gridCellId(meta(),5,0)), CellClass::Unknown);
}
