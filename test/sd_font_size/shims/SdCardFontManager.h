#pragma once
#include <SdCardFontRegistry.h>
#include <functional>
#include <string>
class GfxRenderer;
// Emulate only font payload IO. Selection uses the real registry implementation.
class SdCardFontManager {
  std::string name;
  uint8_t point = 0;
  bool preview = false;
 public:
  inline static int loads = 0;
  bool loadFamily(const SdCardFontFamilyInfo& family, GfxRenderer&, uint8_t target,
                  const std::function<void()>& onColdLoad = {}) {
    ++loads;
    const auto* best = family.pickClosestSize(target);
    if (!best) return false;
    name = family.name;
    point = best->pointSize;
    preview = false;
    if (onColdLoad) onColdLoad();
    return true;
  }
  bool loadFamilyPreview(const SdCardFontFamilyInfo& family, GfxRenderer& r, uint8_t target) {
    const bool ok = loadFamily(family, r, target);
    preview = ok;
    return ok;
  }
  void unloadAll(GfxRenderer&) { name.clear(); point = 0; preview = false; }
  const std::string& currentFamilyName() const { return name; }
  uint8_t currentPointSize() const { return point; }
  bool isPreviewLoad() const { return preview; }
  int getFontId(const std::string& family) const { return name == family && point ? 1000 + point : 0; }
};
