#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <locale>
#include <map>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include "strata/map_writer.hpp"
using strata::writeMapPair;
namespace fs = std::filesystem;

namespace {
// A temp directory removed when the test ends.
struct TempDir {
  std::string path;
  TempDir() {
    char tmpl[] = "/tmp/strata_map_writer_XXXXXX";
    const char* d = mkdtemp(tmpl);
    path = d ? d : "";
  }
  ~TempDir() { if (!path.empty()) { std::error_code ec; fs::remove_all(path, ec); } }
};
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
int tmpFiles(const std::string& dir) {
  int n = 0;
  for (const auto& e : fs::directory_iterator(dir))
    if (e.path().extension() == ".tmp") ++n;
  return n;
}
// Thousands grouping and a decimal comma, as some locales have.
struct CommaPunct : std::numpunct<char> {
  char do_decimal_point() const override { return ','; }
  char do_thousands_sep() const override { return '.'; }
  std::string do_grouping() const override { return "\3"; }
};
}  // namespace

TEST(MapWriter, WritesAPairWithRowsTopDownAndTheImageNextToTheYaml) {
  TempDir dir;
  ASSERT_FALSE(dir.path.empty());
  auto g = grid(3, 2, 0.05, -10.0, -10.0);
  g.data = {100, 0, 50,     // row 0 (bottom in the world)
            75, -1, 0};     // row 1 (top)
  const auto r = writeMapPair(g, dir.path + "/m");
  ASSERT_TRUE(r.ok) << r.message;
  EXPECT_EQ(r.message, "saved " + dir.path + "/m.pgm + " + dir.path + "/m.yaml");
  const std::string pgm = slurp(dir.path + "/m.pgm");
  const std::string header = "P5\n3 2\n255\n";
  ASSERT_EQ(pgm.size(), header.size() + 6);
  EXPECT_EQ(pgm.substr(0, header.size()), header);
  const std::string px = pgm.substr(header.size());
  // First PGM row = top row (1): periodic grey, unknown, free; then row 0: static, free, transient grey.
  const unsigned char want[6] = {100, 205, 254, 0, 254, 100};
  for (int i = 0; i < 6; ++i) EXPECT_EQ(static_cast<unsigned char>(px[i]), want[i]) << "pixel " << i;
  auto y = yamlKeys(dir.path + "/m.yaml");
  EXPECT_EQ(y["image"], "'m.pgm'");        // relative: map_server resolves it next to the YAML
  EXPECT_EQ(y["resolution"], "0.05");
  EXPECT_EQ(y["origin"], "[-10, -10, 0.0]");
  EXPECT_EQ(y["negate"], "0");
  EXPECT_EQ(y["occupied_thresh"], "0.65");
  EXPECT_EQ(y["free_thresh"], "0.196");
  EXPECT_EQ(y["mode"], "trinary");
  EXPECT_EQ(tmpFiles(dir.path), 0);
}

TEST(MapWriter, ResolutionAndOriginReadBackExactly) {
  TempDir dir;
  ASSERT_FALSE(dir.path.empty());
  const double res = 0.0123456789012345, ox = -123.4567891, oy = 98.76543210987654;
  const auto r = writeMapPair(grid(2, 2, res, ox, oy), dir.path + "/m");
  ASSERT_TRUE(r.ok) << r.message;
  auto y = yamlKeys(dir.path + "/m.yaml");
  EXPECT_NEAR(std::strtod(y["resolution"].c_str(), nullptr), res, 1e-12);
  double px = 0, py = 0, pz = 1;
  ASSERT_EQ(std::sscanf(y["origin"].c_str(), "[%lf, %lf, %lf]", &px, &py, &pz), 3) << y["origin"];
  EXPECT_NEAR(px, ox, 1e-12);
  EXPECT_NEAR(py, oy, 1e-12);
  EXPECT_EQ(pz, 0.0);
  EXPECT_EQ(px, ox);                       // in fact exact
  EXPECT_EQ(py, oy);
}

TEST(MapWriter, NumbersIgnoreTheGlobalLocale) {
  // A global locale with grouping and a decimal comma must not leak into the files
  // ("1.200 1" as the PGM size, "0,05" as the resolution).
  const std::locale saved = std::locale::global(std::locale(std::locale::classic(), new CommaPunct));
  TempDir dir;
  const auto r = writeMapPair(grid(1200, 1, 0.05, -10.25, 3.5), dir.path + "/m");
  std::locale::global(saved);
  ASSERT_TRUE(r.ok) << r.message;
  EXPECT_EQ(slurp(dir.path + "/m.pgm").substr(0, 14), "P5\n1200 1\n255\n");
  auto y = yamlKeys(dir.path + "/m.yaml");
  EXPECT_EQ(y["resolution"], "0.05");
  EXPECT_EQ(y["origin"], "[-10.25, 3.5, 0.0]");
}

TEST(MapWriter, ImageNameIsSingleQuotedWithQuotesDoubled) {
  TempDir dir;
  const auto r = writeMapPair(grid(2, 2, 0.05, 0.0, 0.0), dir.path + "/it's: #map");
  ASSERT_TRUE(r.ok) << r.message;
  EXPECT_EQ(yamlKeys(dir.path + "/it's: #map.yaml")["image"], "'it''s: #map.pgm'");
}

TEST(MapWriter, SavePathWithoutAFileNameIsRefused) {
  TempDir dir;
  for (const std::string& base : {dir.path + "/", std::string()}) {
    const auto r = writeMapPair(grid(2, 2, 0.05, 0.0, 0.0), base);
    EXPECT_FALSE(r.ok) << base;
    EXPECT_NE(r.message.find("has no file name"), std::string::npos) << r.message;
  }
  EXPECT_TRUE(fs::is_empty(dir.path));
}

TEST(MapWriter, MovedPairStillResolvesItsImage) {
  TempDir a, b;
  ASSERT_FALSE(a.path.empty()); ASSERT_FALSE(b.path.empty());
  ASSERT_TRUE(writeMapPair(grid(4, 4, 0.1, 0.0, 0.0), a.path + "/m").ok);
  ASSERT_EQ(std::rename((a.path + "/m.pgm").c_str(), (b.path + "/m.pgm").c_str()), 0);
  ASSERT_EQ(std::rename((a.path + "/m.yaml").c_str(), (b.path + "/m.yaml").c_str()), 0);
  std::string image = yamlKeys(b.path + "/m.yaml")["image"];
  ASSERT_GE(image.size(), 3u);
  image = image.substr(1, image.size() - 2);   // unquote
  ASSERT_NE(image[0], '/');
  std::ifstream moved(b.path + "/" + image, std::ios::binary);
  EXPECT_TRUE(moved.good()) << b.path + "/" + image;
}

TEST(MapWriter, MissingDirectoryIsReportedNotSaved) {
  const std::string base = "/nonexistent_strata_dir/sub/m";
  const auto r = writeMapPair(grid(2, 2, 0.05, 0.0, 0.0), base);
  EXPECT_FALSE(r.ok);
  EXPECT_NE(r.message.find(base + ".pgm"), std::string::npos) << r.message;
  EXPECT_NE(r.message.find("No such file or directory"), std::string::npos) << r.message;
}

TEST(MapWriter, FailedYamlWriteLeavesThePreviousPairUntouched) {
  TempDir dir;
  const std::string base = dir.path + "/m";
  auto first = grid(2, 2, 0.05, 1.0, 2.0);
  first.data = {0, 100, -1, 0};
  ASSERT_TRUE(writeMapPair(first, base).ok);
  const std::string pgm_before = slurp(base + ".pgm"), yaml_before = slurp(base + ".yaml");
  // Make the YAML's temp path unwritable: a directory sits where the file would go.
  ASSERT_EQ(mkdir((base + ".yaml.tmp").c_str(), 0755), 0);
  auto second = grid(3, 3, 0.1, -5.0, -5.0);
  second.data.assign(9, 100);
  const auto r = writeMapPair(second, base);
  EXPECT_FALSE(r.ok);
  EXPECT_NE(r.message.find(base + ".yaml.tmp"), std::string::npos) << r.message;
  EXPECT_EQ(slurp(base + ".pgm"), pgm_before);
  EXPECT_EQ(slurp(base + ".yaml"), yaml_before);
  EXPECT_FALSE(fs::exists(base + ".pgm.tmp"));     // the PGM written first was removed
  ASSERT_EQ(rmdir((base + ".yaml.tmp").c_str()), 0);
  EXPECT_EQ(tmpFiles(dir.path), 0);
}

TEST(MapWriter, GridWhoseDataDoesNotMatchItsSizeIsRefused) {
  auto g = grid(3, 3, 0.05, 0.0, 0.0);
  g.data.pop_back();
  EXPECT_FALSE(writeMapPair(g, "/tmp/strata_map_writer_unused").ok);
}
