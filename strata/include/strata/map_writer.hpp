#pragma once
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include "strata_core/types.hpp"
namespace strata {

// Writes a grid as the PGM + YAML pair nav2_map_server loads, and says whether it
// worked. ROS-free so it can be unit-tested.
//  * PGM shades: free 254, transient/periodic (50, 75) 100, static (100) 0, unknown 205;
//    with occupied_thresh 0.65 and free_thresh 0.196 map_server serves 0, -1, 100, -1.
//  * Rows are written top to bottom (map_server puts the first PGM row at the top).
//  * `image:` is the PGM's file name, which map_server resolves next to the YAML (as
//    Nav2's map_saver writes it), so the pair still loads after it is moved.
//  * resolution and origin are written with the fewest digits (15..17) that parse back
//    to the same double, so a configured origin survives exactly.
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

inline MapWriteResult writeMapPair(const strata_core::GridMap& g, const std::string& base) {
  const std::string pgm = base + ".pgm", yaml = base + ".yaml";
  auto fail = [](const std::string& path, const char* what) {
    return MapWriteResult{false, "cannot write " + path + ": " + what};
  };
  if (g.meta.width <= 0 || g.meta.height <= 0 ||
      g.data.size() != static_cast<std::size_t>(g.meta.width) * static_cast<std::size_t>(g.meta.height))
    return MapWriteResult{false, "grid size does not match its data; nothing written"};
  {
    errno = 0;
    std::ofstream f(pgm, std::ios::binary);
    if (!f) return fail(pgm, errno ? std::strerror(errno) : "open failed");
    f << "P5\n" << g.meta.width << " " << g.meta.height << "\n255\n";
    for (int row = g.meta.height - 1; row >= 0; --row)
      for (int col = 0; col < g.meta.width; ++col)
        f.put(static_cast<char>(pgmShade(g.data[static_cast<std::size_t>(row) * g.meta.width + col])));
    f.close();
    if (!f) return fail(pgm, errno ? std::strerror(errno) : "write failed");
  }
  const std::size_t slash = pgm.find_last_of('/');
  const std::string image = slash == std::string::npos ? pgm : pgm.substr(slash + 1);
  errno = 0;
  std::ofstream y(yaml);
  if (!y) return fail(yaml, errno ? std::strerror(errno) : "open failed");
  y << "image: " << image << "\n"
    << "mode: trinary\n"
    << "resolution: " << exactDecimal(g.meta.resolution) << "\n"
    << "origin: [" << exactDecimal(g.meta.origin_x) << ", " << exactDecimal(g.meta.origin_y) << ", 0.0]\n"
    << "negate: 0\noccupied_thresh: 0.65\nfree_thresh: 0.196\n";
  y.close();
  if (!y) return fail(yaml, errno ? std::strerror(errno) : "write failed");
  return MapWriteResult{true, "saved " + pgm + " + " + yaml};
}

}  // namespace strata
