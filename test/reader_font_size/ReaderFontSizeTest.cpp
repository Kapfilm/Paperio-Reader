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
