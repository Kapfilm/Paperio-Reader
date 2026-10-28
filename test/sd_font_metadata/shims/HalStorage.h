#pragma once
#include <cstddef>
#include <fstream>
#include <string>
namespace font_metadata_test {
inline bool storageAvailable = true;
}
class HalFile {
  std::ifstream file;

 public:
  HalFile() = default;
  HalFile(HalFile&&) = default;
  HalFile& operator=(HalFile&&) = default;
  bool open(const char* path) {
    file.open(std::string(path) == "fixture.cpfont" ? FONT_FIXTURE : path, std::ios::binary);
    return file.is_open();
  }
  explicit operator bool() const { return file.is_open(); }
  int read(void* out, size_t count) {
    file.read(static_cast<char*>(out), static_cast<std::streamsize>(count));
    return static_cast<int>(file.gcount());
  }
  bool seekSet(size_t offset) {
    file.clear();
    file.seekg(static_cast<std::streamoff>(offset));
    return static_cast<bool>(file);
  }
  void close() { file.close(); }
};
using FsFile = HalFile;
class MetadataStorage {
 public:
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    return font_metadata_test::storageAvailable && file.open(path);
  }
};
inline MetadataStorage Storage;
