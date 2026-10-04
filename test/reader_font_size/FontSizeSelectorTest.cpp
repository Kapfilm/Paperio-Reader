#include <gtest/gtest.h>
#include "activities/settings/SettingInfo.h"
#include "util/ReaderFontSizeOptions.h"

CrossPointSettings CrossPointSettings::instance;
I18n& I18n::getInstance() { static I18n instance; return instance; }
const char* I18n::get(StrId) const { return "Named"; }

namespace {
struct Choice {
  int8_t named = -1;
  uint8_t point = 0;
  std::vector<uint8_t> sizes;
};
SettingInfo numericSetting(Choice& choice) {
  auto setting = SettingInfo::DynamicEnumCtx(StrId::STR_FONT_SIZE,
      {StrId::STR_DEFAULT_VALUE, StrId::STR_SMALL, StrId::STR_MEDIUM, StrId::STR_LARGE, StrId::STR_X_LARGE}, &choice,
      [](const void* ctx) -> uint8_t {
        const auto& c = *static_cast<const Choice*>(ctx);
        return reader_font_size::overrideSizeSlot(c.named, c.point, true, c.sizes);
      }, [](void* ctx, uint8_t index) {
        auto& c = *static_cast<Choice*>(ctx);
        const auto selected = reader_font_size::choiceForSlot(index, true, c.sizes);
        c.named = selected.named;
        c.point = selected.point;
      });
  setting.enumLabels.emplace_back("Default");
  for (auto point : choice.sizes) setting.enumLabels.push_back(std::to_string(point) + " pt");
  return setting;
}
}

TEST(FontSizeSelector, NumericLabelsAndSelectionBeyondLegacyPresets) {
  Choice choice{-1, 0, {10, 12, 14, 16, 18, 20, 24, 32}};
  auto setting = numericSetting(choice);
  EXPECT_EQ(setting.getEnumOptionCount(), 9);
  EXPECT_EQ(setting.getEnumOptionLabel(7), "24 pt");
  setting.setEnumSelectedIndex(7);
  EXPECT_EQ(choice.point, 24);
  EXPECT_EQ(setting.getEnumSelectedIndex(), 7);
  setting.setEnumSelectedIndex(0);
  EXPECT_EQ(choice.point, 0);
  EXPECT_EQ(choice.named, -1);
}

TEST(FontSizeSelector, DefaultPlusAll255SizesRemainsSelectable) {
  Choice choice;
  for (int i = 1; i <= 255; ++i) choice.sizes.push_back(static_cast<uint8_t>(i));
  auto setting = numericSetting(choice);
  EXPECT_EQ(setting.getEnumOptionCount(), 256);
  EXPECT_EQ(setting.getEnumOptionLabel(255), "255 pt");
  setting.setEnumSelectedIndex(255);
  EXPECT_EQ(choice.point, 255);
  EXPECT_EQ(setting.getEnumSelectedIndex(), 255);
  setting.setEnumSelectedIndex(0);
  EXPECT_EQ(choice.named, -1);
  EXPECT_EQ(choice.point, 0);
}
