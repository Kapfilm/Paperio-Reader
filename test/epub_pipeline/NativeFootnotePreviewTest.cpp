#include <CooperativeAbort.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <sstream>

#include "Epub.h"
#include "Epub/FootnotePreviews.h"
#include "Epub/Page.h"
#include "Epub/Section.h"
#include "Epub/parsers/ChapterHtmlSlimParser.h"
#include "Fb2.h"
#include "GfxRenderer.h"

namespace {
class NativeFootnotePreviewTest : public testing::TestWithParam<const char*> {
 protected:
  std::filesystem::path work;
  void SetUp() override {
    // Independent CTest / sanitizer processes may run the same test name together.
    std::string pattern = (std::filesystem::temp_directory_path() / "paperio_native_note_XXXXXX").string();
    const char* uniqueRoot = mkdtemp(pattern.data());
    ASSERT_NE(uniqueRoot, nullptr);
    work = uniqueRoot;
  }
  void TearDown() override { std::filesystem::remove_all(work); }
};

TEST_P(NativeFootnotePreviewTest, NativeChapterNotesAreGatheredAndRenderedOnlyWhenEnabled) {
  const auto cache = work / "cache";
  std::filesystem::create_directories(cache);
  Fb2 source(std::string(CORPUS_DIR) + "/" + GetParam(), cache.string());
  ASSERT_TRUE(source.load());
  Epub book(source.getPackagePath(), cache.string());
  ASSERT_TRUE(book.load(true));
  ASSERT_TRUE(book.isFb2Package());
  ASSERT_TRUE(FootnotePreviews::gather(book));
  ASSERT_TRUE(FootnotePreviews::cacheExists(book.getCachePath()));

  for (const bool enabled : {false, true}) {
    GfxRenderer renderer;
    FootnotePreviews::Lookup lookup;
    ASSERT_TRUE(lookup.open(book.getCachePath(), &book, 0));
    std::string mainText;
    size_t references = 0;
    ChapterHtmlSlimParser parser(
        nullptr, renderer, 1, 1.0f, false, 0, 460, 760, false, false, false,
        [&](std::unique_ptr<Page> page) {
          references += page->footnotes.size();
          for (const auto& element : page->elements) {
            if (element->getTag() != TAG_PageLine) continue;
            const auto& block = *static_cast<const PageLine&>(*element).getBlock();
            for (uint16_t i = 0; i < block.wordCount(); ++i) {
              mainText += block.wordText(i);
              mainText += ' ';
            }
          }
        },
        false, "", "", 0, {}, nullptr);
    if (enabled) parser.setInlineFootnotePreviews(&lookup);
    ASSERT_TRUE(parser.setup(1024));
    ASSERT_TRUE(book.readItemContentsToStream(book.getSpineItem(0).href, parser, 1024, true));
    ASSERT_TRUE(parser.finalize());
    EXPECT_EQ(mainText.find("NATIVE_FB2_FIRST_NOTE") != std::string::npos, enabled) << mainText;
    EXPECT_EQ(mainText.find("NATIVE_FB2_SECOND_NOTE") != std::string::npos, enabled) << mainText;
    EXPECT_NE(mainText.find("BEFORE"), std::string::npos);
    EXPECT_NE(mainText.find("AFTER"), std::string::npos);
    EXPECT_EQ(references, 2u);
  }
}

TEST_P(NativeFootnotePreviewTest, SectionCacheMissThenBuildRendersNativePreviewsOnlyWhenEnabled) {
  const auto cache = work / "cache";
  std::filesystem::create_directories(cache);
  Fb2 source(std::string(CORPUS_DIR) + "/" + GetParam(), cache.string());
  ASSERT_TRUE(source.load());
  auto book = std::make_shared<Epub>(source.getPackagePath(), cache.string());
  ASSERT_TRUE(book->load(true));
  ASSERT_TRUE(FootnotePreviews::gather(*book));
  GfxRenderer renderer;
  for (const bool enabled : {false, true}) {
    Section::BuildParams params;
    params.fontId = 1;
    params.viewportWidth = 460;
    params.viewportHeight = 760;
    params.inlineFootnotePreviews = enabled;
    Section section(book, 0, renderer);
    // Match EpubReaderActivity: probe before building. The failed open also
    // initializes the device HalFile wrapper retained by this Section instance.
    ASSERT_FALSE(section.loadSectionFile(params));
    ASSERT_TRUE(section.createSectionFile(params, {}, true));
    ASSERT_TRUE(section.loadSectionFile(params));
    std::string text;
    size_t references = 0;
    for (uint16_t pageIndex = 0; pageIndex < section.pageCount; ++pageIndex) {
      section.currentPage = pageIndex;
      const auto page = section.loadPageFromSectionFile();
      ASSERT_TRUE(page);
      references += page->footnotes.size();
      for (const auto& element : page->elements) {
        if (element->getTag() != TAG_PageLine) continue;
        const auto& block = *static_cast<const PageLine&>(*element).getBlock();
        for (uint16_t word = 0; word < block.wordCount(); ++word) {
          const std::string token = block.wordText(word);
          text += token;
          text += ' ';
          if (token.find("NATIVE_FB2_") != std::string::npos) {
            EXPECT_EQ(block.wordSizePct(word), 100);
          } else if (token == "BEFORE" || token == "AFTER" || token == "SECOND" || token == "END") {
            EXPECT_EQ(block.wordSizePct(word), 100);
          }
        }
      }
    }
    EXPECT_EQ(text.find("NATIVE_FB2_FIRST_NOTE") != std::string::npos, enabled) << text;
    EXPECT_EQ(text.find("NATIVE_FB2_SECOND_NOTE") != std::string::npos, enabled) << text;
    EXPECT_NE(text.find("BEFORE"), std::string::npos);
    EXPECT_NE(text.find("AFTER"), std::string::npos);
    EXPECT_EQ(references, 2u);
  }
}

TEST_P(NativeFootnotePreviewTest, FullMultiParagraphNoteSurvivesPaginationAndSerialization) {
  const bool zipped = std::string(GetParam()).ends_with(".zip");
  const auto cache = work / "cache";
  std::filesystem::create_directories(cache);
  Fb2 source(std::string(CORPUS_DIR) + "/inline_footnote_full.fb2" + (zipped ? ".zip" : ""), cache.string());
  ASSERT_TRUE(source.load());
  auto book = std::make_shared<Epub>(source.getPackagePath(), cache.string());
  ASSERT_TRUE(book->load(true));
  ASSERT_TRUE(FootnotePreviews::gather(*book));
  GfxRenderer renderer;
  Section::BuildParams params;
  params.fontId = 1;
  params.viewportWidth = 460;
  params.viewportHeight = 760;
  params.inlineFootnotePreviews = true;
  Section section(book, 0, renderer);
  ASSERT_FALSE(section.loadSectionFile(params));
  ASSERT_TRUE(section.createSectionFile(params, {}, true));
  ASSERT_TRUE(section.loadSectionFile(params));
  EXPECT_GT(section.pageCount, 1u);
  std::string text;
  size_t references = 0;
  size_t numberSeven = 0;
  for (uint16_t pageIndex = 0; pageIndex < section.pageCount; ++pageIndex) {
    section.currentPage = pageIndex;
    const auto page = section.loadPageFromSectionFile();
    ASSERT_TRUE(page);
    references += page->footnotes.size();
    for (const auto& element : page->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      const auto& block = *static_cast<const PageLine&>(*element).getBlock();
      for (uint16_t word = 0; word < block.wordCount(); ++word) {
        const std::string token = block.wordText(word);
        text += token + ' ';
        EXPECT_EQ(block.wordSizePct(word), 100) << token;
        std::string digits;
        for (unsigned char c : token) {
          if (c >= '0' && c <= '9') digits += static_cast<char>(c);
        }
        if (digits == "7") ++numberSeven;
        if (token == "BEFORE" || token == "AFTER" || token == "SECOND" || token == "END")
          EXPECT_EQ(static_cast<uint8_t>(block.wordStyle(word)) & EpdFontFamily::ITALIC, 0) << token;
      }
    }
  }
  EXPECT_EQ(numberSeven, 1u) << text;
  EXPECT_EQ(references, 2u);
  // Every UTF-8 word must survive cache-write and read chunk boundaries.
  for (const std::string word : {"пояснение", "подробность", "завершение"}) {
    size_t count = 0;
    for (size_t pos = text.find(word); pos != std::string::npos; pos = text.find(word, pos + word.size())) ++count;
    EXPECT_EQ(count, 120u) << word;
  }
  EXPECT_NE(text.find("LONG_NOTE_END"), std::string::npos);
  EXPECT_NE(text.find("1941"), std::string::npos);
  EXPECT_NE(text.find("YEAR_START_RETAINED"), std::string::npos);
  EXPECT_NE(text.find("AFTER"), std::string::npos);
  EXPECT_NE(text.find("END"), std::string::npos);
  EXPECT_LT(text.find("FIRST_PARAGRAPH"), text.find("MIDDLE_PARAGRAPH"));
  EXPECT_LT(text.find("MIDDLE_PARAGRAPH"), text.find("LAST_PARAGRAPH"));
  EXPECT_NE(text.find("пояснение MIDDLE_PARAGRAPH"), std::string::npos);
  EXPECT_NE(text.find("подробность LAST_PARAGRAPH"), std::string::npos);
  EXPECT_LT(text.find("LONG_NOTE_END"), text.find("AFTER"));
}

TEST_P(NativeFootnotePreviewTest, OneShotAbortOfAGeneratedWriteCannotPublishACompletedCache) {
  const auto cache = work / "cache";
  std::filesystem::create_directories(cache);
  Fb2 source(std::string(CORPUS_DIR) + "/" + GetParam(), cache.string());
  ASSERT_TRUE(source.load());
  Epub book(source.getPackagePath(), cache.string());
  ASSERT_TRUE(book.load(true));
  static unsigned int checks = 0;
  checks = 0;
  CooperativeAbort::clearAborted();
  // The first check enters native rendering; the second cancels its first
  // generated Print write. Later checks return false, so a non-sticky sink
  // would silently accept the remaining well-formed HTML after that short write.
  CooperativeAbort::setLongTaskAbortPredicate([] { return ++checks == 2; });
  const bool gathered = FootnotePreviews::gather(book);
  CooperativeAbort::setLongTaskAbortPredicate(nullptr);
  EXPECT_FALSE(gathered);
  EXPECT_TRUE(CooperativeAbort::consumeAborted());
  EXPECT_FALSE(FootnotePreviews::cacheExists(book.getCachePath()));
  ASSERT_TRUE(FootnotePreviews::gather(book));
  EXPECT_TRUE(FootnotePreviews::cacheExists(book.getCachePath()));
}

INSTANTIATE_TEST_SUITE_P(PlainAndZipped, NativeFootnotePreviewTest,
                         testing::Values("inline_footnote_preview.fb2", "inline_footnote_preview.fb2.zip"));
}  // namespace
