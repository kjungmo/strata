#include <gtest/gtest.h>
#include "prism_map_core/version.hpp"
TEST(Smoke, VersionDefined) { EXPECT_STREQ(PRISM_MAP_CORE_VERSION, "0.1.0"); }
