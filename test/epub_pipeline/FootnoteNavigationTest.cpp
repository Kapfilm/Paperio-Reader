#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "Epub.h"
#include "Epub/BookMetadataCache.h"
#include "Epub/FootnotePreviews.h"
#include "src/util/FootnoteHistory.h"

namespace {
class FootnoteNavigationTest : public testing::Test {
 protected:
  std::filesystem::path work;
  void SetUp() override {
    work = std::filesystem::temp_directory_path() /
           (std::string("paperio_note_") + testing::UnitTest::GetInstance()->current_test_info()->name());
    std::filesystem::remove_all(work);
    std::filesystem::create_directories(work);
  }
  void TearDown() override { std::filesystem::remove_all(work); }
};

TEST_F(FootnoteNavigationTest, ResolvesPathsRelativeToSourceBeforeBasenames) {
  Epub book(CORPUS_DIR "/../fixtures/footnote_paths.epub", work.string());
  ASSERT_TRUE(book.load(true));
  EXPECT_EQ(book.resolveHrefToSpineIndex("../B/notes.xhtml#n", 0), 1);
  EXPECT_EQ(book.resolveHrefToSpineIndex("notes.xhtml#n", 1), 1);
  EXPECT_EQ(book.resolveHrefToSpineIndex("OPS/B/notes.xhtml#n"), 1);
  EXPECT_EQ(book.resolveHrefToSpineIndex("B/notes.xhtml#n"), 1);
  EXPECT_EQ(book.resolveHrefToSpineIndex("/OPS/B/notes.xhtml#n", 0), 1);
  EXPECT_EQ(book.resolveHrefToSpineIndex("#n", 1), 1);
  EXPECT_EQ(book.resolveHrefToSpineIndex("../B/extra%20notes.xhtml#note%20two", 0), 2);
  EXPECT_EQ(book.resolveHrefToSpineIndex("notes.xhtml#n"), -1);
  EXPECT_EQ(book.resolveHrefToSpineIndex("absent/notes.xhtml#n", 0), -1);
  EXPECT_EQ(book.resolveHrefToSpineIndex("https://example.com/notes.xhtml#n", 0), -1);
  EXPECT_EQ(book.resolveHrefToSpineIndex("#n", 999), -1);
}

TEST_F(FootnoteNavigationTest, Fb2ResolvesLastNoteInLargeSpineAndRejectsNoncanonicalPaths) {
  const std::string package = (work / "fb2_book/package.epub").string();
  std::filesystem::create_directories(package);
  std::ofstream(work / "fb2_book/.fb2_source") << "source.fb2";
  Epub book(package, work.string());
  ASSERT_TRUE(book.isFb2Package());
  constexpr int count = 3941;
  {
    BookMetadataCache cache(book.getCachePath());
    ASSERT_TRUE(cache.beginWrite());
    ASSERT_TRUE(cache.beginContentOpfPass());
    for (int i = 0; i < count; ++i)
      cache.createSpineEntry("OEBPS/text/chapter_" + std::to_string(i) + ".xhtml");
    ASSERT_TRUE(cache.endContentOpfPass());
    ASSERT_TRUE(cache.beginTocPass(true));
    cache.createTocEntry("Main", "OEBPS/text/chapter_0.xhtml", "", 1);
    ASSERT_TRUE(cache.endTocPass());
    ASSERT_TRUE(cache.endWrite());
    ASSERT_TRUE(cache.buildBookBin(package, {}, true));
  }
  ASSERT_TRUE(book.load(false, true));
  ASSERT_TRUE(book.getBasePath().empty());  // Warm book.bin loads do not parse the OPF.
  EXPECT_EQ(book.resolveHrefToSpineIndex("text/chapter_3940.xhtml#fb2-verse", 0), 3940);
  EXPECT_EQ(book.resolveHrefToSpineIndex("chapter_3940.xhtml#last-note", 0), 3940);
  EXPECT_EQ(book.resolveHrefToSpineIndex("text/chapter_3940.xhtml#last-note"), 3940);
  EXPECT_EQ(book.resolveHrefToSpineIndex("OEBPS/text/chapter_3940.xhtml#last-note"), 3940);
  EXPECT_EQ(book.resolveHrefToSpineIndex("/OEBPS/text/chapter_3940.xhtml#last-note", 0), 3940);
  EXPECT_EQ(book.resolveHrefToSpineIndex("./chapter_3940.xhtml#last-note", 15), 3940);
  EXPECT_EQ(book.resolveHrefToSpineIndex("../text/chapter_3940.xhtml#last-note", 15), 3940);
  EXPECT_EQ(book.resolveHrefToSpineIndex("chapter_%33%39%34%30.xhtml#last-note", 15), 3940);
  EXPECT_EQ(book.resolveHrefToSpineIndex("#last-note", 3940), 3940);
  EXPECT_EQ(book.resolveHrefToSpineIndex("chapter_0.xhtml#main", 3940), 0);
  for (const char* invalid : {"wrong/chapter_3940.xhtml", "/wrong/chapter_3940.xhtml",
                              "https://example.org/text/chapter_3940.xhtml", "//host/text/chapter_3940.xhtml",
                              "text/chapter_3941.xhtml", "text/chapter_99999999999999999999999.xhtml",
                              "text/chapter_-1.xhtml", "text/chapter_+1.xhtml", "text/chapter_01.xhtml",
                              "text/chapter_.xhtml", "text/chapter_1x.xhtml", "text/chapter_1.xhtml.bak"}) {
    EXPECT_EQ(book.resolveHrefToSpineIndex(invalid, 0), -1) << invalid;
  }
  EXPECT_EQ(book.resolveHrefToSpineIndex("chapter_3940.xhtml"), -1);
}

TEST_F(FootnoteNavigationTest, Fb2GeneratedLinksSurviveColdAndWarmBookLoad) {
  const auto package = work / "fb2_book/package.epub";
  // This harness uses the ZIP-only filesystem shim; the full FB2 runner
  // separately verifies this route with real directory packages.
  std::filesystem::create_directories(package.parent_path());
  std::filesystem::copy_file(CORPUS_DIR "/../fixtures/fb2_navigation_package.epub", package);
  std::ofstream(work / "fb2_book/.fb2_source") << "source.fb2";

  // A new object is essential: the old object's OPF base would mask the warm-load failure.
  for (const bool cold : {true, false}) {
    SCOPED_TRACE(cold ? "cold OPF build" : "warm book.bin load");
    Epub book(package.string(), work.string());
    ASSERT_TRUE(book.load(cold, true));
    ASSERT_TRUE(book.isFb2Package());
    ASSERT_EQ(book.getSpineItem(1).href, "OEBPS/text/chapter_1.xhtml");
    if (cold) EXPECT_EQ(book.getBasePath(), "OEBPS/");
    else EXPECT_TRUE(book.getBasePath().empty());
    EXPECT_EQ(book.resolveHrefToSpineIndex("chapter_1.xhtml#fb2-verse", 0), 1);
    EXPECT_EQ(book.resolveHrefToSpineIndex("text/chapter_1.xhtml#fb2-verse", 0), 1);
    EXPECT_EQ(book.resolveHrefToSpineIndex("text/chapter_1.xhtml#fb2-verse"), 1);
    EXPECT_EQ(book.resolveHrefToSpineIndex("OEBPS/text/chapter_1.xhtml#fb2-verse", 0), 1);
    EXPECT_EQ(book.resolveHrefToSpineIndex("#fb2-verse", 1), 1);
    for (const char* invalid : {"wrong/chapter_1.xhtml", "wrong/text/chapter_1.xhtml",
                                "/wrong/text/chapter_1.xhtml", "other/OEBPS/text/chapter_1.xhtml"})
      EXPECT_EQ(book.resolveHrefToSpineIndex(invalid, 0), -1) << invalid;
  }
}

TEST_F(FootnoteNavigationTest, Fb2EncodedIndexMustMatchActualSpineEntry) {
  const std::string package = (work / "fb2_book/package.epub").string();
  std::filesystem::create_directories(package);
  std::ofstream(work / "fb2_book/.fb2_source") << "source.fb2";
  Epub book(package, work.string());
  {
    BookMetadataCache cache(book.getCachePath());
    ASSERT_TRUE(cache.beginWrite());
    ASSERT_TRUE(cache.beginContentOpfPass());
    cache.createSpineEntry("OEBPS/text/chapter_9.xhtml");
    ASSERT_TRUE(cache.endContentOpfPass());
    ASSERT_TRUE(cache.beginTocPass(true));
    ASSERT_TRUE(cache.endTocPass());
    ASSERT_TRUE(cache.endWrite());
    ASSERT_TRUE(cache.buildBookBin(package, {}, true));
  }
  ASSERT_TRUE(book.load(false, true));
  ASSERT_TRUE(book.getBasePath().empty());  // Warm book.bin loads do not parse the OPF.
  EXPECT_EQ(book.resolveHrefToSpineIndex("text/chapter_0.xhtml"), -1);
}

TEST_F(FootnoteNavigationTest, PreviewUsesCorrectFileAndDecodedAnchor) {
  Epub book(CORPUS_DIR "/../fixtures/footnote_paths.epub", work.string());
  ASSERT_TRUE(book.load(true));
  ASSERT_TRUE(FootnotePreviews::gather(book));
  ASSERT_TRUE(FootnotePreviews::cacheExists(book.getCachePath()));
  FootnotePreviews::Lookup lookup;
  ASSERT_TRUE(lookup.open(book.getCachePath(), &book, 0));
  std::string text;
  ASSERT_TRUE(lookup.find("../B/notes.xhtml#n", text));
  EXPECT_EQ(text, "CORRECT_NOTE_FROM_B");
  ASSERT_TRUE(lookup.find("../B/extra%20notes.xhtml#note%20two", text));
  EXPECT_EQ(text, "ESCAPED_NOTE_TEXT");
}

TEST_F(FootnoteNavigationTest, OldAndTruncatedPreviewCachesAreNotAccepted) {
  Epub book(CORPUS_DIR "/../fixtures/footnote_paths.epub", work.string());
  ASSERT_TRUE(book.load(true));
  ASSERT_TRUE(FootnotePreviews::gather(book));
  const auto path = book.getCachePath() + FootnotePreviews::CACHE_FILENAME;
  {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(4);
    const char oldVersion[2] = {1, 0};
    file.write(oldVersion, 2);
  }
  EXPECT_FALSE(FootnotePreviews::cacheExists(book.getCachePath()));
  ASSERT_TRUE(FootnotePreviews::gather(book));
  std::filesystem::resize_file(path, 7);
  EXPECT_FALSE(FootnotePreviews::cacheExists(book.getCachePath()));
}

TEST(FootnoteHistoryTest, NestedPreviewReturnsToItsOwnCoordinatesAndPreservesRoot) {
  FootnoteHistory history;
  EXPECT_FALSE(history.pop().has_value());
  EXPECT_TRUE(history.push({2, 47, 100, 321, "", ""}));
  EXPECT_TRUE(history.push({9, 1, 3, 8, "note-seven", "note-seven"}));
  EXPECT_EQ(history.root().pageNumber, 47);
  auto note = history.pop();
  ASSERT_TRUE(note);
  EXPECT_EQ(note->previewAnchor, "note-seven");
  EXPECT_EQ(note->pageNumber, 1);
  EXPECT_FALSE(history.empty());
  auto root = history.pop();
  EXPECT_EQ(root->spineIndex, 2);
  EXPECT_EQ(root->pageNumber, 47);
  EXPECT_EQ(root->pageCount, 100);
  EXPECT_TRUE(history.empty());
}

TEST(FootnoteHistoryTest, FullHistoryRejectsNewJumpWithoutLosingReturnPositions) {
  FootnoteHistory history;
  for (int i = 0; i < FootnoteHistory::CAPACITY; ++i) ASSERT_TRUE(history.push({i, i + 10}));
  ASSERT_TRUE(history.full());
  EXPECT_FALSE(history.push({999, 999}));
  EXPECT_EQ(history.size(), FootnoteHistory::CAPACITY);
  EXPECT_EQ(history.pop()->spineIndex, FootnoteHistory::CAPACITY - 1);
  history.keepRoot();
  EXPECT_EQ(history.size(), 1);
  EXPECT_EQ(history.pop()->pageNumber, 10);
}
}  // namespace
