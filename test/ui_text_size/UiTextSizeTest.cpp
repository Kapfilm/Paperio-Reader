#include <GfxRenderer.h>
#include <builtinFonts/inter_ui_9_bold.h>
#include <builtinFonts/inter_ui_9_regular.h>
#include <gtest/gtest.h>

#include "UiFonts.h"
#include "fontIds.h"

TEST(UiTextSize, EnlargesAllInterfaceLevelsAndRestoresThem) {
  GfxRenderer r;
  registerUiFonts(r);
  const int small = r.fonts.at(SMALL_FONT_ID).getData()->advanceY;
  const int text = r.fonts.at(UI_10_FONT_ID).getData()->advanceY;
  const int header = r.fonts.at(UI_12_FONT_ID).getData()->advanceY;
  const auto* bookData = r.fonts.at(UI_10_FONT_ID).getData();
  r.insertFont(12345, r.fonts.at(UI_10_FONT_ID));
  EXPECT_TRUE(applyUiTextSize(r, true));
  EXPECT_EQ(r.fonts.at(12345).getData(), bookData);
  EXPECT_GT(r.fonts.at(SMALL_FONT_ID).getData()->advanceY, small);
  EXPECT_GT(r.fonts.at(UI_10_FONT_ID).getData()->advanceY, text);
  EXPECT_GT(r.fonts.at(UI_12_FONT_ID).getData()->advanceY, header);
  EXPECT_FALSE(applyUiTextSize(r, true));
  EXPECT_TRUE(applyUiTextSize(r, false));
  EXPECT_EQ(r.fonts.at(SMALL_FONT_ID).getData()->advanceY, small);
  EXPECT_EQ(r.fonts.at(UI_10_FONT_ID).getData()->advanceY, text);
  EXPECT_EQ(r.fonts.at(UI_12_FONT_ID).getData()->advanceY, header);
}
TEST(UiTextSize, MediumUsesInter11ForMainInterfaceText) {
  GfxRenderer renderer;
  registerUiFonts(renderer);
  const auto* normal = renderer.fonts.at(UI_10_FONT_ID).getData();
  EXPECT_TRUE(applyUiTextSize(renderer, UiTextSize::Medium));
  const auto& medium = renderer.fonts.at(UI_10_FONT_ID);
  const auto* mediumRegular = medium.getData();
  const auto* mediumBold = medium.getData(EpdFontFamily::BOLD);
  EXPECT_GT(mediumRegular->advanceY, normal->advanceY);
  EXPECT_NE(mediumRegular, mediumBold);
  EXPECT_NE(medium.getGlyph(0x416), nullptr);
  EXPECT_FALSE(applyUiTextSize(renderer, UiTextSize::Medium));
  EXPECT_TRUE(applyUiTextSize(renderer, UiTextSize::Medium, true));
  EXPECT_EQ(renderer.fonts.at(UI_10_FONT_ID).getData(), mediumBold);
  EXPECT_TRUE(applyUiTextSize(renderer, UiTextSize::Enlarged));
  EXPECT_GT(renderer.fonts.at(UI_10_FONT_ID).getData()->advanceY, mediumRegular->advanceY);
}
TEST(UiTextSize, CompactCapsulesUseEightNineAndTenPointFonts) {
  GfxRenderer renderer;
  for (bool bold : {false, true}) {
    registerUiFonts(renderer);
    applyUiTextSize(renderer, UiTextSize::Normal, bold);
    const auto* normalBody = renderer.fonts.at(UI_10_FONT_ID).getData();
    EXPECT_EQ(renderer.fonts.at(COMPACT_BUTTON_FONT_ID).getData(), renderer.fonts.at(SMALL_FONT_ID).getData());

    applyUiTextSize(renderer, UiTextSize::Medium, bold);
    EXPECT_EQ(renderer.fonts.at(COMPACT_BUTTON_FONT_ID).getData()->advanceY,
              (bold ? inter_ui_9_bold : inter_ui_9_regular).advanceY);

    applyUiTextSize(renderer, UiTextSize::Enlarged, bold);
    EXPECT_EQ(renderer.fonts.at(COMPACT_BUTTON_FONT_ID).getData(), normalBody);
  }
}
TEST(UiTextSize, ReregisterCanEnlargeAgain) {
  GfxRenderer r;
  registerUiFonts(r);
  applyUiTextSize(r, true);
  const int large = r.fonts.at(UI_12_FONT_ID).getData()->advanceY;
  registerUiFonts(r);
  EXPECT_TRUE(applyUiTextSize(r, true));
  EXPECT_EQ(r.fonts.at(UI_12_FONT_ID).getData()->advanceY, large);
}
TEST(UiTextSize, CyrillicSurvivesEnlargement) {
  GfxRenderer r;
  registerUiFonts(r);
  applyUiTextSize(r, true);
  for (int id : {SMALL_FONT_ID, UI_10_FONT_ID, UI_12_FONT_ID}) {
    EXPECT_NE(r.fonts.at(id).getGlyph(0x416), nullptr);
    EXPECT_NE(r.fonts.at(id).getGlyph(0x44F, EpdFontFamily::BOLD), nullptr);
  }
}
TEST(UiTextSize, BoldStyleCombinesWithBothInterfaceSizes) {
  GfxRenderer r;
  registerUiFonts(r);
  const int ids[] = {SMALL_FONT_ID, UI_10_FONT_ID, UI_12_FONT_ID};
  const EpdFontData* regular[3];
  const EpdFontData* bold[3];
  for (int i = 0; i < 3; ++i) {
    regular[i] = r.fonts.at(ids[i]).getData();
    bold[i] = r.fonts.at(ids[i]).getData(EpdFontFamily::BOLD);
  }
  EXPECT_TRUE(applyUiTextSize(r, false, true));
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(r.fonts.at(ids[i]).getData(), bold[i]);
    EXPECT_EQ(r.fonts.at(ids[i]).getData(EpdFontFamily::BOLD), bold[i]);
  }
  EXPECT_FALSE(applyUiTextSize(r, false, true));
  EXPECT_TRUE(applyUiTextSize(r, true, true));
  for (int i = 0; i < 3; ++i) {
    EXPECT_GT(r.fonts.at(ids[i]).getData()->advanceY, bold[i]->advanceY);
    EXPECT_EQ(r.fonts.at(ids[i]).getData(), r.fonts.at(ids[i]).getData(EpdFontFamily::BOLD));
    EXPECT_NE(r.fonts.at(ids[i]).getGlyph(0x416), nullptr);
  }
  EXPECT_TRUE(applyUiTextSize(r, true, false));
  for (int id : ids) EXPECT_NE(r.fonts.at(id).getData(), r.fonts.at(id).getData(EpdFontFamily::BOLD));
  EXPECT_TRUE(applyUiTextSize(r, false, false));
  for (int i = 0; i < 3; ++i) EXPECT_EQ(r.fonts.at(ids[i]).getData(), regular[i]);
}
TEST(UiTextSize, ReregisterCanApplyBoldAgain) {
  GfxRenderer r;
  registerUiFonts(r);
  applyUiTextSize(r, true, true);
  const auto* enlargedBold = r.fonts.at(UI_12_FONT_ID).getData();
  registerUiFonts(r);
  EXPECT_TRUE(applyUiTextSize(r, true, true));
  EXPECT_EQ(r.fonts.at(UI_12_FONT_ID).getData(), enlargedBold);
}
TEST(UiTextSize, InstructionFontRemainsSmallAcrossSizeAndStyleChanges) {
  GfxRenderer r;
  registerUiFonts(r);
  const auto* fixed = r.fonts.at(UI_HINT_FONT_ID).getData();
  const int height = fixed->advanceY;
  for (uint8_t size : {UiTextSize::Normal, UiTextSize::Medium, UiTextSize::Enlarged}) {
    for (bool bold : {false, true}) {
      applyUiTextSize(r, size, bold);
      EXPECT_EQ(r.fonts.at(UI_HINT_FONT_ID).getData(), fixed);
      EXPECT_EQ(r.fonts.at(UI_HINT_FONT_ID).getData()->advanceY, height);
      EXPECT_NE(r.fonts.at(UI_HINT_FONT_ID).getGlyph(0x416), nullptr);
    }
  }
  registerUiFonts(r);
  EXPECT_EQ(r.fonts.at(UI_HINT_FONT_ID).getData(), fixed);
}
