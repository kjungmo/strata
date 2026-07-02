#include <gtest/gtest.h>
#include "strata_core/voxel3d_backend.hpp"
using namespace strata_core;
static LayeredMapParams P(){LayeredMapParams p;p.layer_interval=1;p.l_hit=1.0;p.l_miss=-1.0;p.survival_decay=1.0;
  p.graduate_prob=0.85;p.demote_prob=0.4;p.min_observations=3;p.prune_prob=0.05;p.enable_periodicity=false;return p;}
TEST(Voxel3DBackend, SamePointSameVoxel){ Voxel3DBackend b(0.5,P());
  EXPECT_EQ(b.voxelId({1.1,2.2,3.3}), b.voxelId({1.2,2.1,3.4}));
  EXPECT_NE(b.voxelId({1.1,2.2,3.3}), b.voxelId({9.9,2.2,3.3})); }
TEST(Voxel3DBackend, RepeatedHitGraduatesVoxel){ Voxel3DBackend b(0.5,P());
  Observation o; o.hits.push_back({3.0,0.0,0.5});
  for(int i=0;i<3;++i){ b.integrate(o,{0.0,0.0,0.5}); b.tick(); }
  EXPECT_EQ(b.staticCellCount(),1u); ASSERT_EQ(b.staticPoints().size(),1u);
  EXPECT_NEAR(b.staticPoints()[0].z(),0.75,0.5); }                 // 6-DoF: z preserved
TEST(Voxel3DBackend, MovingPointDoesNotGraduate){ Voxel3DBackend b(0.5,P());
  for(int i=0;i<6;++i){ Observation o; o.hits.push_back({i*1.0,0.0,0.5}); b.integrate(o,{0.0,0.0,0.5}); b.tick(); }
  EXPECT_EQ(b.staticCellCount(),0u); }
