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
TEST(Grid2DBackend, RenderFreeTransientUnknown) {
  Grid2DBackend b(meta(), P());
  const GridMeta m=meta();
  Observation obs; obs.hits.push_back(Eigen::Vector3d(5.5,0.5,0.0));
  b.integrate(obs, Eigen::Vector3d(0.5,0.5,0.0)); b.tick();         // one window: endpoint hit once
  GridMap g=b.toOccupancyGrid();
  EXPECT_EQ(g.data[gridCellId(m,3,0)], 0);     // on the ray: observed free
  EXPECT_EQ(g.data[gridCellId(m,5,0)], 50);    // hit once, not graduated: transient obstacle
  EXPECT_EQ(g.data[gridCellId(m,5,5)], -1);    // never observed
  EXPECT_TRUE(b.observedFree(3,0));
  EXPECT_FALSE(b.observedFree(5,0));
  EXPECT_FALSE(b.observedFree(-1,0));
}
TEST(Grid2DBackend, FreeSurvivesPruning) {
  Grid2DBackend b(meta(), P());
  const GridMeta m=meta();
  Observation obs; obs.hits.push_back(Eigen::Vector3d(5.5,0.5,0.0));
  for(int i=0;i<6;++i){ b.integrate(obs, Eigen::Vector3d(0.5,0.5,0.0)); b.tick(); }
  // l_miss = -1 per window with no decay: a cleared cell falls below prune_prob every
  // third window and is erased (window 3, recreated by the next miss, erased again at
  // window 6), yet the rendered map keeps it free.
  EXPECT_EQ(b.layered().classify(gridCellId(m,3,0)), CellClass::Unknown);
  EXPECT_EQ(b.toOccupancyGrid().data[gridCellId(m,3,0)], 0);
}
TEST(Grid2DBackend, FreedTransientRendersFree) {
  Grid2DBackend b(meta(), P());
  const GridMeta m=meta();
  const Eigen::Vector3d o(0.5,0.5,0.0);
  Observation near; near.hits.push_back(Eigen::Vector3d(3.5,0.5,0.0));   // object at (3,0)
  Observation far;  far.hits.push_back(Eigen::Vector3d(8.5,0.5,0.0));    // object gone: ray passes (3,0)
  b.integrate(far, o); b.tick();                                          // l = -1
  b.integrate(near, o); b.tick();                                         // l = 0
  EXPECT_EQ(b.toOccupancyGrid().data[gridCellId(m,3,0)], 50);   // hit in the last closed window
  b.integrate(far, o); b.tick();                                          // l = -1
  EXPECT_EQ(b.layered().classify(gridCellId(m,3,0)), CellClass::Transient);
  EXPECT_EQ(b.toOccupancyGrid().data[gridCellId(m,3,0)], 0);    // not hit since, leans free
}
TEST(Grid2DBackend, NewObstacleOnClearedGroundShowsAtOnce) {
  Grid2DBackend b(meta(), P());
  const GridMeta m=meta();
  const Eigen::Vector3d o(0.5,0.5,0.0);
  Observation near; near.hits.push_back(Eigen::Vector3d(3.5,0.5,0.0));
  Observation far;  far.hits.push_back(Eigen::Vector3d(8.5,0.5,0.0));
  b.integrate(far, o); b.tick();
  b.integrate(far, o); b.tick();                                          // l = -2, p = 0.12
  EXPECT_EQ(b.toOccupancyGrid().data[gridCellId(m,3,0)], 0);
  b.integrate(near, o);                                                   // hit, window still open
  EXPECT_EQ(b.toOccupancyGrid().data[gridCellId(m,3,0)], 50);
  b.tick();                                                               // l = -1: still leans free
  EXPECT_LT(b.layered().occupancyProb(gridCellId(m,3,0)), 0.5);
  EXPECT_EQ(b.toOccupancyGrid().data[gridCellId(m,3,0)], 50);   // but was hit in the last window
  b.integrate(far, o); b.tick();                                          // object gone again
  EXPECT_EQ(b.toOccupancyGrid().data[gridCellId(m,3,0)], 0);
}
TEST(Grid2DBackend, ObstacleLastSeenHitStaysOutOfView) {
  Grid2DBackend b(meta(), P());
  const GridMeta m=meta();
  const Eigen::Vector3d o(0.5,0.5,0.0);
  Observation near; near.hits.push_back(Eigen::Vector3d(3.5,0.5,0.0));
  Observation far;  far.hits.push_back(Eigen::Vector3d(8.5,0.5,0.0));
  Observation none;                                                      // sensor looks elsewhere
  b.integrate(far, o); b.tick();
  b.integrate(far, o); b.tick();                                          // l = -2
  b.integrate(near, o); b.tick();                                         // seen once: l = -1
  for(int i=0;i<4;++i){ b.integrate(none, o); b.tick(); }                 // windows close unobserved
  EXPECT_LT(b.layered().occupancyProb(gridCellId(m,3,0)), 0.5);
  EXPECT_EQ(b.toOccupancyGrid().data[gridCellId(m,3,0)], 50);   // last observation was a hit
  b.integrate(far, o); b.tick();                                          // observed free again
  EXPECT_EQ(b.toOccupancyGrid().data[gridCellId(m,3,0)], 0);
}
TEST(Grid2DBackend, CloseWindowsClosesRegardlessOfTickCount) {
  LayeredMapParams p = P(); p.layer_interval = 1000;       // tick() alone would never close
  Grid2DBackend b(meta(), p);
  const GridMeta m=meta();
  Observation obs; obs.hits.push_back(Eigen::Vector3d(5.5,0.5,0.0));
  for(int i=0;i<3;++i){ b.integrate(obs, Eigen::Vector3d(0.5,0.5,0.0)); EXPECT_FALSE(b.tick()); b.closeWindows(1); }
  EXPECT_EQ(b.layered().windowCount(), 3);
  EXPECT_TRUE(b.layered().isStatic(gridCellId(m,5,0)));
  b.closeWindows(5);                                      // a silent gap: five windows, nothing seen
  EXPECT_EQ(b.layered().windowCount(), 8);
  EXPECT_TRUE(b.layered().isStatic(gridCellId(m,5,0)));   // unobserved evidence is untouched
  EXPECT_EQ(b.toOccupancyGrid().data[gridCellId(m,3,0)], 0);
}
