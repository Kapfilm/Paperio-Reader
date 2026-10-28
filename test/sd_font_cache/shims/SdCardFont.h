#pragma once
#include <HalStorage.h>

#include <cstdint>
#include <new>
class SdCardFont {
 public:
  inline static int sdLoads = 0;
  inline static int mmapLoads = 0;
  bool load(const char* path) {
    ++sdLoads;
    return font_test::files.count(path) != 0;
  }
  bool loadFromMmap(const uint8_t*, size_t, const char*) {
    ++mmapLoads;
    return true;
  }
  uint32_t contentHash() const { return 12345; }
  int styleCount() const { return 4; }
  const void* getEpdFont(int) const { return nullptr; }
};
