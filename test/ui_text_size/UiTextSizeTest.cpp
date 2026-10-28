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
