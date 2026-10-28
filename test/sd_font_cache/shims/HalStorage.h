#pragma once
#include <cstddef>
#include <map>
#include <string>
namespace font_test {
inline std::map<std::string, size_t> files;
}
class HalFile {
 public:
  size_t bytes = 0;
  explicit operator bool() const { return bytes != 0; }
  size_t fileSize() const { return bytes; }
  void close() {}
};
class TestStorage {
 public:
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    const auto it = font_test::files.find(path);
    file.bytes = it == font_test::files.end() ? 0 : it->second;
    return file.bytes != 0;
  }
};
inline TestStorage Storage;
