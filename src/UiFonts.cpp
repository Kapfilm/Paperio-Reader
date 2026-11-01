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
bool appliedBold = false;

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
EpdFontFamily smallDark(&smallBold, &smallBold);
EpdFontFamily ui10Dark(&ui10Bold, &ui10Bold);
EpdFontFamily ui12Dark(&ui12Bold, &ui12Bold);
EpdFontFamily ui14Dark(&ui14Bold, &ui14Bold);
EpdFontFamily ui10Default(&ui10Regular, &ui10Bold);
EpdFontFamily ui12Default(&ui12Regular, &ui12Bold);
}  // namespace

void registerUiFonts(GfxRenderer& renderer) {
  renderer.insertFont(SMALL_FONT_ID, smallDefault);
  renderer.insertFont(UI_HINT_FONT_ID, smallDefault);
  renderer.insertFont(UI_10_FONT_ID, ui10Default);
  renderer.insertFont(UI_12_FONT_ID, ui12Default);
  applied = false;
  appliedBold = false;
}

bool applyUiTextSize(GfxRenderer& renderer, bool enlarged, bool bold) {
  if (applied == enlarged && appliedBold == bold) return false;
  // Map both regular and explicit bold calls to real bold faces. Width measuring,
  // wrapping and drawing then agree for every size without synthetic thickening.
  renderer.replaceFont(SMALL_FONT_ID, enlarged ? (bold ? ui10Dark : ui10Default) : (bold ? smallDark : smallDefault));
  renderer.replaceFont(UI_10_FONT_ID, enlarged ? (bold ? ui12Dark : ui12Default) : (bold ? ui10Dark : ui10Default));
  renderer.replaceFont(UI_12_FONT_ID, enlarged ? (bold ? ui14Dark : ui14Default) : (bold ? ui12Dark : ui12Default));
  applied = enlarged;
  appliedBold = bold;
  return true;
}
