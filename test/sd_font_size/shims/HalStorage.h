#pragma once
#include <cstddef>
struct FsFile {
  explicit operator bool() const { return false; }
  bool isDirectory() const { return false; }
  FsFile openNextFile() { return {}; }
  void getName(char*, size_t) {}
  void close() {}
};
struct StorageStub { FsFile open(const char*) { return {}; } };
inline StorageStub Storage;
