#include <gtest/gtest.h>
#include "strata_core/version.hpp"
TEST(Smoke, VersionDefined) { EXPECT_STREQ(STRATA_CORE_VERSION, "0.1.0"); }
