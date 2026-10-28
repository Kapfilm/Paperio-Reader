#include <HalStorage.h>
#include <SdCardFont.h>
#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <vector>

namespace {
class SdFontMetadataTest : public ::testing::Test {
 protected:
  SdCardFont font;
  void SetUp() override { font_metadata_test::storageAvailable = true; }
  void checkText(const char* text) {
    EXPECT_EQ(font.prewarm(text, 1, true, false), 0);
    ASSERT_NE(font.getEpdFont(0), nullptr);
    EXPECT_GT(font.getEpdFont(0)->data->intervalCount, 0u);
  }
};
}  // namespace
TEST_F(SdFontMetadataTest, SdFontCanRestoreMetadataBeforeLayingOutANewNote) {
  ASSERT_TRUE(font.load("fixture.cpfont"));
  checkText("Main text");
  font.clearAccumulation();
  font.unloadMetadata();
  ASSERT_TRUE(font.reloadMetadata());
  checkText("Примечание: новое слово");
}
TEST_F(SdFontMetadataTest, RepeatedNoteLayoutsRetainValidRestoredIntervals) {
  ASSERT_TRUE(font.load("fixture.cpfont"));
  const char* texts[] = {"Первая сноска", "Другая глава", "Третье примечание"};
  for (const auto* text : texts) {
    font.clearAccumulation();
    font.unloadMetadata();
    ASSERT_TRUE(font.reloadMetadata());
    checkText(text);
  }
}
TEST_F(SdFontMetadataTest, MissingCardMakesRestoreFailAndAllowsLaterRetry) {
  ASSERT_TRUE(font.load("fixture.cpfont"));
  font.clearAccumulation();
  font.unloadMetadata();
  font_metadata_test::storageAvailable = false;
  EXPECT_FALSE(font.reloadMetadata());
  // Callers must stop layout after false; they may retry when storage returns.
  font_metadata_test::storageAvailable = true;
  ASSERT_TRUE(font.reloadMetadata());
  checkText("После возврата SD");
}
TEST(SdFontMmapMetadata, UnloadIsANoOpForMappedFonts) {
  std::ifstream input(FONT_FIXTURE, std::ios::binary);
  ASSERT_TRUE(input.is_open());
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  ASSERT_FALSE(bytes.empty());
  SdCardFont font;
  ASSERT_TRUE(font.loadFromMmap(bytes.data(), bytes.size(), "fixture.cpfont"));
  font.unloadMetadata();
  EXPECT_EQ(font.prewarm("Примечание из flash", 1, true, false), 0);
  ASSERT_NE(font.getEpdFont(0), nullptr);
  EXPECT_GT(font.getEpdFont(0)->data->intervalCount, 0u);
}
