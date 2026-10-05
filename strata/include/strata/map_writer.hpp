#pragma once
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <unistd.h>
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
//  * Atomic: both files go to <name>.tmp first, are flushed to disk (fsync), then
//    renamed PGM first, YAML second. On any failure the temp files are removed and an
//    existing pair at the same path is left as it was.
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

  std::ostringstream y;
  y.imbue(std::locale::classic());
  y << "image: " << yamlQuoted(fileName(pgm)) << "\n"
    << "mode: trinary\n"
    << "resolution: " << exactDecimal(g.meta.resolution) << "\n"
    << "origin: [" << exactDecimal(g.meta.origin_x) << ", " << exactDecimal(g.meta.origin_y) << ", 0.0]\n"
    << "negate: 0\noccupied_thresh: 0.65\nfree_thresh: 0.196\n";

  // Removes only regular temp files this call may have created (never a directory).
  auto fail = [&](const std::string& path, const std::string& why) {
    ::unlink(pgm_tmp.c_str());
    ::unlink(yaml_tmp.c_str());
    return MapWriteResult{false, "cannot write " + path + ": " + why};
  };
  std::string why = writeDurable(pgm_tmp, p.str() + px);
  if (!why.empty()) return fail(pgm_tmp, why);
  why = writeDurable(yaml_tmp, y.str());
  if (!why.empty()) return fail(yaml_tmp, why);
  if (::rename(pgm_tmp.c_str(), pgm.c_str()) != 0) return fail(pgm, std::strerror(errno));
  if (::rename(yaml_tmp.c_str(), yaml.c_str()) != 0) return fail(yaml, std::strerror(errno));
  return MapWriteResult{true, "saved " + pgm + " + " + yaml};
}

}  // namespace strata
