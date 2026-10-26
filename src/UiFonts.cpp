#include "UiFonts.h"

#include <GfxRenderer.h>
#include <builtinFonts/inter_ui_10_bold.h>
#include <builtinFonts/inter_ui_10_regular.h>
#include <builtinFonts/inter_ui_12_bold.h>
#include <builtinFonts/inter_ui_12_regular.h>
#include <builtinFonts/notosans_8_bold.h>
#include <builtinFonts/notosans_8_regular.h>

#include "fontIds.h"

namespace {

EpdFont smallRegular(&notosans_8_regular);
EpdFont smallBold(&notosans_8_bold);
EpdFont ui10Regular(&inter_ui_10_regular);
EpdFont ui10Bold(&inter_ui_10_bold);
EpdFont ui12Regular(&inter_ui_12_regular);
EpdFont ui12Bold(&inter_ui_12_bold);

EpdFontFamily smallDefault(&smallRegular, &smallBold);
EpdFontFamily ui10Default(&ui10Regular, &ui10Bold);
EpdFontFamily ui12Default(&ui12Regular, &ui12Bold);
}  // namespace

void registerUiFonts(GfxRenderer& renderer) {
  renderer.insertFont(SMALL_FONT_ID, smallDefault);
  renderer.insertFont(UI_10_FONT_ID, ui10Default);
  renderer.insertFont(UI_12_FONT_ID, ui12Default);
}
