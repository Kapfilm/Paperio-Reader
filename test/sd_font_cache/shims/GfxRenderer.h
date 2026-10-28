#pragma once
#include <map>
class SdCardFont;
struct EpdFontFamily;
class GfxRenderer {
  std::map<int, int> fonts;

 public:
  const auto& getFontMap() const { return fonts; }
  void registerSdCardFont(int id, SdCardFont*) { fonts[id] = 1; }
  void clearSdCardFonts() {}
  void removeFont(int id) { fonts.erase(id); }
  void insertFont(int, const EpdFontFamily&) {}
};
