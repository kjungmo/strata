#pragma once
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <unistd.h>
#include "strata/sha256.hpp"
#include "strata_core/types.hpp"
namespace strata {

// Writes a grid as the PGM + YAML pair nav2_map_server loads, and says whether it
// worked. ROS-free so it can be unit-tested.
//  * PGM shades: free 254, transient/periodic (50, 75) 100, static (100) 0, unknown 205;
//    with occupied_thresh 0.65 and free_thresh 0.196 map_server serves 0, -1, 100, -1.
//  * Rows are written top to bottom (map_server puts the first PGM row at the top).
//  * `image:` is the PGM's file name, single-quoted, which map_server resolves next to
//    the YAML (as Nav2's map_saver writes it), so the pair still loads after a move.
//  * resolution and origin are written with the fewest digits (15..17) that parse back
//    to the same double, so a configured origin survives exactly. Every number is
//    formatted in the classic "C" locale, whatever the process's global locale.
//  * Both files go to <name>.tmp first and are flushed to disk (fsync); then the PGM is
//    renamed first and the YAML second, and the directory is fsynced so the renames
//    are durable. A save that fails before the renames leaves the previous pair
//    untouched (its temp files are removed). A crash or failure between the two
//    renames can pair the new PGM with the previous YAML: map_server takes the image
//    name from the YAML, so no rename order avoids this with fixed names.
//  * So that such a pair can be detected, the YAML records the image it belongs to:
//    `strata_image_bytes` (the PGM's size) and `strata_image_sha256` (64 hex digits,
//    single-quoted so it stays a string), both over the exact PGM bytes written.
//    map_server ignores these keys and does not check them: verifyMapPair below, or
//    scripts/check_saved_map.py, does. They tie the YAML to the image bytes only: two
//    saves whose images are byte-identical are not told apart.
struct MapWriteResult {
  bool ok{false};
  std::string message;   // "saved <pgm> + <yaml>", or the failing path and the reason
};

// Shortest decimal form (15, 16 or 17 significant digits) that reads back as v.
inline std::string exactDecimal(double v) {
  for (int digits = 15; digits <= std::numeric_limits<double>::max_digits10; ++digits) {
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s.precision(digits);
    s << v;
    std::istringstream back(s.str());
    back.imbue(std::locale::classic());
    double parsed = 0.0;
    back >> parsed;
    if (parsed == v || digits == std::numeric_limits<double>::max_digits10) return s.str();
  }
  return {};
}

inline unsigned char pgmShade(std::int8_t v) {
  if (v < 0) return 205;     // unknown
  if (v >= 100) return 0;    // occupied (static)
  if (v >= 50) return 100;   // periodic/transient (grey: unknown to map_server)
  return 254;                // free
}

// YAML single-quoted scalar: a quote inside is doubled.
inline std::string yamlQuoted(const std::string& s) {
  std::string out = "'";
  for (char c : s) out += c == '\'' ? std::string("''") : std::string(1, c);
  return out + "'";
}

// Writes data to path and fsyncs it. Empty string on success, else the reason.
inline std::string writeDurable(const std::string& path, const std::string& data) {
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (fd < 0) return std::strerror(errno);
  std::size_t done = 0;
  while (done < data.size()) {
    const ssize_t n = ::write(fd, data.data() + done, data.size() - done);
    if (n < 0) {
      if (errno == EINTR) continue;
      const std::string why = std::strerror(errno);
      ::close(fd);
      return why;
    }
    done += static_cast<std::size_t>(n);
  }
  if (::fsync(fd) != 0) { const std::string why = std::strerror(errno); ::close(fd); return why; }
  if (::close(fd) != 0) return std::strerror(errno);
  return {};
}

// fsync an existing file (e.g. one a library wrote). Empty string on success.
inline std::string syncPath(const std::string& path) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) return std::strerror(errno);
  const bool ok = ::fsync(fd) == 0;
  const std::string why = ok ? "" : std::strerror(errno);
  ::close(fd);
  return why;
}

// fsync a directory, so renames inside it are durable. Empty string on success.
inline std::string syncDir(const std::string& dir) {
  const int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) return std::strerror(errno);
  const bool ok = ::fsync(fd) == 0;
  const std::string why = ok ? "" : std::strerror(errno);
  ::close(fd);
  return why;
}

// The directory part of a path ("." when there is none, "/" for a file at the root).
inline std::string dirName(const std::string& path) {
  const std::size_t slash = path.find_last_of('/');
  if (slash == std::string::npos) return ".";
  return slash == 0 ? "/" : path.substr(0, slash);
}

// The file-name part after the last '/', empty for "" or a path ending in '/'.
inline std::string fileName(const std::string& path) {
  const std::size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

inline MapWriteResult writeMapPair(const strata_core::GridMap& g, const std::string& base) {
  if (fileName(base).empty())
    return MapWriteResult{false, "save_path \"" + base + "\" has no file name (it must not be empty "
                                 "or end in '/'); nothing written"};
  if (g.meta.width <= 0 || g.meta.height <= 0 ||
      g.data.size() != static_cast<std::size_t>(g.meta.width) * static_cast<std::size_t>(g.meta.height))
    return MapWriteResult{false, "grid size does not match its data; nothing written"};
  const std::string pgm = base + ".pgm", yaml = base + ".yaml";
  const std::string pgm_tmp = pgm + ".tmp", yaml_tmp = yaml + ".tmp";

  std::ostringstream p;
  p.imbue(std::locale::classic());
  p << "P5\n" << g.meta.width << " " << g.meta.height << "\n255\n";
  std::string px;
  px.reserve(g.data.size());
  for (int row = g.meta.height - 1; row >= 0; --row)
    for (int col = 0; col < g.meta.width; ++col)
      px += static_cast<char>(pgmShade(g.data[static_cast<std::size_t>(row) * g.meta.width + col]));
  const std::string image = p.str() + px;   // the exact PGM bytes: written, sized and hashed

  std::ostringstream y;
  y.imbue(std::locale::classic());
  y << "image: " << yamlQuoted(fileName(pgm)) << "\n"
    << "mode: trinary\n"
    << "resolution: " << exactDecimal(g.meta.resolution) << "\n"
    << "origin: [" << exactDecimal(g.meta.origin_x) << ", " << exactDecimal(g.meta.origin_y) << ", 0.0]\n"
    << "negate: 0\noccupied_thresh: 0.65\nfree_thresh: 0.196\n"
    << "strata_image_bytes: " << image.size() << "\n"
    << "strata_image_sha256: '" << sha256Hex(image) << "'\n";

  // Removes only regular temp files this call may have created (never a directory).
  auto fail = [&](const std::string& path, const std::string& why) {
    ::unlink(pgm_tmp.c_str());
    ::unlink(yaml_tmp.c_str());
    return MapWriteResult{false, "cannot write " + path + ": " + why};
  };
  std::string why = writeDurable(pgm_tmp, image);
  if (!why.empty()) return fail(pgm_tmp, why);
  why = writeDurable(yaml_tmp, y.str());
  if (!why.empty()) return fail(yaml_tmp, why);
  if (::rename(pgm_tmp.c_str(), pgm.c_str()) != 0) return fail(pgm, std::strerror(errno));
  if (::rename(yaml_tmp.c_str(), yaml.c_str()) != 0)
    return fail(yaml, std::string(std::strerror(errno)) + " (the PGM was already replaced: " + pgm +
                          " is the new image, " + yaml + " is the previous YAML)");
  why = syncDir(dirName(pgm));
  if (!why.empty()) return MapWriteResult{false, "cannot sync directory " + dirName(pgm) + ": " + why +
                                                     " (both files were renamed into place)"};
  return MapWriteResult{true, "saved " + pgm + " + " + yaml};
}

// Whether a saved YAML and the image beside it belong to the same save.
enum class MapPairStatus {
  Ok,            // the image has the size and SHA-256 the YAML records
  Mismatch,      // it does not: e.g. the new PGM beside the previous YAML
  Unverifiable,  // the YAML records neither (an older save, or another tool's)
  Invalid        // a file cannot be read, or the YAML is not what writeMapPair writes
};
struct MapVerifyResult {
  MapPairStatus status{MapPairStatus::Invalid};
  std::string message;
};

// Reads a whole file. False when it cannot be opened or read.
inline bool readFile(const std::string& path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream s;
  s << f.rdbuf();
  out = s.str();
  return !f.bad();
}

// Checks the image a saved YAML names against the size and SHA-256 that YAML records.
// It reads the flat YAML writeMapPair writes (one "key: value" per line) and resolves
// a relative image name next to the YAML, as map_server does, so a moved pair
// verifies. scripts/check_saved_map.py is the same check for a deployer, and also
// validates the PGM header, resolution and origin.
inline MapVerifyResult verifyMapPair(const std::string& yaml_path) {
  using S = MapPairStatus;
  std::string yaml;
  if (!readFile(yaml_path, yaml)) return {S::Invalid, "cannot read " + yaml_path};
  std::string image_name, bytes_text, sha;
  bool has_image = false, has_bytes = false, has_sha = false;
  const auto unquoted = [](const std::string& v) {   // 'it''s' -> it's
    if (v.size() < 2 || v.front() != '\'' || v.back() != '\'') return v;
    std::string out;
    for (std::size_t i = 1; i + 1 < v.size(); ++i) {
      out += v[i];
      if (v[i] == '\'' && i + 2 < v.size() && v[i + 1] == '\'') ++i;
    }
    return out;
  };
  std::istringstream lines(yaml);
  for (std::string line; std::getline(lines, line);) {
    const std::size_t colon = line.find(": ");
    if (colon == std::string::npos) continue;
    const std::string key = line.substr(0, colon), value = unquoted(line.substr(colon + 2));
    if (key == "image") { image_name = value; has_image = true; }
    else if (key == "strata_image_bytes") { bytes_text = value; has_bytes = true; }
    else if (key == "strata_image_sha256") { sha = value; has_sha = true; }
  }
  if (!has_image || image_name.empty()) return {S::Invalid, yaml_path + " names no image"};
  const std::string image_path =
      image_name.front() == '/' ? image_name : dirName(yaml_path) + "/" + image_name;
  std::string image;
  if (!readFile(image_path, image)) return {S::Invalid, "cannot read the image " + image_path};
  if (!has_bytes && !has_sha)
    return {S::Unverifiable, yaml_path + " records no strata_image_bytes / strata_image_sha256; " +
                             image_path + " cannot be matched to it"};
  if (!has_bytes || !has_sha)
    return {S::Invalid, yaml_path + " records only one of strata_image_bytes / strata_image_sha256"};
  const std::string actual_sha = sha256Hex(image);
  if (bytes_text != std::to_string(image.size()) || sha != actual_sha)
    return {S::Mismatch, "image does not match this YAML: saved by an interrupted save? (" + image_path +
                         " is " + std::to_string(image.size()) + " bytes, sha256 " + actual_sha + "; " +
                         yaml_path + " records " + bytes_text + " bytes, sha256 " + sha + ")"};
  return {S::Ok, image_path + " matches " + yaml_path + " (" + std::to_string(image.size()) +
                 " bytes, sha256 " + actual_sha + ")"};
}

}  // namespace strata
