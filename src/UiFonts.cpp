#include "UiFonts.h"

#include <GfxRenderer.h>
#include <builtinFonts/golos_ui_10_medium.h>
#include <builtinFonts/golos_ui_10_semibold.h>
#include <builtinFonts/golos_ui_12_medium.h>
#include <builtinFonts/golos_ui_12_semibold.h>
#include <builtinFonts/golos_ui_8_medium.h>
#include <builtinFonts/inter_ui_10_bold.h>
#include <builtinFonts/inter_ui_10_regular.h>
#include <builtinFonts/inter_ui_12_bold.h>
#include <builtinFonts/inter_ui_12_regular.h>
#include <builtinFonts/notosans_8_bold.h>
#include <builtinFonts/notosans_8_regular.h>

#include "fontIds.h"

namespace {
EpdFont golos8Medium(&golos_ui_8_medium);
EpdFont golos10Medium(&golos_ui_10_medium);
EpdFont golos10Semibold(&golos_ui_10_semibold);
EpdFont golos12Medium(&golos_ui_12_medium);
EpdFont golos12Semibold(&golos_ui_12_semibold);

EpdFont smallRegular(&notosans_8_regular);
EpdFont smallBold(&notosans_8_bold);
EpdFont ui10Regular(&inter_ui_10_regular);
EpdFont ui10Bold(&inter_ui_10_bold);
EpdFont ui12Regular(&inter_ui_12_regular);
EpdFont ui12Bold(&inter_ui_12_bold);

// Glyph bitmaps remain in flash. Switching changes only pointers in the existing
// font map: measuring, truncating and drawing all see the same real font metrics.
EpdFontFamily smallDefault(&smallRegular, &smallBold);
EpdFontFamily smallDark(&smallBold);
EpdFontFamily ui10Default(&ui10Regular, &ui10Bold);
EpdFontFamily ui10Dark(&ui10Bold, &ui10Bold);
EpdFontFamily ui12Default(&ui12Regular, &ui12Bold);
EpdFontFamily ui12Dark(&ui12Bold, &ui12Bold);
// Golos has no bundled 8 px semibold face; use the compact Noto Sans bold
// variant when a caller explicitly requests bold at SMALL_FONT_ID.
EpdFontFamily smallGolos(&golos8Medium, &smallBold);
EpdFontFamily ui10Golos(&golos10Medium, &golos10Semibold);
EpdFontFamily ui12Golos(&golos12Medium, &golos12Semibold);
}  // namespace

void registerUiFonts(GfxRenderer& renderer) {
  renderer.insertFont(SMALL_FONT_ID, smallDefault);
  renderer.insertFont(UI_10_FONT_ID, ui10Default);
  renderer.insertFont(UI_12_FONT_ID, ui12Default);
}

void applyUiFontStyle(GfxRenderer& renderer, uint8_t style) {
  const bool golos = style == 2;
  const bool dark = style == 1;
  renderer.replaceFont(SMALL_FONT_ID, golos ? smallGolos : (dark ? smallDark : smallDefault));
  renderer.replaceFont(UI_10_FONT_ID, golos ? ui10Golos : (dark ? ui10Dark : ui10Default));
  renderer.replaceFont(UI_12_FONT_ID, golos ? ui12Golos : (dark ? ui12Dark : ui12Default));
}
