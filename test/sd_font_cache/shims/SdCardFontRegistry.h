#pragma once
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
// Selection is tested separately in sd_font_size; this fixture isolates cache IO.
struct SdCardFontFileInfo {
  std::string path;
  uint8_t pointSize;
};
struct SdCardFontFamilyInfo {
  std::string name;
  std::vector<SdCardFontFileInfo> files;
  const SdCardFontFileInfo* findFile(uint8_t size) const {
    for (const auto& f : files)
      if (f.pointSize == size) return &f;
    return nullptr;
  }
  std::vector<uint8_t> availableSizes() const {
    std::vector<uint8_t> out;
    for (const auto& f : files) out.push_back(f.pointSize);
    return out;
  }
  const SdCardFontFileInfo* pickClosestSize(uint8_t target) const {
    const SdCardFontFileInfo* best = nullptr;
    for (const auto& f : files)
      if (!best || std::abs(int(f.pointSize) - target) < std::abs(int(best->pointSize) - target)) best = &f;
    return best;
  }
};
