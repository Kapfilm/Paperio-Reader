#pragma once
#include <EpdFontFamily.h>

#include <map>
class GfxRenderer {
 public:
  std::map<int, EpdFontFamily> fonts;
  void insertFont(int id, EpdFontFamily font) { fonts.insert_or_assign(id, font); }
  void replaceFont(int id, EpdFontFamily font) {
    if (fonts.find(id) != fonts.end()) fonts.insert_or_assign(id, font);
  }
};
