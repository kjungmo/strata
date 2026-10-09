#include <cctype>
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
#include <sys/wait.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include "strata/map_writer.hpp"
using strata::MapPairStatus;
using strata::sha256Hex;
using strata::verifyMapPair;
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
void spit(const std::string& path, const std::string& data) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  f << data;
}
// The YAML without the lines that record the image (what an older save wrote).
std::string withoutImageKeys(const std::string& yaml) {
  std::istringstream in(yaml);
  std::string out;
  for (std::string line; std::getline(in, line);)
    if (line.rfind("strata_image_", 0) != 0) out += line + "\n";
  return out;
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

TEST(MapWriter, FailedSecondRenameSaysThePgmWasAlreadyReplaced) {
  TempDir dir;
  const std::string base = dir.path + "/m";
  // A non-empty directory where the YAML goes: the PGM rename succeeds, the YAML's fails.
  ASSERT_EQ(mkdir((base + ".yaml").c_str(), 0755), 0);
  ASSERT_EQ(mkdir((base + ".yaml/keep").c_str(), 0755), 0);
  const auto r = writeMapPair(grid(2, 2, 0.05, 0.0, 0.0), base);
  EXPECT_FALSE(r.ok);
  EXPECT_NE(r.message.find("the PGM was already replaced"), std::string::npos) << r.message;
  EXPECT_TRUE(fs::exists(base + ".pgm"));
  EXPECT_EQ(tmpFiles(dir.path), 0);
}

TEST(MapWriter, GridWhoseDataDoesNotMatchItsSizeIsRefused) {
  auto g = grid(3, 3, 0.05, 0.0, 0.0);
  g.data.pop_back();
  EXPECT_FALSE(writeMapPair(g, "/tmp/strata_map_writer_unused").ok);
}

TEST(Sha256, MatchesTheFips180Vectors) {
  EXPECT_EQ(sha256Hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(sha256Hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  // Two blocks: 56 bytes leave no room for the length in the first one.
  EXPECT_EQ(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  EXPECT_EQ(sha256Hex(std::string(1000000, 'a')),
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(Sha256, PaddingBoundariesAndBinaryBytes) {
  // Lengths either side of the one-block / two-block padding boundary (55 | 56) and of
  // a full block (64); expected values from Python's hashlib.
  EXPECT_EQ(sha256Hex(std::string(55, 'x')), "d5e285683cd4efc02d021a5c62014694958901005d6f71e89e0989fac77e4072");
  EXPECT_EQ(sha256Hex(std::string(56, 'x')), "04c26261370ee7541549d16dee320c723e3fd14671e66a099afe0a377c16888e");
  EXPECT_EQ(sha256Hex(std::string(63, 'x')), "75220b47218278e656f2013bb8f0c455a25eaf01e86c64924e9d48d89776d6f2");
  EXPECT_EQ(sha256Hex(std::string(64, 'x')), "7ce100971f64e7001e8fe5a51973ecdfe1ced42befe7ee8d5fd6219506b5393c");
  EXPECT_EQ(sha256Hex(std::string(65, 'x')), "9537c5fdf120482f7d58d25e9ed583f52c02b4e304ea814db1633ad565aed7e9");
  EXPECT_EQ(sha256Hex(std::string(120, 'x')), "13f05a0b594787f5ecd315edc96141bd3243203d1b7d4f0836f37308b276ba98");
  // Bytes above 0x7f and NULs (a PGM is binary): the 256 byte values 0..255 in order.
  std::string all;
  for (int i = 0; i < 256; ++i) all += static_cast<char>(i);
  EXPECT_EQ(sha256Hex(all), "40aff2e9d2d8922e47afd4648e6967497158785fbd1da870e7110266bf944880");
}

TEST(MapWriter, YamlRecordsTheSizeAndSha256OfTheImageWritten) {
  TempDir dir;
  ASSERT_FALSE(dir.path.empty());
  auto g = grid(3, 2, 0.05, -10.0, -10.0);
  g.data = {100, 0, 50, 75, -1, 0};
  ASSERT_TRUE(writeMapPair(g, dir.path + "/m").ok);
  // The PGM is "P5\n3 2\n255\n" + the six shades the first test checks: 17 bytes.
  const std::string pgm = slurp(dir.path + "/m.pgm");
  ASSERT_EQ(pgm, std::string("P5\n3 2\n255\n") + std::string("\x64\xcd\xfe\x00\xfe\x64", 6));
  auto y = yamlKeys(dir.path + "/m.yaml");
  EXPECT_EQ(y["strata_image_bytes"], "17");
  // sha256 of those 17 bytes, from Python's hashlib (not from sha256Hex).
  EXPECT_EQ(y["strata_image_sha256"], "'4014f0ce0204fe1d5abd97eb83f208306b82e28d4a75f2b7aa82c7a086baedf9'");
  EXPECT_EQ(y["strata_image_sha256"], "'" + sha256Hex(pgm) + "'");
  // The keys map_server reads are still there, ahead of the two it ignores.
  const std::string yaml = slurp(dir.path + "/m.yaml");
  EXPECT_LT(yaml.find("free_thresh: 0.196\n"), yaml.find("strata_image_bytes: "));
  EXPECT_EQ(y.size(), 9u);
}

TEST(MapWriter, SavedPairVerifiesAndStillDoesAfterAMove) {
  TempDir a, b;
  ASSERT_FALSE(a.path.empty()); ASSERT_FALSE(b.path.empty());
  auto g = grid(4, 4, 0.1, 0.0, 0.0);
  g.data[5] = 100;
  ASSERT_TRUE(writeMapPair(g, a.path + "/it's: #map").ok);
  auto v = verifyMapPair(a.path + "/it's: #map.yaml");
  EXPECT_EQ(v.status, MapPairStatus::Ok) << v.message;
  ASSERT_EQ(std::rename((a.path + "/it's: #map.pgm").c_str(), (b.path + "/it's: #map.pgm").c_str()), 0);
  ASSERT_EQ(std::rename((a.path + "/it's: #map.yaml").c_str(), (b.path + "/it's: #map.yaml").c_str()), 0);
  v = verifyMapPair(b.path + "/it's: #map.yaml");
  EXPECT_EQ(v.status, MapPairStatus::Ok) << v.message;
  EXPECT_NE(v.message.find(b.path + "/it's: #map.pgm matches"), std::string::npos) << v.message;
}

TEST(MapWriter, NewImageBesideThePreviousYamlFailsVerification) {
  // The state a crash between the two renames leaves: save A, then save B whose PGM
  // reached its final name while A's YAML stayed. Once with a different grid size
  // (the byte count already differs) and once with the same size (only the hash does).
  struct Case { int w, h; const char* what; };
  for (const Case& c : {Case{5, 3, "different size"}, Case{2, 2, "same size"}}) {
    TempDir dir, other;
    ASSERT_FALSE(dir.path.empty()); ASSERT_FALSE(other.path.empty());
    const std::string base = dir.path + "/m";
    auto first = grid(2, 2, 0.05, 1.0, 2.0);
    first.data = {0, 100, -1, 0};
    ASSERT_TRUE(writeMapPair(first, base).ok);
    ASSERT_EQ(verifyMapPair(base + ".yaml").status, MapPairStatus::Ok) << c.what;
    auto second = grid(c.w, c.h, 0.1, -5.0, -5.0);
    second.data.assign(static_cast<std::size_t>(c.w) * c.h, 100);
    ASSERT_TRUE(writeMapPair(second, other.path + "/m").ok);
    ASSERT_EQ(std::rename((other.path + "/m.pgm").c_str(), (base + ".pgm").c_str()), 0);
    const auto v = verifyMapPair(base + ".yaml");
    EXPECT_EQ(v.status, MapPairStatus::Mismatch) << c.what << ": " << v.message;
    EXPECT_NE(v.message.find("image does not match this YAML: interrupted save, or the image was edited or replaced"),
              std::string::npos) << v.message;
    // B's own YAML still describes that image.
    ASSERT_EQ(std::rename((other.path + "/m.yaml").c_str(), (base + ".yaml").c_str()), 0);
    EXPECT_EQ(verifyMapPair(base + ".yaml").status, MapPairStatus::Ok) << c.what;
  }
}

TEST(MapWriter, FailedSecondRenameLeavesAPairThatFailsVerification) {
  // The same state, produced by writeMapPair itself: the YAML's rename fails after the
  // PGM's succeeded. The previous YAML is kept aside and put back as the "old" one.
  TempDir dir;
  const std::string base = dir.path + "/m";
  auto first = grid(2, 2, 0.05, 1.0, 2.0);
  first.data = {0, 100, -1, 0};
  ASSERT_TRUE(writeMapPair(first, base).ok);
  const std::string previous_yaml = slurp(base + ".yaml");
  ASSERT_EQ(std::remove((base + ".yaml").c_str()), 0);
  ASSERT_EQ(mkdir((base + ".yaml").c_str(), 0755), 0);
  ASSERT_EQ(mkdir((base + ".yaml/keep").c_str(), 0755), 0);
  auto second = grid(2, 2, 0.05, 1.0, 2.0);
  second.data = {100, 100, -1, 0};
  const auto r = writeMapPair(second, base);
  ASSERT_FALSE(r.ok);
  ASSERT_NE(r.message.find("the PGM was already replaced"), std::string::npos) << r.message;
  ASSERT_EQ(rmdir((base + ".yaml/keep").c_str()), 0);
  ASSERT_EQ(rmdir((base + ".yaml").c_str()), 0);
  spit(base + ".yaml", previous_yaml);
  const auto v = verifyMapPair(base + ".yaml");
  EXPECT_EQ(v.status, MapPairStatus::Mismatch) << v.message;
}

TEST(MapWriter, ChangedImageByteFailsVerification) {
  TempDir dir;
  const std::string base = dir.path + "/m";
  ASSERT_TRUE(writeMapPair(grid(8, 8, 0.05, 0.0, 0.0), base).ok);
  std::string pgm = slurp(base + ".pgm");
  pgm.back() = static_cast<char>(254);          // one unknown pixel becomes free
  spit(base + ".pgm", pgm);
  EXPECT_EQ(verifyMapPair(base + ".yaml").status, MapPairStatus::Mismatch);
  spit(base + ".pgm", pgm + "x");               // and a longer file
  EXPECT_EQ(verifyMapPair(base + ".yaml").status, MapPairStatus::Mismatch);
}

TEST(MapWriter, YamlWithoutTheImageKeysIsUnverifiableNotOk) {
  TempDir dir;
  const std::string base = dir.path + "/m";
  ASSERT_TRUE(writeMapPair(grid(2, 2, 0.05, 0.0, 0.0), base).ok);
  const std::string yaml = slurp(base + ".yaml");
  spit(base + ".yaml", withoutImageKeys(yaml));
  ASSERT_EQ(yamlKeys(base + ".yaml").size(), 7u);
  const auto v = verifyMapPair(base + ".yaml");
  EXPECT_EQ(v.status, MapPairStatus::Unverifiable) << v.message;
  // Only one of the two keys is a damaged YAML, not an older save.
  spit(base + ".yaml", withoutImageKeys(yaml) + "strata_image_bytes: 15\n");
  EXPECT_EQ(verifyMapPair(base + ".yaml").status, MapPairStatus::Invalid);
}

TEST(MapWriter, MissingFilesAreInvalidNotVerified) {
  TempDir dir;
  const std::string base = dir.path + "/m";
  EXPECT_EQ(verifyMapPair(base + ".yaml").status, MapPairStatus::Invalid);
  ASSERT_TRUE(writeMapPair(grid(2, 2, 0.05, 0.0, 0.0), base).ok);
  ASSERT_EQ(std::remove((base + ".pgm").c_str()), 0);
  const auto v = verifyMapPair(base + ".yaml");
  EXPECT_EQ(v.status, MapPairStatus::Invalid);
  EXPECT_NE(v.message.find(base + ".pgm"), std::string::npos) << v.message;
}

// map_server and check_saved_map.py cannot load a directory as an image; neither may this.
TEST(MapWriter, ImageThatIsNotARegularFileIsInvalid) {
  TempDir dir;
  const std::string base = dir.path + "/m";
  ASSERT_TRUE(writeMapPair(grid(2, 2, 0.05, 0.0, 0.0), base).ok);
  ASSERT_EQ(std::remove((base + ".pgm").c_str()), 0);
  ASSERT_EQ(::mkdir((base + ".pgm").c_str(), 0755), 0);
  const auto with_keys = verifyMapPair(base + ".yaml");
  EXPECT_EQ(with_keys.status, MapPairStatus::Invalid) << with_keys.message;
  EXPECT_NE(with_keys.message.find(base + ".pgm"), std::string::npos) << with_keys.message;
  // Without the keys it must not pass as merely unverifiable either.
  spit(base + ".yaml", withoutImageKeys(slurp(base + ".yaml")));
  const auto without_keys = verifyMapPair(base + ".yaml");
  EXPECT_EQ(without_keys.status, MapPairStatus::Invalid) << without_keys.message;
  // A YAML path that is a directory is invalid too.
  EXPECT_EQ(verifyMapPair(dir.path).status, MapPairStatus::Invalid);
}

// A YAML saved or edited with CRLF line ends verifies like the LF one (as the checker reads it).
TEST(MapWriter, YamlWithCrlfLineEndsVerifiesLikeTheLfOne) {
  TempDir dir;
  const std::string base = dir.path + "/m";
  ASSERT_TRUE(writeMapPair(grid(3, 2, 0.05, 0.0, 0.0), base).ok);
  const std::string lf = slurp(base + ".yaml");
  const auto toCrlf = [](const std::string& text) {
    std::string out;
    for (char c : text) out += c == '\n' ? std::string("\r\n") : std::string(1, c);
    return out;
  };
  ASSERT_NE(toCrlf(lf), lf);
  spit(base + ".yaml", toCrlf(lf));
  const auto ok = verifyMapPair(base + ".yaml");
  EXPECT_EQ(ok.status, MapPairStatus::Ok) << ok.message;
  // Without the keys the CRLF YAML is unverifiable, and beside another image a mismatch.
  spit(base + ".yaml", toCrlf(withoutImageKeys(lf)));
  const auto no_keys = verifyMapPair(base + ".yaml");
  EXPECT_EQ(no_keys.status, MapPairStatus::Unverifiable) << no_keys.message;
  std::string image = slurp(base + ".pgm");
  image.back() = static_cast<char>(image.back() ^ 1);
  spit(base + ".pgm", image);
  spit(base + ".yaml", toCrlf(lf));
  const auto mismatch = verifyMapPair(base + ".yaml");
  EXPECT_EQ(mismatch.status, MapPairStatus::Mismatch) << mismatch.message;
}

namespace {
// Exit status of check_saved_map.py (0 OK, 1 FAIL, 3 UNVERIFIABLE) on a YAML; -1 if it did not run.
int checkerExit(const std::string& yaml_path) {
  const std::string cmd = std::string("python3 '") + STRATA_CHECK_SAVED_MAP + "' '" + yaml_path + "' >/dev/null 2>&1";
  const int rc = std::system(cmd.c_str());
  return rc != -1 && WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}
// A saved 3 x 2 pair whose YAML lines a test rewrites by hand, as a deployer would.
struct EditedPair {
  TempDir dir;
  std::string base, yaml, bytes, sha;
  EditedPair() : base(dir.path + "/m") {
    EXPECT_TRUE(writeMapPair(grid(3, 2, 0.05, 0.0, 0.0), base).ok);
    yaml = slurp(base + ".yaml");
    const std::string pgm = slurp(base + ".pgm");
    bytes = std::to_string(pgm.size());
    sha = sha256Hex(pgm);
  }
  // The YAML with the line of `key` replaced by `line` (no line end in `line`).
  std::string with(const std::string& key, const std::string& line) const {
    std::istringstream in(yaml);
    std::string out;
    for (std::string l; std::getline(in, l);) out += (l.rfind(key + ":", 0) == 0 ? line : l) + "\n";
    return out;
  }
  // The status verifyMapPair gives this YAML text. It also runs check_saved_map.py on
  // the same file and expects the matching exit status, so every row of every test
  // below is an agreement case between the two.
  MapPairStatus verify(const std::string& text) const {
    spit(base + ".yaml", text);
    const MapPairStatus status = verifyMapPair(base + ".yaml").status;
    const int want = status == MapPairStatus::Ok ? 0 : status == MapPairStatus::Unverifiable ? 3 : 1;
    EXPECT_EQ(checkerExit(base + ".yaml"), want) << "check_saved_map.py disagrees on:\n" << text;
    return status;
  }
};
std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
}  // namespace

// Rewriting the two keys by hand is the documented remedy after an image edit, so a
// YAML that check_saved_map.py accepts must verify here too, and the other way round.
// Each row is OK (exit 0) for the checker. Before the fix only the last two rows
// were Ok here: the tab row was Invalid and the others Mismatch.
TEST(MapWriter, HandEditedKeysVerifyAsTheCheckerReadsThem) {
  const EditedPair p;
  ASSERT_EQ(p.verify(p.yaml), MapPairStatus::Ok);
  const std::string b = "strata_image_bytes", s = "strata_image_sha256";
  const struct { const char* what; std::string text; } rows[] = {
      {"leading zeros in the byte count", p.with(b, b + ": 00" + p.bytes)},
      {"zero-padded to 18 digits", p.with(b, b + ": " + std::string(18 - p.bytes.size(), '0') + p.bytes)},
      {"upper-case hash", p.with(s, s + ": '" + upper(p.sha) + "'")},
      {"double-quoted hash", p.with(s, s + ": \"" + p.sha + "\"")},
      {"unquoted hash", p.with(s, s + ": " + p.sha)},
      {"quoted byte count", p.with(b, b + ": '" + p.bytes + "'")},
      {"trailing spaces after the byte count", p.with(b, b + ": " + p.bytes + "  ")},
      {"comment after the byte count", p.with(b, b + ": " + p.bytes + " # edited")},
      {"comment after the hash", p.with(s, s + ": '" + p.sha + "'\t# edited")},
      {"two spaces after the colon", p.with(b, b + ":  " + p.bytes)},
      {"tab after the colon", p.with(b, b + ":\t" + p.bytes)},
      {"spaces before the colon", p.with(s, s + " : '" + p.sha + "'")},
      {"quoted key", p.with(b, "'" + b + "': " + p.bytes)},
      {"lone CR line ends", [&] { std::string t = p.yaml; for (char& c : t) if (c == '\n') c = '\r'; return t; }()},
      {"comment and blank lines", "# saved map\n\n---\n" + p.yaml + "  # end\n"},
      {"commented-out copies of the keys", p.yaml + "# strata_image_bytes: 1\n#strata_image_sha256: 'x'\n"},
  };
  for (const auto& r : rows) EXPECT_EQ(p.verify(r.text), MapPairStatus::Ok) << r.what << "\n" << r.text;
  // The same edits still fail once the image is another one.
  std::string pgm = slurp(p.base + ".pgm");
  pgm.back() = static_cast<char>(pgm.back() ^ 1);
  spit(p.base + ".pgm", pgm);
  for (const auto& r : rows) EXPECT_EQ(p.verify(r.text), MapPairStatus::Mismatch) << r.what << "\n" << r.text;
}

// The checker refuses a YAML that gives a key twice (FAIL), even with the same value:
// which of the two lines a reader takes is not defined. Before the fix the first,
// second and last rows were Ok here.
TEST(MapWriter, KeyGivenTwiceIsInvalid) {
  const EditedPair p;
  const std::string rows[] = {
      p.yaml + "strata_image_bytes: " + p.bytes + "\n",
      p.yaml + "strata_image_sha256: '" + p.sha + "'\n",
      p.yaml + "strata_image_bytes: 1\n",
      p.yaml + "image: 'm.pgm'\n",
      p.yaml + "'strata_image_bytes' : " + p.bytes + "\n",
  };
  for (const auto& text : rows) {
    EXPECT_EQ(p.verify(text), MapPairStatus::Invalid) << text;
    const auto v = verifyMapPair(p.base + ".yaml");
    EXPECT_NE(v.message.find("appears twice"), std::string::npos) << v.message;
  }
}

// Values the checker refuses outright (FAIL before any comparison) are Invalid, and
// no digit run is turned into a number: 18 digits is the cap, as in the checker.
TEST(MapWriter, ByteCountsAndHashesTheCheckerRefusesAreInvalid) {
  const EditedPair p;
  const std::string b = "strata_image_bytes", s = "strata_image_sha256";
  const struct { const char* what; std::string text; MapPairStatus want; } rows[] = {
      {"18 nines: a count, the wrong one", p.with(b, b + ": " + std::string(18, '9')), MapPairStatus::Mismatch},
      {"19 digits", p.with(b, b + ": " + std::string(19 - p.bytes.size(), '0') + p.bytes), MapPairStatus::Invalid},
      {"5000 digits", p.with(b, b + ": " + std::string(5000, '9')), MapPairStatus::Invalid},
      {"signed count", p.with(b, b + ": +" + p.bytes), MapPairStatus::Invalid},
      {"count with a decimal point", p.with(b, b + ": " + p.bytes + ".0"), MapPairStatus::Invalid},
      {"empty count", p.with(b, b + ":"), MapPairStatus::Invalid},
      {"count as a list", p.with(b, b + ": [" + p.bytes + "]"), MapPairStatus::Invalid},
      {"63 hex digits", p.with(s, s + ": '" + p.sha.substr(1) + "'"), MapPairStatus::Invalid},
      {"65 hex digits", p.with(s, s + ": '0" + p.sha + "'"), MapPairStatus::Invalid},
      {"a letter that is not hex", p.with(s, s + ": 'g" + p.sha.substr(1) + "'"), MapPairStatus::Invalid},
      {"hash with a 0x prefix", p.with(s, s + ": 0x" + p.sha), MapPairStatus::Invalid},
      {"quote not closed", p.with(s, s + ": '" + p.sha), MapPairStatus::Invalid},
      {"double quote not closed", p.with(s, s + ": \"" + p.sha), MapPairStatus::Invalid},
  };
  for (const auto& r : rows) EXPECT_EQ(p.verify(r.text), r.want) << r.what << "\n" << r.text;
}

// Lines the checker's reader does not take make the whole YAML Invalid, with or
// without the keys: an indented key is not a top-level key, so it must not count.
TEST(MapWriter, YamlTheCheckerRefusesIsInvalid) {
  const EditedPair p;
  const std::string b = "strata_image_bytes";
  const struct { const char* what; std::string text; } rows[] = {
      {"indented line", p.with("negate", " negate: 0")},
      {"no blank after the colon", p.with("negate", "negate:0")},
      {"line without a colon", p.yaml + "strata\n"},
      {"flow mapping value", p.yaml + "extra: {a: 1}\n"},
      {"list item with no list open", p.yaml + "- 1\n"},
      {"list not closed on its line", p.with("origin", "origin: [0.0, 0.0,")},
  };
  for (const auto& r : rows) {
    EXPECT_EQ(p.verify(r.text), MapPairStatus::Invalid) << r.what << "\n" << r.text;
    EXPECT_EQ(p.verify(withoutImageKeys(r.text)), MapPairStatus::Invalid) << r.what << " (no keys)";
  }
  EXPECT_EQ(p.verify(p.with(b, " " + b + ": " + p.bytes)), MapPairStatus::Invalid);
  EXPECT_EQ(p.verify(p.with(b, b + ":" + p.bytes)), MapPairStatus::Invalid);
  // A block list and a double-quoted image name are read, as the checker reads them.
  EXPECT_EQ(p.verify(p.with("origin", "origin:\n  - 0.0\n  - 0.0\n  - 0.0")), MapPairStatus::Ok);
  EXPECT_EQ(p.verify(p.with("image", "image: \"m.pgm\"  # the image")), MapPairStatus::Ok);
}
