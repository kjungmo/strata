#pragma once
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <string>
#include <sys/stat.h>
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
//    check_saved_map.py (installed with this package), does. They tie the YAML to the
//    image bytes only: two saves whose images are byte-identical are not told apart,
//    and any later edit of the image fails the check until the keys are rewritten.
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
  Mismatch,      // it does not: e.g. the new PGM beside the previous YAML, or an edited image
  Unverifiable,  // the YAML records neither (an older save, or another tool's)
  Invalid        // a file cannot be read, or the YAML is not what writeMapPair writes
};
struct MapVerifyResult {
  MapPairStatus status{MapPairStatus::Invalid};
  std::string message;
};

// Reads a whole regular file. False when it cannot be opened or read, or when the
// path is a directory or another non-regular file (an ifstream opens a directory
// and reads nothing, which would pass for an empty file).
inline bool readFile(const std::string& path, std::string& out) {
  struct stat st;
  if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream s;
  s << f.rdbuf();
  out = s.str();
  return !f.bad();
}

// The flat map YAML, read by the rules of check_saved_map.py (read_flat_yaml and its
// helpers), which is the specification: the two must give the same verdict on a
// hand-edited YAML. One `key: value` per line; a value is a plain, single-quoted or
// double-quoted scalar, or a `[a, b]` / `- a` list; a `#` at the line start or after
// a space or tab starts a comment; a line ends at LF, CRLF or CR. Anything else
// (nesting, a key given twice, an unclosed quote, a `{...}` value) is refused, not
// guessed at. Whitespace here is ASCII only, and the text is not checked to be UTF-8.
namespace flat_yaml {
struct Value {
  bool is_scalar{false};   // false: a list (its items are checked, not kept)
  std::string text;
};
inline bool isBlank(char c) { return c == ' ' || c == '\t' || c == '\x1f'; }
inline std::string rtrimmed(std::string s) {
  while (!s.empty() && isBlank(s.back())) s.pop_back();
  return s;
}
inline std::string trimmed(const std::string& s) {
  std::size_t b = 0;
  while (b < s.size() && isBlank(s[b])) ++b;
  return rtrimmed(s.substr(b));
}
// Drops a `# comment` outside quotes.
inline std::string withoutComment(const std::string& line) {
  char quote = 0;
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (quote) { if (c == quote) quote = 0; }
    else if (c == '\'' || c == '"') quote = c;
    else if (c == '#' && (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t')) return line.substr(0, i);
  }
  return line;
}
// One scalar: 'it''s' -> it's, "a\"b" -> a"b, plain text as it is. False when a quote is not closed.
inline bool scalar(const std::string& raw, std::string& out) {
  const std::string t = trimmed(raw);
  out.clear();
  if (!t.empty() && t.front() == '\'') {
    if (t.size() < 2 || t.back() != '\'') return false;
    for (std::size_t i = 1; i + 1 < t.size(); ++i) {
      if (t[i] == '\'') {
        if (i + 2 < t.size() && t[i + 1] == '\'') ++i;
        else return false;                       // a lone quote inside the value
      }
      out += t[i];
    }
    return true;
  }
  if (!t.empty() && t.front() == '"') {
    if (t.size() < 2 || t.back() != '"') return false;
    const std::string body = t.substr(1, t.size() - 2);
    std::size_t backslashes = 0;
    for (char c : body) {
      if (c == '\\') { ++backslashes; continue; }
      if (c == '"' && backslashes % 2 == 0) return false;   // an unescaped quote inside the value
      backslashes = 0;
    }
    for (std::size_t i = 0; i < body.size(); ++i) {
      if (body[i] == '\\' && i + 1 < body.size()) {
        ++i;
        out += body[i] == 'n' ? '\n' : body[i] == 't' ? '\t' : body[i];
      } else {
        out += body[i];
      }
    }
    return true;
  }
  out = t;
  return true;
}
inline bool value(const std::string& raw, Value& v) {
  const std::string t = trimmed(raw);   // not empty: the caller checked
  v = Value{};
  if (t.front() == '[') {
    if (t.back() != ']') return false;
    const std::string inner = trimmed(t.substr(1, t.size() - 2));
    std::string item;
    for (std::size_t from = 0; !inner.empty() && from <= inner.size();) {
      const std::size_t comma = std::min(inner.find(',', from), inner.size());
      if (!scalar(inner.substr(from, comma - from), item)) return false;
      from = comma + 1;
    }
    return true;
  }
  if (std::string("{|>&*!").find(t.front()) != std::string::npos) return false;
  v.is_scalar = true;
  return scalar(t, v.text);
}
// The top-level mapping. False, with the reason in `why`, when a line is not one this reader takes.
inline bool read(const std::string& text, std::map<std::string, Value>& out, std::string& why) {
  out.clear();
  bool list_open = false;   // the last key had no value on its line: `- item` lines may follow
  std::size_t number = 0;
  const auto refuse = [&](const std::string& what) {
    why = "line " + std::to_string(number) + ": " + what;
    return false;
  };
  const std::string line_ends("\n\r\v\f\x1c\x1d\x1e");
  for (std::size_t pos = 0; pos < text.size();) {
    std::size_t end = pos;
    while (end < text.size() && line_ends.find(text[end]) == std::string::npos) ++end;
    const std::string line = rtrimmed(withoutComment(text.substr(pos, end - pos)));
    pos = end + (end + 1 < text.size() && text[end] == '\r' && text[end + 1] == '\n' ? 2 : 1);
    ++number;
    const std::string core = trimmed(line);
    if (core.empty() || core == "---" || core == "...") continue;
    if (core.front() == '-' && (core.size() == 1 || isBlank(core[1]))) {
      std::string item;
      if (!list_open) return refuse("list item outside a `key:` list");
      if (!scalar(core.substr(1), item)) return refuse("bad quoted value " + core);
      continue;
    }
    if (line.front() == ' ' || line.front() == '\t') return refuse("nested YAML is not a map YAML: " + core);
    // The key: quoted, or plain up to the first colon; the colon is followed by a blank or ends the line.
    std::size_t colon = std::string::npos;
    if (line.front() == '\'') {
      for (std::size_t i = 1; i < line.size(); ++i) {
        if (line[i] != '\'') continue;
        if (i + 1 < line.size() && line[i + 1] == '\'') { ++i; continue; }
        colon = i + 1;
        break;
      }
    } else if (line.front() == '"') {
      const std::size_t close = line.find('"', 1);
      if (close != std::string::npos) colon = close + 1;
    } else if (!isBlank(line.front()) && line.front() != ':') {
      colon = line.find(':');
    }
    if (line.front() == '\'' || line.front() == '"')
      while (colon < line.size() && isBlank(line[colon])) ++colon;
    if (colon >= line.size() || line[colon] != ':' || (colon + 1 < line.size() && !isBlank(line[colon + 1])))
      return refuse("not a `key: value` line: " + core);
    std::string key;
    if (!scalar(line.substr(0, colon), key)) return refuse("not a `key: value` line: " + core);
    if (out.count(key)) return refuse("key '" + key + "' appears twice");
    const std::string rest = trimmed(line.substr(colon + 1));
    list_open = rest.empty();
    if (list_open) out[key] = Value{};
    else if (!value(rest, out[key])) return refuse("unsupported or badly quoted value " + rest);
  }
  return true;
}
}  // namespace flat_yaml

// Checks the image a saved YAML names against the size and SHA-256 that YAML records.
// It reads the YAML as check_saved_map.py does (flat_yaml above) and resolves a
// relative image name next to the YAML, as map_server does, so a moved pair verifies.
// The two values are compared as the checker compares them: the byte count is 1..18
// decimal digits, leading zeros allowed; the hash is 64 hex digits in either case.
// A YAML the checker's reader refuses is Invalid here. check_saved_map.py also
// validates the PGM header, resolution and origin, which this does not.
inline MapVerifyResult verifyMapPair(const std::string& yaml_path) {
  using S = MapPairStatus;
  const std::size_t kMaxDigits = 18;   // as MAX_DIGITS in check_saved_map.py
  std::string yaml;
  if (!readFile(yaml_path, yaml)) return {S::Invalid, "cannot read " + yaml_path + " (missing, unreadable or not a regular file)"};
  std::map<std::string, flat_yaml::Value> doc;
  std::string why;
  if (!flat_yaml::read(yaml, doc, why)) return {S::Invalid, yaml_path + ": " + why};
  const auto image_it = doc.find("image");
  if (image_it == doc.end() || !image_it->second.is_scalar || image_it->second.text.empty())
    return {S::Invalid, yaml_path + " names no image"};
  const std::string& image_name = image_it->second.text;
  const std::string image_path =
      image_name.front() == '/' ? image_name : dirName(yaml_path) + "/" + image_name;
  std::string image;
  if (!readFile(image_path, image))
    return {S::Invalid, "cannot read the image " + image_path + " (missing, unreadable or not a regular file)"};
  const auto bytes_it = doc.find("strata_image_bytes"), sha_it = doc.find("strata_image_sha256");
  const bool has_bytes = bytes_it != doc.end(), has_sha = sha_it != doc.end();
  if (!has_bytes && !has_sha)
    return {S::Unverifiable, yaml_path + " records no strata_image_bytes / strata_image_sha256; " +
                             image_path + " cannot be matched to it"};
  if (!has_bytes || !has_sha)
    return {S::Invalid, yaml_path + " records only one of strata_image_bytes / strata_image_sha256"};
  // Digits are compared as text, so no count is ever converted to a number.
  std::string want_bytes = bytes_it->second.text, want_sha = sha_it->second.text;
  const bool bytes_ok = bytes_it->second.is_scalar && !want_bytes.empty() && want_bytes.size() <= kMaxDigits &&
                        want_bytes.find_first_not_of("0123456789") == std::string::npos;
  if (!bytes_ok)
    return {S::Invalid, yaml_path + ": strata_image_bytes is not a byte count of at most " +
                        std::to_string(kMaxDigits) + " digits: " + want_bytes.substr(0, 40)};
  const bool sha_ok = sha_it->second.is_scalar && want_sha.size() == 64 &&
                      want_sha.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
  if (!sha_ok) return {S::Invalid, yaml_path + ": strata_image_sha256 is not 64 hex digits: " + want_sha.substr(0, 80)};
  want_bytes.erase(0, std::min(want_bytes.find_first_not_of('0'), want_bytes.size() - 1));
  for (char& c : want_sha)
    if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
  const std::string actual_sha = sha256Hex(image);
  if (want_bytes != std::to_string(image.size()) || want_sha != actual_sha)
    return {S::Mismatch, "image does not match this YAML: interrupted save, or the image was edited or replaced (" +
                         image_path +
                         " is " + std::to_string(image.size()) + " bytes, sha256 " + actual_sha + "; " +
                         yaml_path + " records " + want_bytes + " bytes, sha256 " + want_sha + ")"};
  return {S::Ok, image_path + " matches " + yaml_path + " (" + std::to_string(image.size()) +
                 " bytes, sha256 " + actual_sha + ")"};
}

}  // namespace strata
