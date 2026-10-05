#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <unistd.h>
#include <gtest/gtest.h>
#include "strata/map_writer.hpp"
using strata::writeMapPair;

namespace {
std::string tempDir() {
  char tmpl[] = "/tmp/strata_map_writer_XXXXXX";
  const char* d = mkdtemp(tmpl);
  return d ? d : "";
}
std::string slurp(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream s; s << f.rdbuf(); return s.str();
}
// "key: value" lines of the saved YAML (flat, as writeMapPair writes it).
std::map<std::string, std::string> yamlKeys(const std::string& path) {
  std::map<std::string, std::string> out;
  std::istringstream in(slurp(path));
  for (std::string line; std::getline(in, line);) {
    const auto c = line.find(": ");
    if (c != std::string::npos) out[line.substr(0, c)] = line.substr(c + 2);
  }
  return out;
}
strata_core::GridMap grid(int w, int h, double res, double ox, double oy) {
  strata_core::GridMap g;
  g.meta.width = w; g.meta.height = h; g.meta.resolution = res; g.meta.origin_x = ox; g.meta.origin_y = oy;
  g.data.assign(static_cast<std::size_t>(w) * h, -1);
  return g;
}
}  // namespace

TEST(MapWriter, WritesAPairWithRowsTopDownAndTheImageNextToTheYaml) {
  const std::string dir = tempDir();
  ASSERT_FALSE(dir.empty());
  auto g = grid(3, 2, 0.05, -10.0, -10.0);
  g.data = {100, 0, 50,     // row 0 (bottom in the world)
            75, -1, 0};     // row 1 (top)
  const auto r = writeMapPair(g, dir + "/m");
  ASSERT_TRUE(r.ok) << r.message;
  EXPECT_EQ(r.message, "saved " + dir + "/m.pgm + " + dir + "/m.yaml");
  const std::string pgm = slurp(dir + "/m.pgm");
  const std::string header = "P5\n3 2\n255\n";
  ASSERT_EQ(pgm.size(), header.size() + 6);
  EXPECT_EQ(pgm.substr(0, header.size()), header);
  const std::string px = pgm.substr(header.size());
  // First PGM row = top row (1): periodic grey, unknown, free; then row 0: static, free, transient grey.
  const unsigned char want[6] = {100, 205, 254, 0, 254, 100};
  for (int i = 0; i < 6; ++i) EXPECT_EQ(static_cast<unsigned char>(px[i]), want[i]) << "pixel " << i;
  auto y = yamlKeys(dir + "/m.yaml");
  EXPECT_EQ(y["image"], "m.pgm");          // relative: map_server resolves it next to the YAML
  EXPECT_EQ(y["resolution"], "0.05");
  EXPECT_EQ(y["origin"], "[-10, -10, 0.0]");
  EXPECT_EQ(y["negate"], "0");
  EXPECT_EQ(y["occupied_thresh"], "0.65");
  EXPECT_EQ(y["free_thresh"], "0.196");
  EXPECT_EQ(y["mode"], "trinary");
}

TEST(MapWriter, ResolutionAndOriginReadBackExactly) {
  const std::string dir = tempDir();
  ASSERT_FALSE(dir.empty());
  const double res = 0.0123456789012345, ox = -123.4567891, oy = 98.76543210987654;
  const auto r = writeMapPair(grid(2, 2, res, ox, oy), dir + "/m");
  ASSERT_TRUE(r.ok) << r.message;
  auto y = yamlKeys(dir + "/m.yaml");
  EXPECT_NEAR(std::strtod(y["resolution"].c_str(), nullptr), res, 1e-12);
  double px = 0, py = 0, pz = 1;
  ASSERT_EQ(std::sscanf(y["origin"].c_str(), "[%lf, %lf, %lf]", &px, &py, &pz), 3) << y["origin"];
  EXPECT_NEAR(px, ox, 1e-12);
  EXPECT_NEAR(py, oy, 1e-12);
  EXPECT_EQ(pz, 0.0);
  EXPECT_EQ(px, ox);                       // in fact exact
  EXPECT_EQ(py, oy);
}

TEST(MapWriter, MovedPairStillResolvesItsImage) {
  const std::string a = tempDir(), b = tempDir();
  ASSERT_FALSE(a.empty()); ASSERT_FALSE(b.empty());
  ASSERT_TRUE(writeMapPair(grid(4, 4, 0.1, 0.0, 0.0), a + "/m").ok);
  ASSERT_EQ(std::rename((a + "/m.pgm").c_str(), (b + "/m.pgm").c_str()), 0);
  ASSERT_EQ(std::rename((a + "/m.yaml").c_str(), (b + "/m.yaml").c_str()), 0);
  const std::string image = yamlKeys(b + "/m.yaml")["image"];
  ASSERT_FALSE(image.empty());
  ASSERT_NE(image[0], '/');
  std::ifstream moved(b + "/" + image, std::ios::binary);
  EXPECT_TRUE(moved.good()) << b + "/" + image;
}

TEST(MapWriter, MissingDirectoryIsReportedNotSaved) {
  const std::string base = "/nonexistent_strata_dir/sub/m";
  const auto r = writeMapPair(grid(2, 2, 0.05, 0.0, 0.0), base);
  EXPECT_FALSE(r.ok);
  EXPECT_NE(r.message.find(base + ".pgm"), std::string::npos) << r.message;
  EXPECT_NE(r.message.find("No such file or directory"), std::string::npos) << r.message;
}

TEST(MapWriter, GridWhoseDataDoesNotMatchItsSizeIsRefused) {
  auto g = grid(3, 3, 0.05, 0.0, 0.0);
  g.data.pop_back();
  EXPECT_FALSE(writeMapPair(g, "/tmp/strata_map_writer_unused").ok);
}
