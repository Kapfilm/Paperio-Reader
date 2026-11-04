#include <HalStorage.h>
#include <SdCardFont.h>
#include <gtest/gtest.h>

#include <algorithm>
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

namespace {
struct GlyphPixels {
  uint8_t style;
  uint32_t cp;
  std::vector<uint8_t> bitmap;
};

void checkMetadataRenderRecovery(SdCardFont& font) {
  constexpr uint32_t codepoints[] = {'A', 0x0416};  // Latin + Cyrillic, all present styles.
  ASSERT_EQ(font.prewarm("AЖ", 0x0F, false, false), 0);
  std::vector<GlyphPixels> expected;
  for (uint8_t style = 0; style < SdCardFont::MAX_STYLES; ++style) {
    if (!font.hasStyle(style)) continue;
    const EpdFont* epd = font.getEpdFont(style);
    ASSERT_NE(epd->data->bitmap, nullptr);
    for (uint32_t cp : codepoints) {
      const EpdGlyph* glyph = epd->getGlyph(cp);
      ASSERT_NE(glyph, nullptr);
      const uint8_t* bitmap = SdCardFont::getGlyphBitmap(epd->data, glyph);
      ASSERT_NE(bitmap, nullptr);
      ASSERT_GT(glyph->dataLength, 0u);
      expected.push_back({style, cp, {bitmap, bitmap + glyph->dataLength}});
    }
  }

  // Reproduce the foreground FULL -> background METADATA -> redraw handoff.
  // The old renderer used nullptr + the SD file offset for these interval hits.
  for (int cycle = 0; cycle < 3; ++cycle) {
    font.clearAccumulation();
    ASSERT_EQ(font.prewarm("AЖ", 0x0F, true, false), 0);
    for (const auto& sample : expected) {
      const EpdFont* epd = font.getEpdFont(sample.style);
      ASSERT_EQ(epd->data->bitmap, nullptr);
      const EpdGlyph* glyph = epd->getGlyph(sample.cp);
      ASSERT_NE(glyph, nullptr);
      EXPECT_FALSE(font.isOverflowGlyph(glyph));  // It hit metadata, not the miss handler.
      const uint8_t* bitmap = SdCardFont::getGlyphBitmap(epd->data, glyph);
      ASSERT_NE(bitmap, nullptr);
      EXPECT_EQ(std::vector<uint8_t>(bitmap, bitmap + sample.bitmap.size()), sample.bitmap);
    }
  }
}
}  // namespace

TEST_F(SdFontMetadataTest, MetadataGlyphsRecoverRealBitmapAfterBackgroundLayout) {
  ASSERT_TRUE(font.load("fixture.cpfont"));
  checkMetadataRenderRecovery(font);
}

TEST(SdFontMmapMetadata, MetadataGlyphsRecoverRealBitmapAfterBackgroundLayout) {
  std::ifstream input(FONT_FIXTURE, std::ios::binary);
  ASSERT_TRUE(input.is_open());
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  ASSERT_FALSE(bytes.empty());
  SdCardFont font;
  font_metadata_test::storageAvailable = true;
  ASSERT_TRUE(font.loadFromMmap(bytes.data(), bytes.size(), "fixture.cpfont"));
  checkMetadataRenderRecovery(font);
}

TEST_F(SdFontMetadataTest, MetadataBitmapReadFailureReturnsNullAndCanRetry) {
  ASSERT_TRUE(font.load("fixture.cpfont"));
  ASSERT_EQ(font.prewarm("Ж", 1, true, false), 0);
  const EpdFont* epd = font.getEpdFont(0);
  ASSERT_EQ(epd->data->bitmap, nullptr);
  const EpdGlyph* glyph = epd->getGlyph(0x0416);
  ASSERT_NE(glyph, nullptr);
  font_metadata_test::storageAvailable = false;
  EXPECT_EQ(SdCardFont::getGlyphBitmap(epd->data, glyph), nullptr);
  font_metadata_test::storageAvailable = true;
  EXPECT_NE(SdCardFont::getGlyphBitmap(epd->data, glyph), nullptr);
}

TEST_F(SdFontMetadataTest, MetadataFallbackUsesTheResolvedReplacementGlyph) {
  ASSERT_TRUE(font.load("fixture.cpfont"));
  ASSERT_EQ(font.prewarm("A", 1, true, false), 0);
  const EpdFont* epd = font.getEpdFont(0);
  // Seed a metadata interval containing U+FFFD. The unsupported original
  // codepoint below must resolve the bitmap of the glyph getGlyph actually returns.
  font.clearAccumulation();
  ASSERT_EQ(font.prewarm("�", 1, true, false), 0);
  const EpdGlyph* replacement = epd->getGlyph(0xFFFD);
  const EpdGlyph* missing = epd->getGlyph(0x10FFFF);
  ASSERT_NE(replacement, nullptr);
  ASSERT_EQ(missing, replacement);
  ASSERT_EQ(epd->data->bitmap, nullptr);
  const uint8_t* bitmap = SdCardFont::getGlyphBitmap(epd->data, missing);
  ASSERT_NE(bitmap, nullptr);
  EXPECT_EQ(bitmap, SdCardFont::getGlyphBitmap(epd->data, replacement));
}

namespace {
uint32_t fixtureU32(const std::vector<uint8_t>& bytes, size_t offset) {
  return static_cast<uint32_t>(bytes[offset]) | static_cast<uint32_t>(bytes[offset + 1]) << 8 |
         static_cast<uint32_t>(bytes[offset + 2]) << 16 | static_cast<uint32_t>(bytes[offset + 3]) << 24;
}
std::vector<uint8_t> fixtureBytes() {
  std::ifstream input(FONT_FIXTURE, std::ios::binary);
  return {(std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>()};
}
}  // namespace

TEST(SdFontMmapMetadata, AlignedMappedGlyphTableKeepsDirectMetadataFastPathWithoutSdIo) {
  const auto bytes = fixtureBytes();
  ASSERT_GE(bytes.size(), 64u);
  // The .cpfont v4 regular-style table is TOC entry 0. Shift the byte mapping,
  // not the file layout, so this case proves the aligned direct path survives.
  const size_t glyphOffset = fixtureU32(bytes, 32 + 24) + fixtureU32(bytes, 32 + 4) * sizeof(EpdUnicodeInterval);
  std::vector<uint8_t> storage(bytes.size() + alignof(EpdGlyph));
  const size_t shift =
      (alignof(EpdGlyph) - (reinterpret_cast<uintptr_t>(storage.data()) + glyphOffset) % alignof(EpdGlyph)) %
      alignof(EpdGlyph);
  std::copy(bytes.begin(), bytes.end(), storage.begin() + shift);
  const uint8_t* mapped = storage.data() + shift;
  SdCardFont font;
  font_metadata_test::storageAvailable = true;
  ASSERT_TRUE(font.loadFromMmap(mapped, bytes.size(), "fixture.cpfont"));
  font_metadata_test::storageAvailable = false;
  const int missed = font.prewarm("AЖ", 1, true, false);
  font_metadata_test::storageAvailable = true;
  ASSERT_EQ(missed, 0);
  const auto* data = font.getEpdFont(0)->data;
  EXPECT_EQ(reinterpret_cast<const uint8_t*>(data->glyph), mapped + glyphOffset);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(data->glyph) % alignof(EpdGlyph), 0u);
  EXPECT_NE(font.getEpdFont(0)->getGlyph(0x0416), nullptr);
}

TEST(SdFontMmapMetadata, UnalignedMappedGlyphTableCopiesAlignedMetadataBeforeUse) {
  const auto bytes = fixtureBytes();
  ASSERT_GE(bytes.size(), 64u);
  const size_t glyphOffset = fixtureU32(bytes, 32 + 24) + fixtureU32(bytes, 32 + 4) * sizeof(EpdUnicodeInterval);
  std::vector<uint8_t> storage(bytes.size() + alignof(EpdGlyph));
  size_t shift = 0;
  while ((reinterpret_cast<uintptr_t>(storage.data()) + shift + glyphOffset) % alignof(EpdGlyph) == 0) ++shift;
  std::copy(bytes.begin(), bytes.end(), storage.begin() + shift);
  const uint8_t* mapped = storage.data() + shift;
  SdCardFont font;
  font_metadata_test::storageAvailable = true;
  ASSERT_TRUE(font.loadFromMmap(mapped, bytes.size(), "fixture.cpfont"));
  ASSERT_EQ(font.prewarm("AЖ", 1, true, false), 0);
  const auto* data = font.getEpdFont(0)->data;
  EXPECT_NE(reinterpret_cast<const uint8_t*>(data->glyph), mapped + glyphOffset);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(data->glyph) % alignof(EpdGlyph), 0u);
  const auto* glyph = font.getEpdFont(0)->getGlyph(0x0416);
  ASSERT_NE(glyph, nullptr);
  EXPECT_GT(glyph->width, 0u);
  EXPECT_NE(SdCardFont::getGlyphBitmap(data, glyph), nullptr);
}
