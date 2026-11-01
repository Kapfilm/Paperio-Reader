#include <GfxRenderer.h>
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
  for (bool enlarged : {false, true}) {
    for (bool bold : {false, true}) {
      applyUiTextSize(r, enlarged, bold);
      EXPECT_EQ(r.fonts.at(UI_HINT_FONT_ID).getData(), fixed);
      EXPECT_EQ(r.fonts.at(UI_HINT_FONT_ID).getData()->advanceY, height);
      EXPECT_NE(r.fonts.at(UI_HINT_FONT_ID).getGlyph(0x416), nullptr);
    }
  }
  registerUiFonts(r);
  EXPECT_EQ(r.fonts.at(UI_HINT_FONT_ID).getData(), fixed);
}
