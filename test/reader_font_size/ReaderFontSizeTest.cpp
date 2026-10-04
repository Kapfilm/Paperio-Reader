#include <gtest/gtest.h>
#include "src/util/ReaderFontSizeOptions.h"
using namespace reader_font_size;
TEST(ReaderFontSize, LegacySettingsKeepTheirPointSizes) {
  EXPECT_EQ(namedPointSize(0), 12);
  EXPECT_EQ(namedPointSize(1), 14);
  EXPECT_EQ(namedPointSize(2), 16);
  EXPECT_EQ(namedPointSize(3), 18);
}
TEST(ReaderFontSize, InvalidLegacySettingUsesMedium) {
  EXPECT_EQ(namedPointSize(4), 14);
  EXPECT_EQ(namedPointSize(255), 14);
}
TEST(ReaderFontSize, ReturningFromNumericModeClampsToBuiltinRange) {
  EXPECT_EQ(nearestNamedSize(1), 0);
  EXPECT_EQ(nearestNamedSize(9), 0);
  EXPECT_EQ(nearestNamedSize(24), 3);
  EXPECT_EQ(nearestNamedSize(255), 3);
}
TEST(ReaderFontSize, MidpointsPreferSmallerAndExactSizesRoundTrip) {
  EXPECT_EQ(nearestNamedSize(13), 0);
  EXPECT_EQ(nearestNamedSize(15), 1);
  EXPECT_EQ(nearestNamedSize(17), 2);
  for (uint8_t i=0;i<4;++i) EXPECT_EQ(nearestNamedSize(namedPointSize(i)),i);
}

TEST(BookNumericSize, DefaultInheritsRatherThanSelectingFirstNumericSize) {
  const std::vector<uint8_t> sizes{10,14,20,24,32};
  EXPECT_EQ(overrideSizeSlot(-1,0,true,sizes),0);
  const auto choice=choiceForSlot(0,true,sizes);
  EXPECT_EQ(choice.named,-1); EXPECT_EQ(choice.point,0);
}
TEST(BookNumericSize, AllInstalledSizesCanRoundTripBeyondFourPresets) {
  const std::vector<uint8_t> sizes{10,12,14,16,18,20,24,32};
  for (uint8_t slot=1;slot<=sizes.size();++slot) {
    const auto choice=choiceForSlot(slot,true,sizes);
    EXPECT_EQ(choice.point,sizes[slot-1]);
    EXPECT_EQ(overrideSizeSlot(choice.named,choice.point,true,sizes),slot);
  }
}
TEST(BookNumericSize, LegacyPresetAndMissingFileUseClosestSmallerOnTie) {
  const std::vector<uint8_t> sizes{12,16,24};
  EXPECT_EQ(overrideSizeSlot(1,0,true,sizes),1);
  EXPECT_EQ(overrideSizeSlot(3,20,true,sizes),2);
}
TEST(BookNumericSize, NamedSelectionClearsNumericOverride) {
  const std::vector<uint8_t> sizes{12,14,16,18};
  const auto choice=choiceForSlot(2,false,sizes);
  EXPECT_EQ(choice.named,1); EXPECT_EQ(choice.point,0);
  EXPECT_EQ(overrideSizeSlot(3,32,false,sizes),4);
}
TEST(BookNumericSize, LargestPointAndOptionIndexDoNotOverflow) {
  std::vector<uint8_t> sizes;
  for(int point=1;point<=255;++point) sizes.push_back(point);
  const auto choice=choiceForSlot(255,true,sizes);
  EXPECT_EQ(choice.point,255);
  EXPECT_EQ(overrideSizeSlot(choice.named,choice.point,true,sizes),255);
}
