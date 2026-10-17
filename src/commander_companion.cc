#include "commander_companion.h"
#if defined(_WIN32)
#include "windows_private_file.h"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <numeric>
#include <regex>
#include <set>
#include <sstream>
#include <utility>

#if defined(__linux__)
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace companion {
namespace {
constexpr uint64_t kMaxSafeInteger = 9007199254740991ULL;
struct Json {
  enum Type { Null, Boolean, Number, String, Object, Array } type = Null;
  bool boolean = false;
  uint64_t number = 0;
  std::string string;
  std::map<std::string, Json> object;
  std::vector<Json> array;
};

bool Utf8(const std::string& s) {
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = s[i++];
    if (c < 0x80) continue;
    uint32_t value = 0, minimum = 0;
    size_t count = 0;
    if (c >= 0xC2 && c <= 0xDF) { count = 1; value = c & 31; minimum = 0x80; }
    else if (c >= 0xE0 && c <= 0xEF) { count = 2; value = c & 15; minimum = 0x800; }
    else if (c >= 0xF0 && c <= 0xF4) { count = 3; value = c & 7; minimum = 0x10000; }
    else return false;
    if (i + count > s.size()) return false;
    while (count--) {
      const unsigned char next = s[i++];
      if ((next & 0xC0) != 0x80) return false;
      value = (value << 6) | (next & 63);
    }
    if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
      return false;
  }
  return true;
}

void AppendUtf8(uint32_t c, std::string& out) {
  if (c < 0x80) out += static_cast<char>(c);
  else if (c < 0x800) {
    out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 63));
  } else if (c < 0x10000) {
    out += static_cast<char>(0xE0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 63));
    out += static_cast<char>(0x80 | (c & 63));
  } else {
    out += static_cast<char>(0xF0 | (c >> 18)); out += static_cast<char>(0x80 | ((c >> 12) & 63));
    out += static_cast<char>(0x80 | ((c >> 6) & 63)); out += static_cast<char>(0x80 | (c & 63));
  }
}

// This deliberately accepts only the JSON number subset in the closed schema:
// non-negative safe decimal integers, not floats, exponents or coerced strings.
class Parser {
 public:
  explicit Parser(const std::string& source) : source_(source) {}
  bool Parse(Json& out) {
    return !source_.empty() && source_.size() <= kMaximumSnapshotBytes &&
           Value(out, 0) && (Space(), position_ == source_.size());
  }
 private:
  void Space() { while (position_ < source_.size() &&
      (source_[position_] == ' ' || source_[position_] == '\t' ||
       source_[position_] == '\r' || source_[position_] == '\n')) ++position_; }
  bool Take(char c) { Space(); if (position_ >= source_.size() || source_[position_] != c) return false;
    ++position_; return true; }
  bool Literal(const char* word) {
    const std::string text(word);
    if (source_.compare(position_, text.size(), text) != 0) return false;
    position_ += text.size(); return true;
  }
  bool Hex4(uint32_t& result) {
    result = 0;
    for (int i = 0; i < 4; ++i) {
      if (position_ >= source_.size()) return false;
      const char c = source_[position_++];
      int n = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
              c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
      if (n < 0) return false;
      result = (result << 4) | static_cast<uint32_t>(n);
    }
    return true;
  }
  bool Text(std::string& out) {
    if (!Take('"')) return false;
    while (position_ < source_.size()) {
      const unsigned char c = source_[position_++];
      if (c == '"') return Utf8(out);
      if (c < 32) return false;
      if (c != '\\') { out += static_cast<char>(c); continue; }
      if (position_ >= source_.size()) return false;
      const char escaped = source_[position_++];
      if (escaped == '"' || escaped == '\\' || escaped == '/') out += escaped;
      else if (escaped == 'b') out += '\b';
      else if (escaped == 'f') out += '\f';
      else if (escaped == 'n') out += '\n';
      else if (escaped == 'r') out += '\r';
      else if (escaped == 't') out += '\t';
      else if (escaped == 'u') {
        uint32_t code = 0; if (!Hex4(code)) return false;
        if (code >= 0xD800 && code <= 0xDBFF) {
          if (source_.compare(position_, 2, "\\u") != 0) return false;
          position_ += 2; uint32_t low = 0;
          if (!Hex4(low) || low < 0xDC00 || low > 0xDFFF) return false;
          code = 0x10000 + ((code - 0xD800) << 10) + low - 0xDC00;
        } else if (code >= 0xDC00 && code <= 0xDFFF) return false;
        AppendUtf8(code, out);
      } else return false;
    }
    return false;
  }
  bool Value(Json& out, unsigned depth) {
    Space();
    if (depth > 12 || ++nodes_ > 4096 || position_ >= source_.size()) return false;
    const char c = source_[position_];
    if (c == '{') {
      out.type = Json::Object; ++position_; if (Take('}')) return true;
      do {
        std::string key; Json child;
        if (!Text(key) || !Take(':') || !Value(child, depth + 1) ||
            !out.object.emplace(key, std::move(child)).second) return false;
        if (Take('}')) return true;
      } while (Take(','));
      return false;
    }
    if (c == '[') {
      out.type = Json::Array; ++position_; if (Take(']')) return true;
      do {
        Json child; if (!Value(child, depth + 1)) return false;
        out.array.push_back(std::move(child)); if (Take(']')) return true;
      } while (Take(','));
      return false;
    }
    if (c == '"') { out.type = Json::String; return Text(out.string); }
    if (c == 'n') return Literal("null");
    if (c == 't' || c == 'f') {
      out.type = Json::Boolean; out.boolean = c == 't'; return Literal(c == 't' ? "true" : "false");
    }
    if (c < '0' || c > '9') return false;
    out.type = Json::Number;
    const size_t start = position_;
    while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9') {
      const unsigned digit = source_[position_++] - '0';
      if (out.number > (kMaxSafeInteger - digit) / 10) return false;
      out.number = out.number * 10 + digit;
    }
    return position_ - start == 1 || source_[start] != '0';
  }
  const std::string& source_;
  size_t position_ = 0, nodes_ = 0;
};

bool Keys(const Json& object, std::initializer_list<const char*> keys) {
  if (object.type != Json::Object || object.object.size() != keys.size()) return false;
  return std::all_of(keys.begin(), keys.end(), [&object](const char* key) { return object.object.count(key) != 0; });
}
const Json& At(const Json& value, const char* key) { return value.object.at(key); }
bool AsText(const Json& value, std::string& out, size_t cap, bool nullable = true) {
  if (nullable && value.type == Json::Null) { out.clear(); return true; }
  if (value.type != Json::String || value.string.empty() || value.string.size() > cap) return false;
  if (nullable) {
    static const std::regex sensitive(R"(\b(?:sk|ghp|github_pat)[-_][A-Za-z0-9_-]{16,}|\bBearer\s+\S+|(?:password|api[_ -]?key|access[_ -]?token|secret)\s*[=:]\s*\S+)", std::regex::icase);
    if (std::regex_search(value.string, sensitive)) return false;
  }
  // Reject controls, bidi overrides, and zero-width controls in displayed identity.
  if (std::any_of(value.string.begin(), value.string.end(), [](unsigned char c) { return c < 32 || c == 127; })) return false;
  if (value.string.find("\xE2\x80\x8B") != std::string::npos ||
      value.string.find("\xE2\x80\x8E") != std::string::npos ||
      value.string.find("\xE2\x80\x8F") != std::string::npos) return false;
  for (unsigned c = 0xAA; c <= 0xAE; ++c)
    if (value.string.find(std::string("\xE2\x80") + static_cast<char>(c)) != std::string::npos) return false;
  for (unsigned c = 0xA6; c <= 0xA9; ++c)
    if (value.string.find(std::string("\xE2\x81") + static_cast<char>(c)) != std::string::npos) return false;
  out = value.string; return true;
}
bool Alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool Digit(char c) { return c >= '0' && c <= '9'; }
bool Token(const std::string& s, bool tools = false, bool extended = false) {
  if (s.empty()) return true;
  if (!(Alpha(s[0]) || (!tools && Digit(s[0])))) return false;
  return std::all_of(s.begin(), s.end(), [extended](char c) {
    return Alpha(c) || Digit(c) || c == '_' || c == '.' || c == ':' || c == '-' ||
           (extended && (c == '@' || c == '/'));
  });
}
bool Identifier(const std::string& s) {
  if (s.empty()) return true;
  if (s[0] < 'a' || s[0] > 'z') return false;
  return std::all_of(s.begin(), s.end(), [](char c) { return (c >= 'a' && c <= 'z') || Digit(c) || c == '_' || c == '-'; });
}
bool Hex(const std::string& s, size_t length) {
  if (s.empty()) return true;
  return s.size() == length && std::all_of(s.begin(), s.end(), [](char c) {
    return Digit(c) || (c >= 'a' && c <= 'f'); });
}
bool Integer(const Json& value, uint64_t& out, uint64_t cap = kMaxSafeInteger, bool nullable = true) {
  if (nullable && value.type == Json::Null) { out = 0; return true; }
  if (value.type != Json::Number || value.number == 0 || value.number > cap) return false;
  out = value.number; return true;
}
bool NonnegativeInteger(const Json& value, uint64_t& out, uint64_t cap = kMaxSafeInteger) {
  if (value.type != Json::Number || value.number > cap) return false;
  out = value.number; return true;
}
bool Names(const Json& value, std::vector<std::string>& out, size_t cap, bool extended = false) {
  if (value.type != Json::Array || value.array.size() > cap) return false;
  std::set<std::string> unique;
  for (const auto& entry : value.array) {
    std::string name;
    if (!AsText(entry, name, 128, false) || !Token(name, !extended, extended) || !unique.insert(name).second)
      return false;
    out.push_back(name);
  }
  return true;
}
bool OneOf(const std::string& s, std::initializer_list<const char*> names) {
  return std::any_of(names.begin(), names.end(), [&s](const char* name) { return s == name; });
}
bool BindingFields(const Json& value, Binding& out) {
  if (!Keys(value, {"appId", "accountId", "profileId", "workflowId", "projectRoot", "chatUrl"})) return false;
  return AsText(At(value, "appId"), out.app_id, 128) && Token(out.app_id) &&
    AsText(At(value, "accountId"), out.account_id, 128) && Token(out.account_id) &&
    AsText(At(value, "profileId"), out.profile_id, 128) && Token(out.profile_id) &&
    AsText(At(value, "workflowId"), out.workflow_id, 64) && Identifier(out.workflow_id) &&
    AsText(At(value, "projectRoot"), out.project_root, 16384) &&
    (out.project_root.empty() || IsCanonicalProjectRoot(out.project_root)) &&
    AsText(At(value, "chatUrl"), out.chat_url, 1024) && (out.chat_url.empty() || IsCanonicalChatUrl(out.chat_url));
}
bool Complete(const Binding& b) {
  return !b.app_id.empty() && !b.account_id.empty() && !b.profile_id.empty() &&
         !b.workflow_id.empty() && !b.project_root.empty() && !b.chat_url.empty();
}
bool DeviceName(const Json& value, std::string& out) {
  if (!AsText(value, out, 512)) return false;
  const auto characters = std::count_if(out.begin(), out.end(), [](unsigned char c) { return (c & 0xC0) != 0x80; });
  return characters <= 128;
}
bool SameBinding(const Binding& a, const Binding& b) {
  return a.app_id == b.app_id && a.account_id == b.account_id && a.profile_id == b.profile_id &&
         a.workflow_id == b.workflow_id && SameProjectRoot(a.project_root, b.project_root) && a.chat_url == b.chat_url;
}
bool KnownDifference(const std::string& a, const std::string& b) {
  return !a.empty() && !b.empty() && a != b;
}
bool KnownRootDifference(const std::string& a, const std::string& b) {
  return !a.empty() && !b.empty() && !SameProjectRoot(a, b);
}
bool NativeConflict(const Binding& expected, const Observation& s) {
  const auto& b = s.binding;
  return KnownDifference(expected.app_id, b.app_id) || KnownDifference(expected.account_id, b.account_id) ||
    KnownDifference(expected.profile_id, b.profile_id) || KnownDifference(expected.workflow_id, b.workflow_id) ||
    KnownRootDifference(expected.project_root, b.project_root) || KnownDifference(expected.chat_url, b.chat_url) ||
    KnownDifference(expected.app_id, s.app_id) || KnownDifference(expected.profile_id, s.profile) ||
    KnownDifference(expected.workflow_id, s.project_id) || KnownRootDifference(expected.project_root, s.project_root) ||
    KnownDifference(expected.app_id, s.chat_app_id) || KnownDifference(expected.chat_url, s.conversation_url);
}
bool InternallyBound(const Observation& s) {
  return Complete(s.binding) && s.backend_state == "CONFIRMED" && s.chat_state == "CONFIRMED" &&
    s.project_state == "BOUND" && s.project_id == s.binding.workflow_id &&
    s.app_id == s.binding.app_id && s.profile == s.binding.profile_id &&
    s.chat_app_id == s.binding.app_id && s.conversation_url == s.binding.chat_url &&
    SameProjectRoot(s.project_root, s.binding.project_root);
}

// Standard SHA-256 (FIPS 180-4). Only hashes the canonical ASCII tool-name list;
// it is content-integrity evidence, not a signature or device attestation.
uint32_t Rotate(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
std::string Sha256(const std::string& input) {
  static constexpr uint32_t k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
  std::array<uint32_t, 8> h = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  std::vector<unsigned char> bytes(input.begin(), input.end());
  const uint64_t bits = static_cast<uint64_t>(bytes.size()) * 8;
  bytes.push_back(0x80); while (bytes.size() % 64 != 56) bytes.push_back(0);
  for (int i = 7; i >= 0; --i) bytes.push_back(static_cast<unsigned char>(bits >> (i * 8)));
  for (size_t offset = 0; offset < bytes.size(); offset += 64) {
    uint32_t w[64]{};
    for (unsigned i = 0; i < 16; ++i) for (unsigned j = 0; j < 4; ++j) w[i] = (w[i] << 8) | bytes[offset + i * 4 + j];
    for (unsigned i = 16; i < 64; ++i) {
      const uint32_t s0 = Rotate(w[i-15],7) ^ Rotate(w[i-15],18) ^ (w[i-15] >> 3);
      const uint32_t s1 = Rotate(w[i-2],17) ^ Rotate(w[i-2],19) ^ (w[i-2] >> 10);
      w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    auto v = h;
    for (unsigned i = 0; i < 64; ++i) {
      const uint32_t s1 = Rotate(v[4],6) ^ Rotate(v[4],11) ^ Rotate(v[4],25);
      const uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
      const uint32_t t1 = v[7] + s1 + ch + k[i] + w[i];
      const uint32_t s0 = Rotate(v[0],2) ^ Rotate(v[0],13) ^ Rotate(v[0],22);
      const uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
      const uint32_t t2 = s0 + maj;
      for (unsigned j = 7; j > 0; --j) v[j] = v[j-1];
      v[4] += t1; v[0] = t1 + t2;
    }
    for (unsigned i = 0; i < 8; ++i) h[i] += v[i];
  }
  const char* digits = "0123456789abcdef"; std::string result;
  for (uint32_t word : h) for (int i = 7; i >= 0; --i) result += digits[(word >> (i * 4)) & 15];
  return result;
}

std::string Join(const std::vector<std::string>& names) {
  std::string out;
  for (const auto& name : names) { if (!out.empty()) out += ", "; out += name; }
  return out.empty() ? "UNKNOWN / none observed" : out;
}

#if defined(_WIN32)
const char* PlatformPrivateFileGuardUnavailable() {
  return "WINDOWS_PRIVATE_FILE_GUARD_UNAVAILABLE";
}
#endif

int OpenPrivateDirectory(const std::string& root, std::string& error) {
#if defined(__linux__)
  if (root.empty() || root.size() > 4096 || root.front() != '/' || root == "/" || root.back() == '/') { error = "PROFILE_ROOT_INVALID"; return -1; }
  int directory = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (directory < 0) { error = "PROFILE_ROOT_UNAVAILABLE"; return -1; }
  size_t start = 1;
  while (start < root.size()) {
    const size_t end = root.find('/', start);
    const std::string part = root.substr(start, end == std::string::npos ? end : end - start);
    if (part.empty() || part == "." || part == "..") { ::close(directory); error = "PROFILE_ROOT_INVALID"; return -1; }
    const int next = ::openat(directory, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    ::close(directory); directory = next;
    if (directory < 0) { error = "PROFILE_ROOT_UNAVAILABLE"; return -1; }
    struct stat parent{};
    if (::fstat(directory, &parent) != 0 ||
        ((parent.st_mode & 0022) && !(parent.st_uid == 0 && (parent.st_mode & S_ISVTX)))) {
      ::close(directory); error = "PROFILE_ROOT_UNSAFE"; return -1;
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  struct stat root_stat{};
  if (::fstat(directory, &root_stat) != 0 || root_stat.st_uid != ::geteuid() || (root_stat.st_mode & 0077)) {
    ::close(directory); error = "PROFILE_ROOT_UNSAFE"; return -1;
  }
  error.clear(); return directory;
#else
  (void)root; error = PlatformPrivateFileGuardUnavailable(); return -1;
#endif
}

std::string ReadPrivateAt(int directory, const char* name, size_t cap, std::string& error) {
#if defined(__linux__)
  const int file = ::openat(directory, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
  if (file < 0) { error = errno == ENOENT ? "FILE_MISSING" : "FILE_UNAVAILABLE"; return {}; }
  struct stat before{}, after{}, path_after{};
  if (::fstat(file, &before) != 0 || !S_ISREG(before.st_mode) || before.st_uid != ::geteuid() ||
      (before.st_mode & 0077) || before.st_nlink != 1 || before.st_size <= 0 ||
      static_cast<uint64_t>(before.st_size) > cap) {
    ::close(file); error = "FILE_UNSAFE_OR_OVERSIZE"; return {};
  }
  std::string bytes; bytes.reserve(static_cast<size_t>(before.st_size));
  char buffer[4096]; bool ok = true;
  while (bytes.size() <= cap) {
    const ssize_t count = ::read(file, buffer, sizeof(buffer));
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) { ok = false; break; }
    if (!count) break;
    bytes.append(buffer, static_cast<size_t>(count));
  }
  ok = ok && bytes.size() == static_cast<size_t>(before.st_size) && bytes.size() <= cap &&
    ::fstat(file, &after) == 0 && ::fstatat(directory, name, &path_after, AT_SYMLINK_NOFOLLOW) == 0 &&
    before.st_dev == after.st_dev && before.st_ino == after.st_ino &&
    before.st_size == after.st_size && before.st_mode == after.st_mode && before.st_uid == after.st_uid &&
    before.st_nlink == after.st_nlink && before.st_mtim.tv_sec == after.st_mtim.tv_sec &&
    before.st_mtim.tv_nsec == after.st_mtim.tv_nsec && before.st_ctim.tv_sec == after.st_ctim.tv_sec &&
    before.st_ctim.tv_nsec == after.st_ctim.tv_nsec && after.st_dev == path_after.st_dev && after.st_ino == path_after.st_ino;
  ::close(file);
  if (!ok) { error = "FILE_CHANGED_DURING_READ"; return {}; }
  error.clear(); return bytes;
#else
  (void)directory; (void)name; (void)cap;
  error = PlatformPrivateFileGuardUnavailable(); return {};
#endif
}

#if defined(__linux__)
bool DirectoryUnchanged(const std::string& root, int directory, const struct stat& before, std::string& error) {
  struct stat after{}, path_after{};
  const int current = OpenPrivateDirectory(root, error);
  if (current < 0) return false;
  const bool same = ::fstat(directory, &after) == 0 && ::fstat(current, &path_after) == 0 &&
    before.st_dev == after.st_dev && before.st_ino == after.st_ino &&
    before.st_mode == after.st_mode && before.st_uid == after.st_uid &&
    before.st_mtim.tv_sec == after.st_mtim.tv_sec && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec &&
    before.st_ctim.tv_sec == after.st_ctim.tv_sec && before.st_ctim.tv_nsec == after.st_ctim.tv_nsec &&
    after.st_dev == path_after.st_dev && after.st_ino == path_after.st_ino;
  ::close(current);
  if (!same) error = "PROFILE_DIRECTORY_CHANGED_DURING_READ";
  return same;
}
#endif

std::string ReadPrivateFile(const std::string& root, const char* name, size_t cap, std::string& error) {
#if defined(__linux__)
  const int directory = OpenPrivateDirectory(root, error);
  if (directory < 0) return {};
  struct stat before{};
  if (::fstat(directory, &before) != 0) { ::close(directory); error = "PROFILE_ROOT_UNAVAILABLE"; return {}; }
  std::string bytes = ReadPrivateAt(directory, name, cap, error);
  if (error.empty() && !DirectoryUnchanged(root, directory, before, error)) bytes.clear();
  ::close(directory); return bytes;
#elif defined(_WIN32)
  return winprivate::ReadPrivateFile(root, name, cap, error);
#else
  (void)root; (void)name; (void)cap;
  error = PlatformPrivateFileGuardUnavailable(); return {};
#endif
}
}  // namespace

bool IsCanonicalChatUrl(const std::string& url) {
  if (url.size() > 1024) return false;
  const std::string prefix = "https://chatgpt.com/";
  if (url.compare(0, prefix.size(), prefix) != 0) return false;
  std::vector<std::string> parts; size_t start = prefix.size();
  while (start <= url.size()) {
    const size_t end = url.find('/', start);
    parts.push_back(url.substr(start, end == std::string::npos ? end : end - start));
    if (end == std::string::npos) break;
    start = end + 1;
  }
  const auto id = [](const std::string& s) { return !s.empty() && s.size() <= 128 &&
    std::all_of(s.begin(), s.end(), [](char c) { return Alpha(c) || Digit(c) || c == '-' || c == '_'; }); };
  return (parts.size() == 2 && parts[0] == "c" && id(parts[1])) ||
         (parts.size() == 4 && parts[0] == "g" && id(parts[1]) && parts[2] == "c" && id(parts[3]));
}

bool IsCanonicalProjectRoot(const std::string& root) {
  if (root.empty() || root.size() > 16384 || !Utf8(root)) return false;
  const size_t units = std::accumulate(root.begin(), root.end(), size_t{0}, [](size_t count, unsigned char c) {
    return count + ((c & 0xC0) != 0x80 ? (c >= 0xF0 ? 2 : 1) : 0);
  });
  if (units > 4096) return false;
  const bool windows = root.size() > 3 && Alpha(root[0]) && root[1] == ':' && root[2] == '\\';
  const char separator = windows ? '\\' : '/';
  const size_t start = windows ? 3 : 1;
  if ((!windows && (root[0] != '/' || root.size() == 1)) || root.back() == separator) return false;
  if (windows && root.find('/') != std::string::npos) return false;
  if (!windows && root.find('\\') != std::string::npos) return false;
  size_t position = start;
  while (position < root.size()) {
    const size_t end = root.find(separator, position);
    const std::string part = root.substr(position, end == std::string::npos ? end : end - position);
    if (part.empty() || part == "." || part == "..") return false;
    if (std::any_of(part.begin(), part.end(), [windows](unsigned char c) {
      return c < 32 || c == 127 || (windows && (c == ':' || c == '"' || c == '<' || c == '>' || c == '|' || c == '?' || c == '*'));
    })) return false;
    if (windows && (part.back() == ' ' || part.back() == '.')) return false;
    if (end == std::string::npos) break;
    position = end + 1;
  }
  return true;
}

bool SameProjectRoot(const std::string& a, const std::string& b) {
  if (!IsCanonicalProjectRoot(a) || !IsCanonicalProjectRoot(b) || a.size() != b.size()) return false;
  if (a[0] == '/' || b[0] == '/') return a == b;
  for (size_t i = 0; i < a.size(); ++i) {
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 'a' - 'A') : c; };
    if (lower(a[i]) != lower(b[i])) return false;
  }
  return true;
}

std::string CatalogSha256(std::vector<std::string> names) {
  std::sort(names.begin(), names.end()); std::string json = "[";
  for (size_t i = 0; i < names.size(); ++i) { if (i) json += ','; json += '"' + names[i] + '"'; }
  return Sha256(json + "]");
}

Observation ParseObservation(const std::string& json) {
  Observation s; Json root;
  if (!Parser(json).Parse(root)) { s.error = "SNAPSHOT_JSON_INVALID"; return s; }
  s.error = "SNAPSHOT_SCHEMA_INVALID";
  if (!Keys(root, {"schema", "kind", "observedAtEpochMs", "expiresAtEpochMs", "producer", "identity", "backend", "currentChat", "project", "binding", "reconciliation"}) ||
      At(root, "schema").type != Json::Number || At(root, "schema").number != 1 ||
      At(root, "kind").type != Json::String || At(root, "kind").string != "COMMANDER_COMPANION_OBSERVATION" ||
      !Integer(At(root, "observedAtEpochMs"), s.observed_at, kMaxSafeInteger, false) ||
      !Integer(At(root, "expiresAtEpochMs"), s.expires_at, kMaxSafeInteger, false) ||
      s.expires_at < s.observed_at || s.expires_at - s.observed_at > kMaximumLifetimeMs) return s;
  const auto& producer = At(root, "producer");
  if (!Keys(producer, {"id", "version"}) || At(producer, "id").type != Json::String ||
      At(producer, "id").string != "remote-commander-companion" || At(producer, "version").type != Json::String || At(producer, "version").string != "1") return s;
  const auto& identity = At(root, "identity");
  if (!Keys(identity, {"appId", "profile", "deviceName", "version", "commit", "configSha256", "routeGeneration"}) ||
      !AsText(At(identity, "appId"), s.app_id, 128) || !Token(s.app_id) ||
      !AsText(At(identity, "profile"), s.profile, 128) || !Token(s.profile) ||
      !DeviceName(At(identity, "deviceName"), s.device_name) ||
      !AsText(At(identity, "version"), s.version, 128) || !Token(s.version) ||
      !AsText(At(identity, "commit"), s.commit, 64) || (!Hex(s.commit, 40) && !Hex(s.commit, 64)) ||
      !AsText(At(identity, "configSha256"), s.config_sha256, 64) || !Hex(s.config_sha256, 64) ||
      !Integer(At(identity, "routeGeneration"), s.route_generation)) return s;
  const auto& backend = At(root, "backend");
  if (!Keys(backend, {"state", "toolNames", "toolCatalogSha256"}) || !AsText(At(backend, "state"), s.backend_state, 128, false) ||
      !OneOf(s.backend_state, {"CONFIRMED", "UNAVAILABLE", "MISMATCH"}) || !Names(At(backend, "toolNames"), s.backend_tools, 256) ||
      !AsText(At(backend, "toolCatalogSha256"), s.tool_catalog_sha256, 64) || !Hex(s.tool_catalog_sha256, 64) ||
      (s.backend_state == "CONFIRMED" && s.tool_catalog_sha256 != CatalogSha256(s.backend_tools))) return s;
  if (s.backend_state != "CONFIRMED" && (!s.backend_tools.empty() || !s.tool_catalog_sha256.empty())) return s;
  const auto& chat = At(root, "currentChat");
  if (!Keys(chat, {"state", "conversationUrl", "appId", "toolNames", "skills", "plugins"}) ||
      !AsText(At(chat, "state"), s.chat_state, 128, false) || !OneOf(s.chat_state, {"UNKNOWN", "CONFIRMED"}) ||
      !AsText(At(chat, "conversationUrl"), s.conversation_url, 1024) || (!s.conversation_url.empty() && !IsCanonicalChatUrl(s.conversation_url)) ||
      !AsText(At(chat, "appId"), s.chat_app_id, 128) || !Token(s.chat_app_id) ||
      !Names(At(chat, "toolNames"), s.chat_tools, 256) || !Names(At(chat, "skills"), s.skills, 64, true) || !Names(At(chat, "plugins"), s.plugins, 64, true)) return s;
  if (s.chat_state == "UNKNOWN" && (!s.conversation_url.empty() || !s.chat_app_id.empty() || !s.chat_tools.empty() || !s.skills.empty() || !s.plugins.empty())) return s;
  if (s.chat_state == "CONFIRMED" && (s.conversation_url.empty() || s.chat_app_id.empty())) return s;
  const auto& project = At(root, "project");
  if (!Keys(project, {"state", "projectId", "root", "revision", "nextAction", "evidenceSha256"}) ||
      !AsText(At(project, "state"), s.project_state, 128, false) || !OneOf(s.project_state, {"UNKNOWN", "BOUND", "STOP"}) ||
      !AsText(At(project, "projectId"), s.project_id, 64) || !Identifier(s.project_id) ||
      !AsText(At(project, "root"), s.project_root, 16384) || (!s.project_root.empty() && !IsCanonicalProjectRoot(s.project_root)) ||
      !Integer(At(project, "revision"), s.revision, 10000) || !AsText(At(project, "nextAction"), s.next_action, 64) ||
      (!s.next_action.empty() && !OneOf(s.next_action, {"REVIEW_RECORDED_CHECKPOINT", "INSPECT_PROJECT_STATE", "RECONCILE_UNCERTAIN", "REVIEW_SESSION_BINDING", "AWAIT_AUTHORITATIVE_CHAT_OBSERVATION"})) ||
      !AsText(At(project, "evidenceSha256"), s.evidence_sha256, 64) || !Hex(s.evidence_sha256, 64)) return s;
  if (s.project_state == "BOUND" && (s.project_id.empty() || s.project_root.empty() || !s.revision)) return s;
  if (!BindingFields(At(root, "binding"), s.binding)) return s;
  const auto& reconciliation = At(root, "reconciliation");
  if (!Keys(reconciliation, {"state", "reasonCodes", "actionAllowed"}) ||
      !AsText(At(reconciliation, "state"), s.reconciliation_state, 128, false) || !OneOf(s.reconciliation_state, {"BOUND", "STOP", "UNPROVEN"}) ||
      !Names(At(reconciliation, "reasonCodes"), s.reasons, 16) || At(reconciliation, "actionAllowed").type != Json::Boolean || At(reconciliation, "actionAllowed").boolean) return s;
  if (std::any_of(s.reasons.begin(), s.reasons.end(), [](const std::string& reason) {
    return !OneOf(reason, {"BINDING_MISSING", "APP_ID_MISSING", "ACCOUNT_ID_MISSING", "PROFILE_ID_MISSING", "WORKFLOW_ID_MISSING", "PROJECT_ROOT_MISSING", "CHAT_URL_MISSING", "APP_ID_MISMATCH", "ACCOUNT_ID_MISMATCH", "PROFILE_ID_MISMATCH", "WORKFLOW_ID_MISMATCH", "PROJECT_ROOT_MISMATCH", "CHAT_URL_MISMATCH", "CHAT_OBSERVATION_MISSING", "CHAT_OBSERVATION_STALE", "UNCERTAIN_OPERATION", "BACKEND_UNAVAILABLE", "BACKEND_MISMATCH", "WORKFLOW_UNAVAILABLE", "MACHINE_MISMATCH", "CONFIG_MISMATCH"});
  })) return s;
  if ((s.reconciliation_state == "BOUND" && (!InternallyBound(s) || !s.reasons.empty())) ||
      (s.project_state == "BOUND" && s.reconciliation_state != "BOUND") ||
      (s.reconciliation_state == "STOP" && s.reasons.empty()) ||
      ((s.project_state == "STOP") != (s.reconciliation_state == "STOP"))) return s;
  const auto differs = [](const std::string& a, const std::string& b) { return !a.empty() && !b.empty() && a != b; };
  const bool conflicts = differs(s.app_id, s.binding.app_id) || differs(s.profile, s.binding.profile_id) ||
    differs(s.project_id, s.binding.workflow_id) ||
    (!s.project_root.empty() && !s.binding.project_root.empty() && !SameProjectRoot(s.project_root, s.binding.project_root)) ||
    (s.chat_state == "CONFIRMED" && (differs(s.chat_app_id, s.binding.app_id) || differs(s.conversation_url, s.binding.chat_url)));
  if (conflicts && s.reconciliation_state != "STOP") return s;
  s.valid = true; s.error.clear(); return s;
}

Monitor ParseMonitor(const std::string& json) {
  Monitor m; Json root;
  if (json.size() > kMaximumMonitorBytes || !Parser(json).Parse(root)) { m.error = "MONITOR_JSON_INVALID"; return m; }
  m.error = "MONITOR_SCHEMA_INVALID";
  if (!Keys(root, {"schema","kind","observedAtEpochMs","expiresAtEpochMs","identity","gui","browser","workflows","extensions","operations"}) ||
      At(root,"schema").type != Json::Number || At(root,"schema").number != 1 ||
      At(root,"kind").type != Json::String || At(root,"kind").string != "COMMANDER_LIVE_MONITOR" ||
      !Integer(At(root,"observedAtEpochMs"),m.observed_at,kMaxSafeInteger,false) ||
      !Integer(At(root,"expiresAtEpochMs"),m.expires_at,kMaxSafeInteger,false) ||
      m.expires_at < m.observed_at || m.expires_at-m.observed_at > 30000) return m;
  const auto& identity=At(root,"identity");
  if (!Keys(identity,{"deviceName","profile","version","configSha256","port","platform"}) ||
      !DeviceName(At(identity,"deviceName"),m.device_name) ||
      !AsText(At(identity,"profile"),m.profile,128,false) || !Token(m.profile) ||
      !AsText(At(identity,"version"),m.version,64,false) || !Token(m.version) ||
      !AsText(At(identity,"configSha256"),m.config_sha256,64,false) || !Hex(m.config_sha256,64) ||
      !Integer(At(identity,"port"),m.port,65535,false) ||
      !AsText(At(identity,"platform"),m.platform,16,false) || !OneOf(m.platform,{"win32","linux"})) return m;
  const auto& gui=At(root,"gui");
  if (!Keys(gui,{"enabled","available","uncertain","backend","sessionType","reason","capabilities"}) ||
      At(gui,"enabled").type!=Json::Boolean || At(gui,"available").type!=Json::Boolean ||
      At(gui,"uncertain").type!=Json::Boolean ||
      !AsText(At(gui,"backend"),m.gui_backend,128) || !AsText(At(gui,"sessionType"),m.gui_session_type,32) ||
      !AsText(At(gui,"reason"),m.gui_reason,160)) return m;
  m.gui_enabled=At(gui,"enabled").boolean; m.gui_available=At(gui,"available").boolean; m.gui_uncertain=At(gui,"uncertain").boolean;
  const auto& caps=At(gui,"capabilities");
  if (!Keys(caps,{"screenshot","mouse","keyboard","focus"}) ||
      At(caps,"screenshot").type!=Json::Boolean || At(caps,"mouse").type!=Json::Boolean ||
      At(caps,"keyboard").type!=Json::Boolean || At(caps,"focus").type!=Json::Boolean) return m;
  m.gui_screenshot=At(caps,"screenshot").boolean; m.gui_mouse=At(caps,"mouse").boolean;
  m.gui_keyboard=At(caps,"keyboard").boolean; m.gui_focus=At(caps,"focus").boolean;
  const auto& browser=At(root,"browser");
  if (!Keys(browser,{"enabled","available","uncertain","backend","reason"}) ||
      At(browser,"enabled").type!=Json::Boolean || At(browser,"available").type!=Json::Boolean ||
      At(browser,"uncertain").type!=Json::Boolean ||
      !AsText(At(browser,"backend"),m.browser_backend,128) || !AsText(At(browser,"reason"),m.browser_reason,160)) return m;
  m.browser_enabled=At(browser,"enabled").boolean; m.browser_available=At(browser,"available").boolean; m.browser_uncertain=At(browser,"uncertain").boolean;
  const auto& workflows=At(root,"workflows");
  if (!Keys(workflows,{"enabled","engineEnabled","runCount"}) ||
      At(workflows,"enabled").type!=Json::Boolean || At(workflows,"engineEnabled").type!=Json::Boolean ||
      !NonnegativeInteger(At(workflows,"runCount"),m.workflow_runs,10000)) return m;
  m.workflows_enabled=At(workflows,"enabled").boolean; m.workflow_engine_enabled=At(workflows,"engineEnabled").boolean;
  const auto& extensions=At(root,"extensions");
  if (!Keys(extensions,{"count","items"}) || !NonnegativeInteger(At(extensions,"count"),m.extension_count,32) ||
      At(extensions,"items").type!=Json::Array || At(extensions,"items").array.size()!=m.extension_count) return m;
  for (const auto& item:At(extensions,"items").array) {
    if (!Keys(item,{"id","version"})) return m;
    std::string id,version;
    if (!AsText(At(item,"id"),id,64,false) || !Identifier(id) ||
        !AsText(At(item,"version"),version,64,false) || !Token(version)) return m;
    m.extensions.push_back(id+"@"+version);
  }
  const auto& operations=At(root,"operations");
  if (!Keys(operations,{"active","lockedKeys"}) ||
      !NonnegativeInteger(At(operations,"active"),m.active_operations,10000) ||
      !NonnegativeInteger(At(operations,"lockedKeys"),m.locked_keys,10000)) return m;
  m.valid=true; m.error.clear(); return m;
}

Automation ParseAutomation(const std::string& json) {
  Automation a; Json root;
  if (json.size() > 4096 || !Parser(json).Parse(root)) { a.error = "AUTOMATION_JSON_INVALID"; return a; }
  a.error = "AUTOMATION_SCHEMA_INVALID";
  if (!Keys(root, {"schema","kind","observedAtEpochMs","expiresAtEpochMs","configSha256","automation"}) ||
      At(root,"schema").type != Json::Number || At(root,"schema").number != 1 ||
      At(root,"kind").type != Json::String || At(root,"kind").string != "COMMANDER_AUTOMATION_MONITOR" ||
      !Integer(At(root,"observedAtEpochMs"),a.observed_at,kMaxSafeInteger,false) ||
      !Integer(At(root,"expiresAtEpochMs"),a.expires_at,kMaxSafeInteger,false) ||
      a.expires_at < a.observed_at || a.expires_at - a.observed_at > 30000 ||
      !AsText(At(root,"configSha256"),a.config_sha256,64,false) || !Hex(a.config_sha256,64)) return a;
  const auto& state = At(root,"automation");
  if (!Keys(state,{"runnerConfigured","automaticExecution","localModel","workCredits","creditUsage"}) ||
      At(state,"runnerConfigured").type != Json::Boolean ||
      At(state,"automaticExecution").type != Json::Boolean ||
      !AsText(At(state,"localModel"),a.local_model,48,false) ||
      !OneOf(a.local_model,{"UNVERIFIED","NOT_ATTACHED","PROPOSAL_ONLY"}) ||
      !AsText(At(state,"workCredits"),a.work_credits,48,false) ||
      !OneOf(a.work_credits,{"DENY_IN_CORE","UNVERIFIED"}) ||
      !AsText(At(state,"creditUsage"),a.credit_usage,48,false) ||
      a.credit_usage != "UNVERIFIED") return a;
  a.runner_configured=At(state,"runnerConfigured").boolean;
  a.automatic_execution=At(state,"automaticExecution").boolean;
  if (a.automatic_execution && !a.runner_configured) return a;
  a.valid=true; a.error.clear(); return a;
}

NativeBinding ParseNativeBinding(const std::string& json) {
  NativeBinding out; Json root;
  out.error = "NATIVE_BINDING_INVALID";
  if (json.size() > kMaximumNativeBindingBytes || !Parser(json).Parse(root) || !BindingFields(root, out.binding) || !Complete(out.binding)) return out;
  out.valid = true; out.error.clear(); return out;
}

bool IsFresh(const Observation& s, uint64_t now_ms) {
  return s.valid && now_ms >= s.observed_at && now_ms < s.expires_at;
}

bool IsFresh(const Monitor& m, uint64_t now_ms) {
  return m.valid && now_ms >= m.observed_at && now_ms < m.expires_at;
}

bool IsFresh(const Automation& a, uint64_t now_ms) {
  return a.valid && now_ms >= a.observed_at && now_ms < a.expires_at;
}

bool ImmutableSnapshotName(const std::string& name, uint64_t& observed_at) {
  const std::string prefix = "commander-companion-";
  constexpr size_t kStampLength = 16, kUuidLength = 36;
  if (name.size() != prefix.size() + kStampLength + 1 + kUuidLength + 5 ||
      name.compare(0, prefix.size(), prefix) != 0 || name.compare(name.size() - 5, 5, ".json") != 0) return false;
  observed_at = 0;
  for (size_t i = prefix.size(); i < prefix.size() + kStampLength; ++i) {
    if (!Digit(name[i])) return false;
    const unsigned digit = name[i] - '0';
    if (observed_at > (kMaxSafeInteger - digit) / 10) return false;
    observed_at = observed_at * 10 + digit;
  }
  if (!observed_at || name[prefix.size() + kStampLength] != '-') return false;
  const size_t start = prefix.size() + kStampLength + 1;
  for (size_t i = 0; i < kUuidLength; ++i) {
    const char c = name[start + i];
    if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') return false; }
    else if (!(Digit(c) || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

std::vector<DisplayRow> DescribeMonitor(const Monitor& m, uint64_t now_ms) {
  std::vector<DisplayRow> rows;
  if (!m.valid || !IsFresh(m,now_ms)) {
    const std::string reason=!m.valid?m.error:(now_ms<m.observed_at?"MONITOR_FROM_FUTURE":"MONITOR_EXPIRED");
    rows.push_back({m.blocked?"Commander Core — 🔴 BLOCKED":"Commander Core — ⚪ DISCONNECTED",reason});
    return rows;
  }
  rows.push_back({"Commander Core — 🟢 CONNECTED (monitor only)","v"+m.version+" / "+m.device_name+" / "+m.profile+" / "+m.platform+":"+std::to_string(m.port)});
  const std::string gui=!m.gui_enabled?"DISABLED":m.gui_uncertain?"UNCERTAIN":m.gui_available?"READY":"UNAVAILABLE";
  rows.push_back({"GUI control: "+gui,
    "backend "+(m.gui_backend.empty()?"UNKNOWN":m.gui_backend)+" / "+(m.gui_session_type.empty()?"UNKNOWN":m.gui_session_type)+
    " / screenshot="+(m.gui_screenshot?"yes":"no")+" mouse="+(m.gui_mouse?"yes":"no")+" keyboard="+(m.gui_keyboard?"yes":"no")+" focus="+(m.gui_focus?"yes":"no")+
    (m.gui_reason.empty()?"":" / "+m.gui_reason)});
  const std::string browser=!m.browser_enabled?"DISABLED":m.browser_uncertain?"UNCERTAIN":m.browser_available?"READY":"UNAVAILABLE";
  rows.push_back({"Background browser: "+browser,(m.browser_backend.empty()?"UNKNOWN":m.browser_backend)+(m.browser_reason.empty()?"":" / "+m.browser_reason)});
  rows.push_back({"Workflows: "+std::to_string(m.workflow_runs)+(m.workflow_engine_enabled?" / engine ON":" / engine OFF"),
    m.workflows_enabled?"Durable workflow monitor enabled.":"Durable workflows disabled."});
  rows.push_back({"Extensions: "+std::to_string(m.extension_count),Join(m.extensions)});
  rows.push_back({"Operations: "+std::to_string(m.active_operations)+" active / "+std::to_string(m.locked_keys)+" locks",
    "Live local Commander counters; no command is executed by this panel."});
  rows.push_back({"Monitor TTL: "+std::to_string((m.expires_at-now_ms)/1000)+"s","Private local file refreshed by Commander Core; Browser only reads it."});
  return rows;
}

std::vector<DisplayRow> DescribeAutomation(const Automation& a, const Monitor& m, uint64_t now_ms) {
  std::vector<DisplayRow> rows;
  if (!IsFresh(m,now_ms) || !IsFresh(a,now_ms) ||
      m.config_sha256 != a.config_sha256 ||
      a.observed_at > m.observed_at + 10000 ||
      m.observed_at > a.observed_at + 10000) {
    rows.push_back({"Automation: UNVERIFIED",
      a.valid ? "Stale or mismatched private automation evidence" : a.error});
    rows.push_back({"Work credits: UNVERIFIED","No account-level billing receipt"});
    return rows;
  }
  const std::string state=a.automatic_execution?"AUTO_READ_ONLY":
    a.runner_configured?"MANUAL_ONLY":"NOT_CONFIGURED";
  rows.push_back({"Automation: "+state,"Local model: "+a.local_model+
    "; private status only, not independent model acceptance"});
  rows.push_back({"Work credits: "+a.work_credits,
    "Core guard policy, account usage "+a.credit_usage+"; Browser cannot execute anything"});
  return rows;
}

std::vector<DisplayRow> Describe(const Observation& s, const NativeBinding& expected, const std::string& active_url, uint64_t now_ms) {
  std::vector<DisplayRow> rows;
  rows.push_back({"Chat binding — فقط مشاهده", "Independent chat/project binding evidence; action authority remains fail-closed."});
  if (!s.valid || !IsFresh(s, now_ms)) {
    const std::string reason = !s.valid ? s.error : now_ms < s.observed_at ? "SNAPSHOT_FROM_FUTURE" : "SNAPSHOT_EXPIRED";
    rows.push_back({s.blocked ? "🔴 وضعیت: STOP" : "⚪ وضعیت: UNPROVEN", reason});
    rows.push_back({"سیستم: UNKNOWN", "Refresh an owner-private snapshot in this CEF profile."});
    rows.push_back({"ابزار همین چت: UNKNOWN", "No active-chat exposure has been established."});
    rows.push_back({"پروژه / نشست: UNPROVEN", "No action, continuation or background execution is claimed."});
    return rows;
  }
  const bool conflict = expected.valid && NativeConflict(expected.binding, s);
  const bool chat_matches = IsCanonicalChatUrl(active_url) && active_url == s.binding.chat_url;
  const bool chat_conflict = expected.valid && IsCanonicalChatUrl(active_url) && active_url != expected.binding.chat_url;
  const bool bound = expected.valid && SameBinding(expected.binding, s.binding) && !conflict && chat_matches && s.reconciliation_state == "BOUND" && InternallyBound(s);
  const bool stop = conflict || s.reconciliation_state == "STOP" || chat_conflict;
  rows.push_back({stop ? "🔴 نشست: STOP" : bound ? "🟢 نشست: BOUND (مشاهده)" : "⚪ نشست: UNPROVEN",
                 conflict ? "NATIVE_BINDING_MISMATCH" : !expected.valid ? expected.error : chat_conflict ? "ACTIVE_CHAT_MISMATCH" : !chat_matches ? "ACTIVE_CHAT_UNPROVEN" : !SameBinding(expected.binding, s.binding) ? "NATIVE_BINDING_UNPROVEN" : Join(s.reasons)});
  rows.push_back({"کاتالوگ سیستم: " + (s.backend_state == "CONFIRMED" ? std::string("OBSERVED") : s.backend_state) + " / " + std::to_string(s.backend_tools.size()),
                 "Exporter state " + s.backend_state + "; not a live current-chat exposure claim. Tools: " + Join(s.backend_tools)});
  rows.push_back({"شناسه برنامه: " + (s.app_id.empty() ? "UNKNOWN" : s.app_id), "AppID is canonical; displayed device name is only an alias."});
  rows.push_back({"پروفایل: " + (s.profile.empty() ? "UNKNOWN" : s.profile), "Version " + s.version + " / device alias " + s.device_name + " / commit " + s.commit});
  rows.push_back({bound ? "ابزار همین چت: " + std::to_string(s.chat_tools.size()) : "ابزار همین چت: UNKNOWN", bound ? Join(s.chat_tools) : "System catalog is not current-chat tool exposure."});
  rows.push_back({bound ? "Skills / Plugins: " + std::to_string(s.skills.size()) + " / " + std::to_string(s.plugins.size()) : "Skills / Plugins: UNKNOWN", bound ? "Skills: " + Join(s.skills) + " | Plugins: " + Join(s.plugins) : "Needs authoritative host-adapter evidence and exact native chat binding."});
  rows.push_back({bound ? "پروژه: " + s.project_id + " / R" + std::to_string(s.revision) : "پروژه: " + (stop ? std::string("STOP") : std::string("UNPROVEN")), bound ? s.project_root + " | " + s.next_action + " | evidence " + (s.evidence_sha256.empty() ? "UNKNOWN" : s.evidence_sha256) : "Recorded project context is not enabled for this native session."});
  rows.push_back({"اعتبار باقی‌مانده: " + std::to_string((s.expires_at - now_ms) / 1000) + " ثانیه", "Snapshot expires automatically. Refresh only reads local files; it does not call Commander or a model."});
  rows.push_back({"اجرا / پس‌زمینه: UNPROVEN", "No commands, MCP calls, continuation, credentials or browser-origin bridge."});
  return rows;
}

Observation ReadObservation(const std::string& root) {
  Observation out;
#if defined(__linux__)
  std::string error;
  const int directory = OpenPrivateDirectory(root, error);
  if (directory < 0) { out.error = error; out.blocked = true; return out; }
  struct stat before{};
  if (::fstat(directory, &before) != 0) { ::close(directory); out.error = "PROFILE_ROOT_UNAVAILABLE"; out.blocked = true; return out; }
  const int duplicate = ::fcntl(directory, F_DUPFD_CLOEXEC, 0);
  DIR* listing = duplicate < 0 ? nullptr : ::fdopendir(duplicate);
  if (!listing) {
    if (duplicate >= 0) ::close(duplicate);
    ::close(directory); out.error = "SNAPSHOT_DIRECTORY_UNAVAILABLE"; out.blocked = true; return out;
  }
  size_t scanned = 0, recognized = 0;
  std::string latest;
  uint64_t latest_time = 0;
  for (;;) {
    errno = 0;
    struct dirent* entry = ::readdir(listing);
    if (!entry) { if (errno) error = "SNAPSHOT_DIRECTORY_READ_FAILED"; break; }
    const std::string name(entry->d_name);
    if (name == "." || name == "..") continue;
    if (++scanned > 4096) { error = "SNAPSHOT_DIRECTORY_ENTRY_LIMIT"; break; }
    uint64_t observed_at = 0;
    if (!ImmutableSnapshotName(name, observed_at)) continue;
    if (++recognized > 32) { error = "SNAPSHOT_CANDIDATE_LIMIT"; break; }
    if (name > latest) { latest = name; latest_time = observed_at; }
  }
  ::closedir(listing);
  std::string bytes;
  if (error.empty()) bytes = ReadPrivateAt(directory, latest.empty() ? "commander-companion.json" : latest.c_str(), kMaximumSnapshotBytes, error);
  if (error.empty() && !DirectoryUnchanged(root, directory, before, error)) bytes.clear();
  ::close(directory);
  if (!error.empty()) {
    out.error = error == "FILE_MISSING" && latest.empty() ? "SNAPSHOT_MISSING" : error;
    out.blocked = out.error != "SNAPSHOT_MISSING"; return out;
  }
  out = ParseObservation(bytes);
  if (!out.valid) out.blocked = true;
  if (out.valid && !latest.empty() && out.observed_at != latest_time) {
    out.valid = false; out.blocked = true; out.error = "SNAPSHOT_FILENAME_TIMESTAMP_MISMATCH";
  }
  return out;
#elif defined(_WIN32)
  auto snapshot = winprivate::ReadLatestSnapshot(root, kMaximumSnapshotBytes);
  if (!snapshot.error.empty()) {
    out.error = snapshot.error == "FILE_MISSING" ? "SNAPSHOT_MISSING" : snapshot.error;
    out.blocked = out.error != "SNAPSHOT_MISSING";
    return out;
  }
  out = ParseObservation(snapshot.bytes);
  if (!out.valid) out.blocked = true;
  if (out.valid && !snapshot.selected_name.empty() &&
      out.observed_at != snapshot.selected_epoch_ms) {
    out.valid = false; out.blocked = true;
    out.error = "SNAPSHOT_FILENAME_TIMESTAMP_MISMATCH";
  }
  return out;
#else
  (void)root;
  out.error = PlatformPrivateFileGuardUnavailable(); out.blocked = true; return out;
#endif
}
Monitor ReadMonitor(const std::string& root) {
  std::string error;
  const std::string bytes=ReadPrivateFile(root,"commander-monitor.json",kMaximumMonitorBytes,error);
  if (!error.empty()) {
    Monitor out; out.error=error=="FILE_MISSING"?"MONITOR_MISSING":error; out.blocked=out.error!="MONITOR_MISSING"; return out;
  }
  Monitor out=ParseMonitor(bytes);
  if (!out.valid) out.blocked=true;
  return out;
}

Automation ReadAutomation(const std::string& root) {
  std::string error;
  const std::string bytes=ReadPrivateFile(root,"commander-automation.json",4096,error);
  if (!error.empty()) {
    Automation a; a.error=error=="FILE_MISSING"?"AUTOMATION_MISSING":error;
    a.blocked=a.error!="AUTOMATION_MISSING"; return a;
  }
  Automation a=ParseAutomation(bytes);
  if (!a.valid) a.blocked=true;
  return a;
}

NativeBinding ReadNativeBinding(const std::string& root) {
  std::string error; const std::string bytes = ReadPrivateFile(root, "commander-binding.json", kMaximumNativeBindingBytes, error);
  if (error.empty()) return ParseNativeBinding(bytes);
  NativeBinding out; out.error = error == "FILE_MISSING" ? "NATIVE_BINDING_MISSING" : error; return out;
}
std::string ProfileRoot() {
  // Companion trust data is intentionally separate from the Chromium profile.
  // CGWA_PROFILE_ROOT remains a compatibility fallback for isolated test profiles.
  if (const char* root = std::getenv("CGWA_COMPANION_ROOT"); root && *root) return root;
  if (const char* root = std::getenv("CGWA_PROFILE_ROOT"); root && *root) return root;
#if defined(_WIN32)
  const char* local = std::getenv("LOCALAPPDATA");
  return local && *local ? std::string(local) + "\\ChatGPTRemoteCommander\\browser-companion" : std::string();
#else
  if (const char* state = std::getenv("XDG_STATE_HOME"); state && *state)
    return std::string(state) + "/chatgpt-remote-commander/browser-companion";
  const char* home = std::getenv("HOME");
  return home && *home ? std::string(home) + "/.local/state/chatgpt-remote-commander/browser-companion" : std::string();
#endif
}
uint64_t NowEpochMs() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count());
}
}  // namespace companion
