#include "UiFonts.h"

#include <GfxRenderer.h>
#include <builtinFonts/inter_ui_10_bold.h>
#include <builtinFonts/inter_ui_10_regular.h>
#include <builtinFonts/inter_ui_12_bold.h>
#include <builtinFonts/inter_ui_12_regular.h>
#include <builtinFonts/inter_ui_14_bold.h>
#include <builtinFonts/inter_ui_14_regular.h>
#include <builtinFonts/notosans_8_bold.h>
#include <builtinFonts/notosans_8_regular.h>

#include "fontIds.h"

namespace {
bool applied = false;

EpdFont smallRegular(&notosans_8_regular);
EpdFont smallBold(&notosans_8_bold);
EpdFont ui10Regular(&inter_ui_10_regular);
EpdFont ui10Bold(&inter_ui_10_bold);
EpdFont ui12Regular(&inter_ui_12_regular);
EpdFont ui12Bold(&inter_ui_12_bold);

EpdFont ui14Regular(&inter_ui_14_regular);
EpdFont ui14Bold(&inter_ui_14_bold);
EpdFontFamily ui14Default(&ui14Regular, &ui14Bold);

EpdFontFamily smallDefault(&smallRegular, &smallBold);
EpdFontFamily ui10Default(&ui10Regular, &ui10Bold);
EpdFontFamily ui12Default(&ui12Regular, &ui12Bold);
}  // namespace

void registerUiFonts(GfxRenderer& renderer) {
  renderer.insertFont(SMALL_FONT_ID, smallDefault);
  renderer.insertFont(UI_10_FONT_ID, ui10Default);
  renderer.insertFont(UI_12_FONT_ID, ui12Default);
  applied = false;
}

bool applyUiTextSize(GfxRenderer& renderer, bool enlarged) {
  if (applied == enlarged) return false;
  renderer.replaceFont(SMALL_FONT_ID, enlarged ? ui10Default : smallDefault);
  renderer.replaceFont(UI_10_FONT_ID, enlarged ? ui12Default : ui10Default);
  renderer.replaceFont(UI_12_FONT_ID, enlarged ? ui14Default : ui12Default);
  applied = enlarged;
  return true;
}
