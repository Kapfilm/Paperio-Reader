#include <gtest/gtest.h>
#include <GfxRenderer.h>
#include "CrossPointSettings.h"
#include "SdCardFontGlobals.h"
#include <cstring>

CrossPointSettings CrossPointSettings::instance;
SdCardFontSystem sdFontSystem;

class SdFontSizeTest : public testing::Test {
 protected:
  GfxRenderer renderer;
  void SetUp() override {
    sdFontSystem.unload(renderer);
    auto& families = const_cast<std::vector<SdCardFontFamilyInfo>&>(sdFontSystem.registry().getFamilies());
    families = {{"Many", {{"a", 10, 0}, {"b", 17, 0}, {"c", 23, 0}, {"d", 32, 0}}},
                {"Other", {{"e", 20, 0}, {"f", 26, 0}}}};
    SETTINGS.numericFontSizes = 1;
    SETTINGS.readerFontPointSize = 23;
    SETTINGS.fontSize = CrossPointSettings::EXTRA_LARGE;
    strcpy(SETTINGS.sdFontFamilyName, "Many");
    SdCardFontManager::loads = 0;
  }
};

TEST_F(SdFontSizeTest, PreviewAndReaderUseTheSameExplicitPoint) {
  sdFontSystem.ensureLoadedForPreview(renderer, "Many", CrossPointSettings::MEDIUM, 23);
  EXPECT_EQ(resolveSdCardFontId("Many", CrossPointSettings::MEDIUM, 23), 1023);
  int coldLoads = 0;
  sdFontSystem.ensureLoaded(renderer, "Many", CrossPointSettings::MEDIUM, [&] { ++coldLoads; }, 23);
  EXPECT_EQ(coldLoads, 1); // Preview must be promoted to the normal cache-backed load.
  EXPECT_EQ(resolveSdCardFontId("Many", CrossPointSettings::MEDIUM, 23), 1023);
  sdFontSystem.ensureLoaded(renderer, "Many", CrossPointSettings::MEDIUM, [&] { ++coldLoads; }, 23);
  EXPECT_EQ(coldLoads, 1);
}

TEST_F(SdFontSizeTest, MissingNamedSizeResolvesToLoadedClosestFile) {
  sdFontSystem.ensureLoaded(renderer, "Many", CrossPointSettings::MEDIUM);
  EXPECT_EQ(resolveSdCardFontId("Many", CrossPointSettings::MEDIUM), 1017);
  EXPECT_EQ(resolveSdCardFontId("Other", CrossPointSettings::MEDIUM), 0);
  EXPECT_EQ(resolveSdCardFontId("Many", CrossPointSettings::MEDIUM, 32), 0);
}

TEST_F(SdFontSizeTest, ZeroPointUsesBookNamedPresetInsteadOfGlobalNumericPoint) {
  sdFontSystem.ensureLoaded(renderer);
  EXPECT_EQ(resolveSdCardFontId("Many", CrossPointSettings::MEDIUM, 23), 1023);
  sdFontSystem.ensureLoaded(renderer, "Many", CrossPointSettings::SMALL, {}, 0);
  EXPECT_EQ(resolveSdCardFontId("Many", CrossPointSettings::SMALL), 1010);
  EXPECT_EQ(resolveSdCardFontId("Many", CrossPointSettings::MEDIUM, 23), 0);
}

TEST_F(SdFontSizeTest, SwitchingFamilyChoosesNearestAvailableAndBuiltinClearsPoint) {
  fontFamilyDynamicSetter(nullptr, CrossPointSettings::BUILTIN_FONT_COUNT + 1);
  EXPECT_STREQ(SETTINGS.sdFontFamilyName, "Other");
  EXPECT_EQ(SETTINGS.readerFontPointSize, 20); // 23 ties between 20 and 26; choose lower.
  fontFamilyDynamicSetter(nullptr, 0);
  EXPECT_EQ(SETTINGS.readerFontPointSize, 0);
  EXPECT_STREQ(SETTINGS.sdFontFamilyName, "");
  EXPECT_EQ(SETTINGS.fontSize, CrossPointSettings::EXTRA_LARGE);
}

TEST_F(SdFontSizeTest, NamedModeFamilyChangeDoesNotInventNumericOverride) {
  SETTINGS.numericFontSizes = 0;
  SETTINGS.readerFontPointSize = 0;
  fontFamilyDynamicSetter(nullptr, CrossPointSettings::BUILTIN_FONT_COUNT + 1);
  EXPECT_EQ(SETTINGS.readerFontPointSize, 0);
  sdFontSystem.ensureLoaded(renderer);
  EXPECT_EQ(resolveSdCardFontId("Other", CrossPointSettings::EXTRA_LARGE), 1020);
}
