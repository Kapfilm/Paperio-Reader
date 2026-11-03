#include <CooperativeAbort.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "Epub.h"
#include "Epub/BookMetadataCache.h"
#include "Epub/FootnotePreviews.h"
#include "Epub/Page.h"
#include "Epub/Section.h"
#include "Epub/parsers/ChapterHtmlSlimParser.h"
#include "GfxRenderer.h"
#include "PipelineRunner.h"
#include "src/util/FootnoteHistory.h"

namespace {
class FootnoteNavigationTest : public testing::Test {
 protected:
  std::filesystem::path work;
  void SetUp() override {
    // Independent CTest / sanitizer processes may run the same test name together.
    std::string pattern = (std::filesystem::temp_directory_path() / "paperio_note_XXXXXX").string();
    const char* uniqueRoot = mkdtemp(pattern.data());
    ASSERT_NE(uniqueRoot, nullptr);
    work = uniqueRoot;
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
    for (int i = 0; i < count; ++i) cache.createSpineEntry("OEBPS/text/chapter_" + std::to_string(i) + ".xhtml");
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
  for (const char* invalid :
       {"wrong/chapter_3940.xhtml", "/wrong/chapter_3940.xhtml", "https://example.org/text/chapter_3940.xhtml",
        "//host/text/chapter_3940.xhtml", "text/chapter_3941.xhtml", "text/chapter_99999999999999999999999.xhtml",
        "text/chapter_-1.xhtml", "text/chapter_+1.xhtml", "text/chapter_01.xhtml", "text/chapter_.xhtml",
        "text/chapter_1x.xhtml", "text/chapter_1.xhtml.bak"}) {
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
    if (cold)
      EXPECT_EQ(book.getBasePath(), "OEBPS/");
    else
      EXPECT_TRUE(book.getBasePath().empty());
    EXPECT_EQ(book.resolveHrefToSpineIndex("chapter_1.xhtml#fb2-verse", 0), 1);
    EXPECT_EQ(book.resolveHrefToSpineIndex("text/chapter_1.xhtml#fb2-verse", 0), 1);
    EXPECT_EQ(book.resolveHrefToSpineIndex("text/chapter_1.xhtml#fb2-verse"), 1);
    EXPECT_EQ(book.resolveHrefToSpineIndex("OEBPS/text/chapter_1.xhtml#fb2-verse", 0), 1);
    EXPECT_EQ(book.resolveHrefToSpineIndex("#fb2-verse", 1), 1);
    for (const char* invalid : {"wrong/chapter_1.xhtml", "wrong/text/chapter_1.xhtml", "/wrong/text/chapter_1.xhtml",
                                "other/OEBPS/text/chapter_1.xhtml"})
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

TEST_F(FootnoteNavigationTest, UnpackedBookPreviewsUseTheSameStreamingSourceAsReading) {
  const auto package = work / "unpacked.epub";
  std::filesystem::create_directories(package);
  std::ofstream(package / "chapter.xhtml")
      << "<html><body><p>Text<a epub:type='noteref' href='notes.xhtml#note%20one'>note</a></p></body></html>";
  std::ofstream(package / "notes.xhtml")
      << "<html><body><aside id='note one'>EXPLANATION_FROM_UNPACKED_BOOK</aside></body></html>";
  Epub book(package.string(), work.string());
  BookMetadataCache metadata(book.getCachePath());
  ASSERT_TRUE(metadata.beginWrite());
  ASSERT_TRUE(metadata.beginContentOpfPass());
  metadata.createSpineEntry("chapter.xhtml");
  metadata.createSpineEntry("notes.xhtml");
  ASSERT_TRUE(metadata.endContentOpfPass());
  ASSERT_TRUE(metadata.beginTocPass(false));
  ASSERT_TRUE(metadata.endTocPass());
  ASSERT_TRUE(metadata.endWrite());
  ASSERT_TRUE(metadata.buildBookBin(package.string(), {}, true));
  ASSERT_TRUE(book.load(false, true));
  ASSERT_TRUE(FootnotePreviews::gather(book));
  FootnotePreviews::Lookup lookup;
  ASSERT_TRUE(lookup.open(book.getCachePath(), &book, 0));
  std::string text;
  ASSERT_TRUE(lookup.find("notes.xhtml#note%20one", text));
  EXPECT_EQ(text, "EXPLANATION_FROM_UNPACKED_BOOK");
}

TEST_F(FootnoteNavigationTest, LegacyNamedNoteAnchorsAndSemanticReferencesAreGathered) {
  Epub book(CORPUS_DIR "/../fixtures/footnote_legacy_name.epub", work.string());
  ASSERT_TRUE(book.load(true));
  ASSERT_TRUE(FootnotePreviews::gather(book));
  FootnotePreviews::Lookup lookup;
  ASSERT_TRUE(lookup.open(book.getCachePath(), &book, 0));
  std::string text;
  ASSERT_TRUE(lookup.find("../B/notes.xhtml#n", text));
  EXPECT_EQ(text, "LEGACY_NAMED_NOTE");
  ASSERT_TRUE(lookup.find("../B/extra%20notes.xhtml#note%20two", text));
  EXPECT_EQ(text, "ENCODED_NAMED_NOTE");
  ASSERT_TRUE(lookup.find("#same", text));
  EXPECT_EQ(text, "SAME_SPINE_NOTE");
}

TEST_F(FootnoteNavigationTest, EnabledPreviewsRenderInZipBookButDisabledSettingDoesNotExpand) {
  for (const bool enabled : {false, true}) {
    pipeline_harness::Profile profile;
    profile.inlineFootnotePreviews = enabled;
    std::ostringstream dump;
    ASSERT_TRUE(pipeline_harness::runAndDump(CORPUS_DIR "/../fixtures/footnote_legacy_name.epub",
                                             (work / (enabled ? "on" : "off")).string(), profile, dump));
    const std::string firstSpine = dump.str().substr(0, dump.str().find("SPINE 1"));
    EXPECT_EQ(firstSpine.find("LEGACY_NAMED_NOTE") != std::string::npos, enabled);
    EXPECT_EQ(firstSpine.find("ENCODED_NAMED_NOTE") != std::string::npos, enabled);
  }
}

TEST_F(FootnoteNavigationTest, InlinePreviewUsesBodySizeAndDoesNotChangeReferenceOrFollowingText) {
  Epub book(CORPUS_DIR "/../fixtures/footnote_legacy_name.epub", work.string());
  ASSERT_TRUE(book.load(true));
  ASSERT_TRUE(FootnotePreviews::gather(book));
  for (const bool normalize : {false, true}) {
    for (const bool sizedReference : {false, true}) {
      GfxRenderer renderer;
      CssParser css("");
      FootnotePreviews::Lookup lookup;
      ASSERT_TRUE(lookup.open(book.getCachePath(), &book, 0));
      size_t noteWords = 0;
      size_t referenceWords = 0;
      size_t bodyWords = 0;
      ChapterHtmlSlimParser parser(
          nullptr, renderer, 1, 1.0f, false, 0, 460, 760, false, normalize, false,
          [&](std::unique_ptr<Page> page) {
            for (const auto& element : page->elements) {
              if (element->getTag() != TAG_PageLine) continue;
              const auto& block = *static_cast<const PageLine&>(*element).getBlock();
              for (uint16_t i = 0; i < block.wordCount(); ++i) {
                const std::string token = block.wordText(i);
                if (token.find("LEGACY_NAMED_NOTE") != std::string::npos) {
                  ++noteWords;
                  EXPECT_EQ(block.wordSizePct(i), 100);
                } else if (token == "reference") {
                  ++referenceWords;
                  EXPECT_EQ(block.wordSizePct(i), sizedReference ? 120 : 100);
                  EXPECT_EQ(static_cast<uint8_t>(block.wordStyle(i)) & EpdFontFamily::ITALIC, 0);
                } else if (token == "BEFORE" || token == "AFTER") {
                  ++bodyWords;
                  EXPECT_EQ(block.wordSizePct(i), 100);
                  EXPECT_EQ(static_cast<uint8_t>(block.wordStyle(i)) & EpdFontFamily::ITALIC, 0);
                }
              }
            }
          },
          true, "", "", 0, {}, nullptr, &css);
      parser.setInlineFootnotePreviews(&lookup);
      ASSERT_TRUE(parser.setup(1024));
      const std::string html =
          "<html><body><p>BEFORE <a epub:type='noteref' href='../B/notes.xhtml#n'>" +
          std::string(sizedReference ? "<span style='font-size:1.2em'>reference</span>" : "reference") +
          "</a> AFTER</p></body></html>";
      ASSERT_EQ(parser.write(reinterpret_cast<const uint8_t*>(html.data()), html.size()), html.size());
      ASSERT_TRUE(parser.finalize());
      EXPECT_EQ(noteWords, 1u);
      EXPECT_EQ(referenceWords, 1u);
      EXPECT_EQ(bodyWords, 2u);
    }
  }
}

TEST_F(FootnoteNavigationTest, FullMultiParagraphNoteSurvivesGatherPaginationAndSerialization) {
  auto book = std::make_shared<Epub>(CORPUS_DIR "/../fixtures/inline_footnote_full.epub", work.string());
  ASSERT_TRUE(book->load(true));
  ASSERT_TRUE(FootnotePreviews::gather(*book));
  FootnotePreviews::Lookup lookup;
  ASSERT_TRUE(lookup.open(book->getCachePath(), book.get(), 0));
  std::string note;
  ASSERT_TRUE(lookup.find("notes.xhtml#fn-long", note));
  EXPECT_GT(note.size(), 6000u);
  EXPECT_NE(note.find("FIRST_PARAGRAPH"), std::string::npos);
  EXPECT_NE(note.find("MIDDLE_PARAGRAPH"), std::string::npos);
  EXPECT_NE(note.find("LAST_PARAGRAPH"), std::string::npos);
  EXPECT_NE(note.find("LONG_NOTE_END"), std::string::npos);
  EXPECT_EQ(note.find("[7]"), std::string::npos);
  ASSERT_TRUE(lookup.find("notes.xhtml#fn-year", note));
  EXPECT_EQ(note, "1941 YEAR_START_RETAINED");
  ASSERT_TRUE(lookup.find("notes.xhtml#fn-plain-number", note));
  EXPECT_EQ(note, "1 из 10 PLAIN_NUMBER_RETAINED");
  ASSERT_TRUE(lookup.find("notes.xhtml#fn-three-digit", note));
  EXPECT_EQ(note, "THREE_DIGIT_LABEL_REMOVED");
  ASSERT_TRUE(lookup.find("notes.xhtml#fn-grouped-number", note));
  EXPECT_EQ(note, "1,234 человека GROUPED_NUMBER_RETAINED");
  ASSERT_TRUE(lookup.find("notes.xhtml#fn-inline-label", note));
  EXPECT_EQ(note, "NO_SPACE_MARKER_REMOVED");

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
  size_t numberOne = 0;
  size_t number123 = 0;
  size_t number1234 = 0;
  size_t numberNine = 0;
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
        if (digits == "1") ++numberOne;
        if (digits == "123") ++number123;
        if (digits == "1234") ++number1234;
        if (digits == "9") ++numberNine;
        if (token == "BEFORE" || token == "AFTER" || token == "SECOND" || token == "END")
          EXPECT_EQ(static_cast<uint8_t>(block.wordStyle(word)) & EpdFontFamily::ITALIC, 0) << token;
      }
    }
  }
  EXPECT_EQ(numberSeven, 1u) << text;
  EXPECT_EQ(references, 7u);
  EXPECT_EQ(number1234, 2u) << text;
  EXPECT_EQ(numberNine, 1u) << text;
  EXPECT_EQ(number123, 1u) << text;
  // A bare number in note prose is not a duplicate label, even when it matches the reference.
  EXPECT_EQ(numberOne, 2u) << text;
  // Every UTF-8 word must survive cache-write and read chunk boundaries.
  for (const std::string word : {"пояснение", "подробность", "завершение"}) {
    size_t count = 0;
    for (size_t pos = text.find(word); pos != std::string::npos; pos = text.find(word, pos + word.size())) ++count;
    EXPECT_EQ(count, 120u) << word;
  }
  EXPECT_NE(text.find("LONG_NOTE_END"), std::string::npos);
  EXPECT_NE(text.find("1941"), std::string::npos);
  EXPECT_NE(text.find("YEAR_START_RETAINED"), std::string::npos);
  EXPECT_NE(text.find("PLAIN_NUMBER_RETAINED"), std::string::npos);
  EXPECT_NE(text.find("THREE_DIGIT_LABEL_REMOVED"), std::string::npos);
  EXPECT_NE(text.find("GROUPED_NUMBER_RETAINED"), std::string::npos);
  EXPECT_NE(text.find("NO_SPACE_MARKER_REMOVED"), std::string::npos);
  EXPECT_NE(text.find("AFTER"), std::string::npos);
  EXPECT_NE(text.find("END"), std::string::npos);
  EXPECT_LT(text.find("FIRST_PARAGRAPH"), text.find("MIDDLE_PARAGRAPH"));
  EXPECT_LT(text.find("MIDDLE_PARAGRAPH"), text.find("LAST_PARAGRAPH"));
  EXPECT_NE(text.find("пояснение MIDDLE_PARAGRAPH"), std::string::npos);
  EXPECT_NE(text.find("подробность LAST_PARAGRAPH"), std::string::npos);
  EXPECT_LT(text.find("LONG_NOTE_END"), text.find("AFTER"));
}

TEST_F(FootnoteNavigationTest, StreamingNotesRejectConsumerAbortAndCancellationWithoutPoisoningCache) {
  Epub book(CORPUS_DIR "/../fixtures/inline_footnote_full.epub", work.string());
  ASSERT_TRUE(book.load(true));
  ASSERT_TRUE(FootnotePreviews::gather(book));
  FootnotePreviews::Lookup lookup;
  ASSERT_TRUE(lookup.open(book.getCachePath(), &book, 0));
  size_t callbacks = 0;
  EXPECT_FALSE(lookup.stream("notes.xhtml#fn-long", [&](const char*, size_t) {
    ++callbacks;
    return false;
  }));
  EXPECT_EQ(callbacks, 1u);

  CooperativeAbort::clearAborted();
  CooperativeAbort::setLongTaskAbortPredicate([] { return true; });
  const bool completed = lookup.stream("notes.xhtml#fn-long", [&](const char*, size_t) {
    ++callbacks;
    return true;
  });
  CooperativeAbort::setLongTaskAbortPredicate(nullptr);
  EXPECT_FALSE(completed);
  EXPECT_EQ(callbacks, 1u);
  EXPECT_TRUE(CooperativeAbort::consumeAborted());

  std::string streamed;
  EXPECT_TRUE(lookup.stream("notes.xhtml#fn-long", [&](const char* data, size_t size) {
    ++callbacks;
    EXPECT_LE(size, 512u);  // A long note must not need a whole-note heap allocation.
    // Every callback must end at a UTF-8 boundary, including SD read boundaries.
    for (size_t i = 0; i < size;) {
      const auto lead = static_cast<unsigned char>(data[i]);
      const size_t bytes = lead < 0x80 ? 1 : (lead & 0xe0) == 0xc0 ? 2 : (lead & 0xf0) == 0xe0 ? 3 : 4;
      EXPECT_LE(i + bytes, size);
      if (i + bytes > size) return false;
      for (size_t j = 1; j < bytes; ++j) EXPECT_EQ(static_cast<unsigned char>(data[i + j]) & 0xc0, 0x80);
      i += bytes;
    }
    streamed.append(data, size);
    return true;
  }));
  EXPECT_GT(callbacks, 2u);
  EXPECT_NE(streamed.find("LONG_NOTE_END"), std::string::npos);
  std::string found;
  ASSERT_TRUE(lookup.find("notes.xhtml#fn-long", found));
  EXPECT_EQ(streamed, found);
  EXPECT_TRUE(FootnotePreviews::cacheExists(book.getCachePath()));
}

TEST_F(FootnoteNavigationTest, BoundedPopupPreviewDoesNotLimitFullInlineStreaming) {
  Epub book(CORPUS_DIR "/../fixtures/inline_footnote_full.epub", work.string());
  ASSERT_TRUE(book.load(true));
  ASSERT_TRUE(FootnotePreviews::gather(book));
  FootnotePreviews::Lookup lookup;
  ASSERT_TRUE(lookup.open(book.getCachePath(), &book, 0));
  for (const size_t maxBytes : {1u, 239u, 240u, 241u, 255u, 256u, 257u}) {
    std::string preview;
    ASSERT_TRUE(lookup.preview("notes.xhtml#fn-long", preview, maxBytes));
    EXPECT_LE(preview.size(), maxBytes + 3);
    EXPECT_TRUE(preview.ends_with("..."));
    const std::string prefix = preview.substr(0, preview.size() - 3);
    for (size_t i = 0; i < prefix.size();) {
      const auto lead = static_cast<unsigned char>(prefix[i]);
      const size_t bytes = lead < 0x80 ? 1 : (lead & 0xe0) == 0xc0 ? 2 : (lead & 0xf0) == 0xe0 ? 3 : 4;
      ASSERT_LE(i + bytes, prefix.size());
      for (size_t j = 1; j < bytes; ++j) EXPECT_EQ(static_cast<unsigned char>(prefix[i + j]) & 0xc0, 0x80);
      i += bytes;
    }
    std::string full;
    ASSERT_TRUE(lookup.find("notes.xhtml#fn-long", full));
    EXPECT_TRUE(full.starts_with(prefix));
    EXPECT_NE(full.find("LONG_NOTE_END"), std::string::npos);
    EXPECT_FALSE(lookup.failed());
    EXPECT_TRUE(FootnotePreviews::cacheExists(book.getCachePath()));
  }
  for (const size_t maxBytes : {1u, 2u, 3u}) {
    std::string preview;
    ASSERT_TRUE(lookup.preview("notes.xhtml#fn-cyrillic", preview, maxBytes));
    EXPECT_EQ(preview, maxBytes == 1 ? "..." : "Р...");
  }
  std::string shortNote;
  ASSERT_TRUE(lookup.preview("notes.xhtml#fn-year", shortNote));
  EXPECT_EQ(shortNote, "1941 YEAR_START_RETAINED");
}

TEST_F(FootnoteNavigationTest, InvalidNoteBlobLengthIsRejectedAndCanBeRegathered) {
  Epub book(CORPUS_DIR "/../fixtures/inline_footnote_full.epub", work.string());
  ASSERT_TRUE(book.load(true));
  ASSERT_TRUE(FootnotePreviews::gather(book));
  const auto path = book.getCachePath() + FootnotePreviews::CACHE_FILENAME;
  {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    ASSERT_TRUE(file);
    file.seekp(12);  // The first blob's length, immediately after the cache header.
    const char impossibleLength[4] = {static_cast<char>(0xff), static_cast<char>(0xff), static_cast<char>(0xff),
                                      static_cast<char>(0xff)};
    file.write(impossibleLength, sizeof(impossibleLength));
    ASSERT_TRUE(file);
  }
  {
    FootnotePreviews::Lookup broken;
    ASSERT_TRUE(broken.open(book.getCachePath(), &book, 0));
    bool called = false;
    EXPECT_FALSE(broken.stream("notes.xhtml#fn-long", [&](const char*, size_t) {
      called = true;
      return true;
    }));
    EXPECT_FALSE(called);
    EXPECT_TRUE(broken.failed());
    EXPECT_FALSE(FootnotePreviews::cacheExists(book.getCachePath()));
  }
  ASSERT_TRUE(FootnotePreviews::gather(book));
  FootnotePreviews::Lookup repaired;
  ASSERT_TRUE(repaired.open(book.getCachePath(), &book, 0));
  std::string note;
  ASSERT_TRUE(repaired.find("notes.xhtml#fn-long", note));
  EXPECT_NE(note.find("LONG_NOTE_END"), std::string::npos);
}

TEST_F(FootnoteNavigationTest, CorruptFullNoteCannotPublishPreviewEnabledSection) {
  auto book = std::make_shared<Epub>(CORPUS_DIR "/../fixtures/inline_footnote_full_after_pages.epub", work.string());
  ASSERT_TRUE(book->load(true));
  ASSERT_TRUE(FootnotePreviews::gather(*book));
  {
    std::fstream file(book->getCachePath() + FootnotePreviews::CACHE_FILENAME,
                      std::ios::binary | std::ios::in | std::ios::out);
    ASSERT_TRUE(file);
    file.seekp(12);
    const uint32_t impossibleLength = UINT32_MAX;
    file.write(reinterpret_cast<const char*>(&impossibleLength), sizeof(impossibleLength));
    ASSERT_TRUE(file);
  }
  GfxRenderer renderer;
  Section::BuildParams params;
  params.fontId = 1;
  params.viewportWidth = 460;
  params.viewportHeight = 760;
  params.inlineFootnotePreviews = true;
  {
    Section broken(book, 0, renderer);
    ASSERT_FALSE(broken.loadSectionFile(params));
    EXPECT_FALSE(broken.createSectionFile(params, {}, true));
    EXPECT_GT(broken.pageCount, 0u);
  }
  EXPECT_FALSE(FootnotePreviews::cacheExists(book->getCachePath()));
  ASSERT_TRUE(FootnotePreviews::gather(*book));
  Section recovered(book, 0, renderer);
  EXPECT_FALSE(recovered.loadSectionFile(params));
  EXPECT_TRUE(recovered.createSectionFile(params, {}, true));
  EXPECT_TRUE(recovered.loadSectionFile(params));
}

TEST_F(FootnoteNavigationTest, CancelledFullNoteAfterCompletedPagesCannotPublishPartialSection) {
  auto book = std::make_shared<Epub>(CORPUS_DIR "/../fixtures/inline_footnote_full_after_pages.epub", work.string());
  ASSERT_TRUE(book->load(true));
  ASSERT_TRUE(FootnotePreviews::gather(*book));
  GfxRenderer renderer;
  Section::BuildParams params;
  params.fontId = 1;
  params.viewportWidth = 460;
  params.viewportHeight = 760;
  params.inlineFootnotePreviews = true;
  {
    Section broken(book, 0, renderer);
    ASSERT_FALSE(broken.loadSectionFile(params));
    static Section* activeSection = nullptr;
    static size_t checksAfterCompletedPage = 0;
    activeSection = &broken;
    checksAfterCompletedPage = 0;
    CooperativeAbort::clearAborted();
    CooperativeAbort::setLongTaskAbortPredicate(
        [] { return activeSection->pageCount > 0 && ++checksAfterCompletedPage == 4; });
    const bool completed = broken.createSectionFile(params, {}, true);
    CooperativeAbort::setLongTaskAbortPredicate(nullptr);
    EXPECT_FALSE(completed);
    EXPECT_GT(broken.pageCount, 0u);
    EXPECT_EQ(checksAfterCompletedPage, 4u);
    EXPECT_TRUE(CooperativeAbort::consumeAborted());
  }
  EXPECT_TRUE(FootnotePreviews::cacheExists(book->getCachePath()));
  Section recovered(book, 0, renderer);
  EXPECT_FALSE(recovered.loadSectionFile(params));
  EXPECT_TRUE(recovered.createSectionFile(params, {}, true));
  EXPECT_TRUE(recovered.loadSectionFile(params));
}

TEST_F(FootnoteNavigationTest, RepeatedTargetIdsKeepOneBoundedIndexEntryWithLastText) {
  Epub book(CORPUS_DIR "/../fixtures/footnote_duplicate_id.epub", work.string());
  ASSERT_TRUE(book.load(true));
  ASSERT_TRUE(FootnotePreviews::gather(book));
  {
    std::ifstream file(book.getCachePath() + FootnotePreviews::CACHE_FILENAME, std::ios::binary);
    ASSERT_TRUE(file);
    file.seekg(6);  // Header count follows the four-byte magic and two-byte version.
    unsigned char count[2] = {};
    file.read(reinterpret_cast<char*>(count), sizeof(count));
    ASSERT_TRUE(file);
    EXPECT_EQ(static_cast<unsigned int>(count[0]) | (static_cast<unsigned int>(count[1]) << 8), 1u);
  }
  FootnotePreviews::Lookup lookup;
  ASSERT_TRUE(lookup.open(book.getCachePath(), &book, 0));
  std::string note;
  ASSERT_TRUE(lookup.find("notes.xhtml#repeated", note));
  EXPECT_EQ(note, "FINAL_DUPLICATE_NOTE");
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

TEST_F(FootnoteNavigationTest, MissingSourceNeverPublishesAnEmptyCache) {
  const auto source = work / "source.epub";
  std::filesystem::copy_file(CORPUS_DIR "/../fixtures/footnote_paths.epub", source);
  Epub book(source.string(), work.string());
  ASSERT_TRUE(book.load(true));
  std::filesystem::remove(source);
  EXPECT_FALSE(FootnotePreviews::gather(book));
  EXPECT_FALSE(FootnotePreviews::cacheExists(book.getCachePath()));
  std::filesystem::copy_file(CORPUS_DIR "/../fixtures/footnote_paths.epub", source);
  ASSERT_TRUE(FootnotePreviews::gather(book));
  EXPECT_TRUE(FootnotePreviews::cacheExists(book.getCachePath()));
}

TEST_F(FootnoteNavigationTest, FailedNoteCaptureDiscardsItsUncommittedCache) {
  const auto source = work / "source.epub";
  std::filesystem::copy_file(CORPUS_DIR "/../fixtures/footnote_paths.epub", source);
  Epub book(source.string(), work.string());
  ASSERT_TRUE(book.load(true));
  bool removed = false;
  EXPECT_FALSE(FootnotePreviews::gather(book, [&](int progress) {
    if (progress == 60 && !removed) {
      std::filesystem::remove(source);
      removed = true;
    }
  }));
  EXPECT_TRUE(removed);
  EXPECT_FALSE(FootnotePreviews::cacheExists(book.getCachePath()));
  EXPECT_FALSE(std::filesystem::exists(book.getCachePath() + FootnotePreviews::CACHE_FILENAME));
  std::filesystem::copy_file(CORPUS_DIR "/../fixtures/footnote_paths.epub", source);
  ASSERT_TRUE(FootnotePreviews::gather(book));
  EXPECT_TRUE(FootnotePreviews::cacheExists(book.getCachePath()));
}

TEST_F(FootnoteNavigationTest, CancelledScanCanRetryWithoutAValidEmptyCache) {
  Epub book(CORPUS_DIR "/../fixtures/footnote_paths.epub", work.string());
  ASSERT_TRUE(book.load(true));
  CooperativeAbort::setLongTaskAbortPredicate([] { return true; });
  const bool gathered = FootnotePreviews::gather(book);
  CooperativeAbort::setLongTaskAbortPredicate(nullptr);
  EXPECT_FALSE(gathered);
  EXPECT_TRUE(CooperativeAbort::consumeAborted());
  EXPECT_FALSE(FootnotePreviews::cacheExists(book.getCachePath()));
  ASSERT_TRUE(FootnotePreviews::gather(book));
  EXPECT_TRUE(FootnotePreviews::cacheExists(book.getCachePath()));
}

TEST_F(FootnoteNavigationTest, FailedGatherFallbackCannotPoisonTheEnabledLayoutVariant) {
  auto book = std::make_shared<Epub>(CORPUS_DIR "/../fixtures/footnote_legacy_name.epub", work.string());
  ASSERT_TRUE(book->load(true));
  ASSERT_FALSE(FootnotePreviews::cacheExists(book->getCachePath()));
  GfxRenderer renderer;
  Section::BuildParams on;
  on.fontId = 1;
  on.viewportWidth = 460;
  on.viewportHeight = 760;
  on.inlineFootnotePreviews = true;
  const auto pageWords = [](Section& section) {
    std::string text;
    for (uint16_t pageIndex = 0; pageIndex < section.pageCount; ++pageIndex) {
      section.currentPage = pageIndex;
      const auto page = section.loadPageFromSectionFile();
      if (!page) continue;
      for (const auto& element : page->elements) {
        if (element->getTag() != TAG_PageLine) continue;
        const auto& block = *static_cast<const PageLine&>(*element).getBlock();
        for (uint16_t word = 0; word < block.wordCount(); ++word) {
          text += block.wordText(word);
          text += ' ';
        }
      }
    }
    return text;
  };
  {
    Section fallback(book, 0, renderer);
    ASSERT_TRUE(fallback.createSectionFile(on, {}, true));
    ASSERT_TRUE(fallback.loadSectionFile(on));
    EXPECT_EQ(pageWords(fallback).find("LEGACY_NAMED_NOTE"), std::string::npos);
    auto off = on;
    off.inlineFootnotePreviews = false;
    Section cachedOff(book, 0, renderer);
    EXPECT_TRUE(cachedOff.loadSectionFile(off));
  }
  ASSERT_TRUE(FootnotePreviews::gather(*book));
  Section recovered(book, 0, renderer);
  EXPECT_FALSE(recovered.loadSectionFile(on));
  ASSERT_TRUE(recovered.createSectionFile(on, {}, true));
  ASSERT_TRUE(recovered.loadSectionFile(on));
  EXPECT_NE(pageWords(recovered).find("LEGACY_NAMED_NOTE"), std::string::npos);
}

TEST_F(FootnoteNavigationTest, ValidEmptyPreviewSourceIsReadableWithoutAnIndex) {
  Epub book(CORPUS_DIR "/test_headings.epub", work.string());
  ASSERT_TRUE(book.load(true));
  ASSERT_TRUE(FootnotePreviews::gather(book));
  ASSERT_TRUE(FootnotePreviews::cacheExists(book.getCachePath()));
  FootnotePreviews::Lookup lookup;
  EXPECT_TRUE(lookup.open(book.getCachePath(), &book, 0));
  EXPECT_FALSE(lookup.isOpen());
  std::string text;
  EXPECT_FALSE(lookup.find("#missing", text));
}

TEST_F(FootnoteNavigationTest, OldAndTruncatedPreviewCachesAreNotAccepted) {
  Epub book(CORPUS_DIR "/../fixtures/footnote_paths.epub", work.string());
  ASSERT_TRUE(book.load(true));
  ASSERT_TRUE(FootnotePreviews::gather(book));
  const auto path = book.getCachePath() + FootnotePreviews::CACHE_FILENAME;
  {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(4);
    const char oldVersion[2] = {2, 0};
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
