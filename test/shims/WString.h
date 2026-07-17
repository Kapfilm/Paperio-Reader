#pragma once

#include <cstddef>
#include <cstring>
#include <string>

// Minimal host-side stand-in for the Arduino framework's WString/String.
// Provides the surface used by repo code compiled in host tests: c_str/length
// plus the startsWith/endsWith used on HalStorage::listFiles() results.
// Owns its bytes (std::string) so shims can return Strings for generated names.
class String {
 public:
  String() = default;
  String(const char* s) : _buf(s ? s : "") {}
  // explicit: an implicit std::string->String conversion makes calls ambiguous
  // where repo code overloads on both types (e.g. hasGifExtension).
  explicit String(std::string s) : _buf(std::move(s)) {}

  const char* c_str() const { return _buf.c_str(); }
  size_t length() const { return _buf.length(); }
  bool startsWith(const char* prefix) const { return _buf.rfind(prefix, 0) == 0; }
  bool endsWith(const char* suffix) const {
    const size_t n = std::strlen(suffix);
    return _buf.size() >= n && _buf.compare(_buf.size() - n, n, suffix) == 0;
  }

 private:
  std::string _buf;
};
