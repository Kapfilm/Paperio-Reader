#pragma once
// Real-filesystem shim for host tests that exercise ZipFile or CssParser.
// Implements FsFile on top of stdio so tests can read actual .epub files
// and write real cache files under /tmp.

#include <fcntl.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

// Device HalStorage.h transitively provides the Arduino core (millis, ESP);
// mirror that so TUs which only include HalStorage.h still compile.
#include "Arduino.h"
#include "Print.h"
#include "WString.h"

struct IoCounts {
  size_t opens=0, reads=0, writes=0, seeks=0, readBytes=0, writeBytes=0;
};
inline IoCounts ioCounts;
class HalFile : public Print {
 public:
  HalFile() = default;
  ~HalFile() { release(); }

  HalFile(HalFile&& o) noexcept : path_(std::move(o.path_)), fp_(o.fp_), initialized_(o.initialized_) { o.fp_ = nullptr; o.initialized_ = false; }
  HalFile& operator=(HalFile&& o) noexcept {
    if (this != &o) {
      release();
      initialized_ = o.initialized_;
      o.initialized_ = false;
      path_ = std::move(o.path_);
      fp_ = o.fp_;
      o.fp_ = nullptr;
    }
    return *this;
  }
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  bool openMode(const char* path, const char* mode) { release(); initialized_ = true; path_=path; if(isDirectory()) return true; ++ioCounts.opens; fp_ = fopen(path, mode); return fp_ != nullptr; }

  bool openForRead(const std::string& path) {
    release();
    initialized_ = true;
    path_=path; if(isDirectory()) return true;
    ++ioCounts.opens; fp_ = fopen(path.c_str(), "rb");
    return fp_ != nullptr;
  }

  bool openForWrite(const std::string& path) {
    release();
    initialized_ = true;
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
      std::error_code ec;
      std::filesystem::create_directories(parent, ec);
      if (ec) return false;
    }
    ++ioCounts.opens; fp_ = fopen(path.c_str(), "wb");
    return fp_ != nullptr;
  }

  bool close() {
    // The device asserts when close() is called before any open attempt.
    // Keep destructor/move cleanup separate, as the production wrapper does.
    if (!initialized_) { std::fputs("HalFile::close before initialization\n", stderr); std::abort(); }
    release();
    return true;
  }

  bool isOpen() const { return fp_ != nullptr; }
  explicit operator bool() const { return fp_ != nullptr || isDirectory(); }

  int read(void* buf, size_t n) {
    if (!fp_) return -1;
    ++ioCounts.reads; size_t got=fread(buf, 1, n, fp_); ioCounts.readBytes+=got; return static_cast<int>(got);
  }
  // Single-byte read, SdFat-style: returns the byte or -1 (used by Bitmap.cpp).
  int read() {
    if (!fp_) return -1;
    ++ioCounts.reads; int b=fgetc(fp_); ioCounts.readBytes+=(b>=0); return b;
  }

  bool seek(size_t pos) { ++ioCounts.seeks; return fp_ && fseek(fp_, static_cast<long>(pos), SEEK_SET) == 0; }
  bool seekSet(size_t pos) { return seek(pos); }
  bool seekCur(int64_t offset) { ++ioCounts.seeks; return fp_ && fseek(fp_, static_cast<long>(offset), SEEK_CUR) == 0; }
  bool seek64(uint64_t pos) { ++ioCounts.seeks; return fp_ && fseek(fp_, static_cast<long>(pos), SEEK_SET) == 0; }
  bool seekSet64(uint64_t pos) { return seek64(pos); }

  size_t position() const { return fp_ ? static_cast<size_t>(ftell(fp_)) : 0; }
  uint64_t position64() const { return position(); }
  int available() const {
    if (!fp_) return 0;
    const long pos = ftell(fp_);
    fseek(fp_, 0, SEEK_END);
    const long end = ftell(fp_);
    fseek(fp_, pos, SEEK_SET);
    return static_cast<int>(end - pos);
  }

  size_t size() { return fileSize(); }
  size_t fileSize() {
    if (!fp_) return 0;
    const long cur = ftell(fp_);
    fseek(fp_, 0, SEEK_END);
    const long sz = ftell(fp_);
    fseek(fp_, cur, SEEK_SET);
    return static_cast<size_t>(sz);
  }
  uint64_t size64() { return fileSize(); }
  uint64_t fileSize64() { return fileSize(); }

  size_t write(const uint8_t* data, size_t size) {
    if (!fp_) return 0;
    ++ioCounts.writes; size_t n=fwrite(data, 1, size, fp_); ioCounts.writeBytes+=n; return n;
  }
  // Device SdFat accepts void*; Page.cpp writes char arrays through this.
  size_t write(const void* data, size_t size) {
    if (!fp_) return 0;
    ++ioCounts.writes; size_t n=fwrite(data, 1, size, fp_); ioCounts.writeBytes+=n; return n;
  }
  size_t write(uint8_t c) override {
    if (!fp_) return 0;
    ++ioCounts.writes; size_t n=fwrite(&c, 1, 1, fp_); ioCounts.writeBytes+=n; return n;
  }
  void flush() {
    if (fp_) fflush(fp_);
  }
  size_t getName(char*, size_t) { return 0; }
  bool rename(const char*) { return false; }
  bool getModifyDateTime(uint16_t*, uint16_t*) { return false; }
  bool isDirectory() const { return !path_.empty() && std::filesystem::is_directory(path_); }
  void rewindDirectory() {}
  HalFile openNextFile() { return HalFile{}; }

 private:
  void release() { if (fp_) { fclose(fp_); fp_ = nullptr; } path_.clear(); }
  std::string path_;
  FILE* fp_ = nullptr;
  bool initialized_ = false;
};

using FsFile = HalFile;

class HalStorage {
 public:
  size_t readFileToBuffer(const char* path, char* buffer, size_t size, size_t maxBytes=0) {
    HalFile f; if(!size || !f.openForRead(path)) return 0;
    size_t n=f.read(buffer, std::min(size-1, maxBytes ? maxBytes : size-1));
    buffer[n]=0; return n;
  }
  bool openFileForRead(const char*, const std::string& path, HalFile& f) { return f.openForRead(path); }
  bool openFileForRead(const char*, const char* path, HalFile& f) { return f.openForRead(path); }
  bool openFileForWrite(const char*, const std::string& path, HalFile& f) { return f.openForWrite(path); }
  bool exists(const char* path) { return std::filesystem::exists(path); }
  bool mkdir(const char* path, bool recursive = true) {
    std::error_code ec;
    return recursive ? std::filesystem::create_directories(path, ec) : std::filesystem::create_directory(path, ec);
  }
  bool remove(const char* path) { return std::filesystem::remove(path); }
  bool removeDir(const char* path) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    return !ec;
  }
  using oflag_t = int;
  bool rename(const char* from, const char* to) { return std::rename(from, to) == 0; }
  HalFile open(const char* path, oflag_t flags = O_RDONLY) {
    HalFile f;
    f.openMode(path, (flags & O_TRUNC) ? "w+b" : ((flags & O_RDWR) ? "r+b" : "rb"));
    return f;
  }
  // Sorted for determinism: std::filesystem iteration order is unspecified and
  // the pipeline harness requires byte-identical behavior across runs.
  std::vector<String> listFiles(const char* path = "/", int maxFiles = 200) {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(path, ec)) {
      if (e.is_regular_file()) names.push_back(e.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    if (names.size() > static_cast<size_t>(maxFiles)) names.resize(maxFiles);
    std::vector<String> out;
    out.reserve(names.size());
    for (auto& n : names) out.push_back(String(std::move(n)));
    return out;
  }
  static HalStorage& getInstance() {
    static HalStorage i;
    return i;
  }
};

#define Storage HalStorage::getInstance()
