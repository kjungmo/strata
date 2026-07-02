#include <gtest/gtest.h>
#include "strata_core/types.hpp"
using namespace strata_core;

static GridMeta meta(int w, int h, double r) {
  GridMeta m; m.width = w; m.height = h; m.resolution = r; m.origin_x = 0; m.origin_y = 0; return m;
}

TEST(GridMath, WorldGridRoundTrip) {
  GridMeta m = meta(10, 10, 0.5);
  int gx, gy;
  ASSERT_TRUE(worldToGrid(m, 1.25, 2.75, gx, gy));
  EXPECT_EQ(gx, 2);
  EXPECT_EQ(gy, 5);
  double wx, wy;
  gridToWorld(m, 2, 5, wx, wy);
  EXPECT_NEAR(wx, 1.25, 1e-9);
  EXPECT_NEAR(wy, 2.75, 1e-9);
}

TEST(GridMath, OutOfBoundsRejected) {
  GridMeta m = meta(4, 4, 1.0);
  int gx, gy;
  EXPECT_FALSE(worldToGrid(m, -0.1, 0.0, gx, gy));
  EXPECT_FALSE(worldToGrid(m, 4.0, 0.0, gx, gy));
}

TEST(GridMath, CellIdRowMajor) {
  GridMeta m = meta(10, 10, 1.0);
  EXPECT_EQ(gridCellId(m, 3, 4), 43);
  EXPECT_NE(gridCellId(m, 3, 4), gridCellId(m, 4, 3));
}
