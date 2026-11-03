#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
constexpr unsigned O_WRONLY = 1, O_CREAT = 2, O_EXCL = 4;
inline std::filesystem::path dropboxTestRoot;
inline bool dropboxTestWriteFail = false;
inline uint64_t dropboxTestFree = 1024 * 1024;
class HalFile {
 public:
  HalFile() = default;
  explicit HalFile(const std::filesystem::path& path) : stream(path, std::ios::binary) {}
  HalFile(HalFile&&) = default;
  explicit operator bool() const { return stream.is_open(); }
  size_t write(const void* b, size_t n) {
    if (dropboxTestWriteFail) return 0;
    stream.write(static_cast<const char*>(b), n);
    return stream ? n : 0;
  }
  void flush() { stream.flush(); }
  bool close() {
    bool good = static_cast<bool>(stream);
    stream.close();
    return good && !stream.fail();
  }

 private:
  std::ofstream stream;
};
class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage s;
    return s;
  }
  bool ready() const { return true; }
  bool exists(const char* path) { return std::filesystem::exists(resolve(path)); }
  bool ensureDirectoryExists(const char* path) {
    std::filesystem::create_directories(resolve(path));
    return true;
  }
  uint64_t sdFreeBytes() { return dropboxTestFree; }
  HalFile open(const char* path, unsigned) {
    if (exists(path)) return {};
    return HalFile(resolve(path));
  }
  bool remove(const char* path) { return std::filesystem::remove(resolve(path)); }
  bool rename(const char* from, const char* to) {
    if (exists(to)) return false;
    std::filesystem::rename(resolve(from), resolve(to));
    return true;
  }
  static std::filesystem::path resolve(const char* path) { return dropboxTestRoot / std::string(path).substr(1); }
};
#define Storage HalStorage::getInstance()
