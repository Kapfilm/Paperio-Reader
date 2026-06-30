#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class GfxRenderer;
class SdCardFont;
struct SdCardFontFamilyInfo;

class SdCardFontManager {
 public:
  SdCardFontManager() = default;
  ~SdCardFontManager();
  SdCardFontManager(const SdCardFontManager&) = delete;
  SdCardFontManager& operator=(const SdCardFontManager&) = delete;

  // Load the single size closest to targetPtSize for a discovered family.
  // Only one .cpfont file is loaded; other sizes remain on disk. This keeps
  // resident interval + kern/ligature tables to one size's worth of memory
  // (see PR #1327 discussion re: Literata OOM).
  // Returns true on success.
  // onColdLoad (if set) is invoked once, just before the slow path that writes the
  // font into the flash partition (i.e. only on a genuine first load, not on a
  // flash-cache mmap hit) — callers use it to show a "loading font" popup.
  bool loadFamily(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, uint8_t targetPtSize,
                  const std::function<void()>& onColdLoad = {});

  // Unload everything, unregister from renderer.
  void unloadAll(GfxRenderer& renderer);

  // Look up the font ID for the loaded family at the primary (body) size.
  // Returns 0 if nothing loaded or familyName doesn't match.
  int getFontId(const std::string& familyName) const;

  // Look up the font ID for a specific point size within the loaded family.
  // Returns 0 if that size is not loaded or familyName doesn't match.
  int getFontIdForPt(const std::string& familyName, uint8_t pt) const;

  // Get name of currently loaded family (empty if none).
  const std::string& currentFamilyName() const { return loadedFamilyName_; };

  // Point size of the primary (body) loaded entry — closest match to targetPtSize.
  // 0 if nothing loaded.
  uint8_t currentPointSize() const { return loadedPointSize_; };

  // All point sizes currently loaded for the active family, sorted ascending.
  // Empty if nothing loaded.
  std::vector<uint8_t> loadedSizes() const;

 private:
  struct LoadedFont {
    SdCardFont* font;  // heap-allocated, owned
    int fontId;
    uint8_t size;
  };
  static int computeFontId(uint32_t contentHash, const char* familyName, uint8_t pointSize);

  GfxRenderer* renderer_ = nullptr;
  std::string loadedFamilyName_;
  uint8_t loadedPointSize_ = 0;
  std::vector<LoadedFont> loaded_;
};
