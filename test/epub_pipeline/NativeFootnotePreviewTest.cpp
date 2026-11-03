#include <CooperativeAbort.h>
#include <gtest/gtest.h>

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
    work = std::filesystem::temp_directory_path() /
           (std::string("paperio_native_note_") + testing::UnitTest::GetInstance()->current_test_info()->name());
    std::filesystem::remove_all(work);
    std::filesystem::create_directories(work);
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
          text += block.wordText(word);
          text += ' ';
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
