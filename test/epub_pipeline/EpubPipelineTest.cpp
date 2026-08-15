// Phase-0 equivalence harness tests (docs/compiled-book-pipeline-plan.md):
//  1. Determinism — two cold runs over the same book produce byte-identical dumps.
//  2. Warm-path equivalence — a run served from the section cache dumps
//     identically to the cold run that built it.
//  3. Golden equivalence — the dump matches the committed golden for every
//     synthetic corpus book. Regenerate intentionally changed goldens with:
//     UPDATE_GOLDENS=1 ctest -R EpubPipeline
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Epub/Page.h"
#include "Epub/parsers/ChapterHtmlSlimParser.h"
#include "GfxRenderer.h"
#include "PipelineRunner.h"

// Defined by ChapterHtmlSlimParser.cpp, which is linked into this full-pipeline target.
bool looksLikeFootnoteAnchor(const std::string& id);
bool parseCompactGenericAnchor(const std::string& id, uint32_t& value);

namespace fs = std::filesystem;

namespace {

TEST(EpubAnchorClassificationTest, GenericConverterIdsAreNotFootnotes) {
  // FB2/EPUB converters commonly assign id123-style anchors to every paragraph.
  // Classifying these as notes forces every paragraph onto a separate rendered page.
  EXPECT_FALSE(looksLikeFootnoteAnchor("id1"));
  EXPECT_FALSE(looksLikeFootnoteAnchor("id17705"));

  EXPECT_TRUE(looksLikeFootnoteAnchor("fn7"));
  EXPECT_TRUE(looksLikeFootnoteAnchor("note_42"));
  EXPECT_TRUE(looksLikeFootnoteAnchor("footnote-3"));
  EXPECT_TRUE(looksLikeFootnoteAnchor("endnote.9"));
  EXPECT_TRUE(looksLikeFootnoteAnchor("filepos123"));
}

TEST(EpubAnchorClassificationTest, CanonicalGenericIdsCanBeStoredCompactlyWithoutChangingTheirText) {
  uint32_t value = 0;
  EXPECT_TRUE(parseCompactGenericAnchor("id17705", value));
  EXPECT_EQ(value, 17705u);
  EXPECT_TRUE(parseCompactGenericAnchor("id0", value));
  EXPECT_EQ(value, 0u);

  EXPECT_FALSE(parseCompactGenericAnchor("id017705", value));
  EXPECT_FALSE(parseCompactGenericAnchor("id17a", value));
  EXPECT_FALSE(parseCompactGenericAnchor("note17", value));
  EXPECT_FALSE(parseCompactGenericAnchor("id4294967296", value));
}

TEST(EpubFootnoteCapacityTest, RetainsReferenceHeavyBibleVerse) {
  Page page;
  for (int i = 0; i < 40; ++i) {
    const std::string number = std::to_string(i + 1);
    const std::string href = "chapter.xhtml#id" + std::to_string(1000 + i);
    page.addFootnote(number.c_str(), href.c_str());
  }

  ASSERT_EQ(page.footnotes.size(), Page::MAX_FOOTNOTES_PER_PAGE);
  EXPECT_EQ(page.footnotes.size(), 32u);
  EXPECT_STREQ(page.footnotes.back().number, "32");
  EXPECT_STREQ(page.footnotes.back().href, "chapter.xhtml#id1031");
}

std::string pageText(const Page& page) {
  std::string text;
  for (const auto& element : page.elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto& block = *static_cast<const PageLine&>(*element).getBlock();
    for (uint16_t i = 0; i < block.wordCount(); ++i) {
      if (!text.empty()) text += ' ';
      text += block.wordText(i);
    }
  }
  return text;
}

TEST(EpubTargetedFootnotePreviewTest, StartsAtRequestedAnchorAndStopsAtPageLimit) {
  GfxRenderer renderer;
  std::vector<std::unique_ptr<Page>> pages;
  const std::string xhtml =
      "<html><body><p>BEFORE MUST NEVER APPEAR</p><div id=\"note2\"><p>SECOND note starts here and contains "
      "enough words to fill several deliberately tiny pages for the preview page limit test.</p><p>More note text "
      "continues after the first paragraph and should still remain inside the bounded preview.</p></div></body></html>";

  ChapterHtmlSlimParser parser(
      nullptr, renderer, 1, 1.0f, false, 0, 90, 48, false, false, false,
      [&](std::unique_ptr<Page> page) { pages.emplace_back(std::move(page)); }, false, "", "", 0, {}, nullptr, nullptr,
      nullptr, "note2", 2);
  ASSERT_TRUE(parser.setup(xhtml.size()));
  ASSERT_EQ(parser.write(reinterpret_cast<const uint8_t*>(xhtml.data()), xhtml.size()), xhtml.size());
  ASSERT_TRUE(parser.finalize());
  ASSERT_TRUE(parser.previewComplete());
  ASSERT_EQ(pages.size(), 2u);

  std::string rendered;
  for (const auto& page : pages) rendered += pageText(*page) + ' ';
  EXPECT_NE(rendered.find("SECOND"), std::string::npos);
  EXPECT_EQ(rendered.find("BEFORE"), std::string::npos);
}

TEST(EpubTargetedFootnotePreviewTest, MissingAnchorFailsWithoutRenderingTheChapterStart) {
  GfxRenderer renderer;
  std::vector<std::unique_ptr<Page>> pages;
  const std::string xhtml = "<html><body><p>Ordinary chapter text</p></body></html>";
  ChapterHtmlSlimParser parser(
      nullptr, renderer, 1, 1.0f, false, 0, 90, 80, false, false, false,
      [&](std::unique_ptr<Page> page) { pages.emplace_back(std::move(page)); }, false, "", "", 0, {}, nullptr, nullptr,
      nullptr, "missing-note", 2);
  ASSERT_TRUE(parser.setup(xhtml.size()));
  ASSERT_EQ(parser.write(reinterpret_cast<const uint8_t*>(xhtml.data()), xhtml.size()), xhtml.size());
  EXPECT_FALSE(parser.finalize());
  EXPECT_TRUE(pages.empty());
}

TEST(EpubTargetedFootnotePreviewTest, GenericSameFileBibleAnchorStartsAtTop) {
  GfxRenderer renderer;
  std::vector<std::unique_ptr<Page>> pages;
  const std::string xhtml =
      "<html><body><p id=\"id100\">Earlier verse must be skipped.</p>"
      "<p id=\"id19645\">Requested Bible verse starts here.</p>"
      "<p id=\"id19644\">Following verse remains available.</p></body></html>";

  ChapterHtmlSlimParser parser(
      nullptr, renderer, 1, 1.0f, false, 0, 240, 120, false, false, false,
      [&](std::unique_ptr<Page> page) { pages.emplace_back(std::move(page)); }, false, "", "", 0, {}, nullptr, nullptr,
      nullptr, "id19645", 3);
  ASSERT_TRUE(parser.setup(xhtml.size()));
  ASSERT_EQ(parser.write(reinterpret_cast<const uint8_t*>(xhtml.data()), xhtml.size()), xhtml.size());
  ASSERT_TRUE(parser.finalize());
  ASSERT_FALSE(pages.empty());

  const std::string rendered = pageText(*pages.front());
  EXPECT_EQ(rendered.find("Earlier"), std::string::npos);
  EXPECT_EQ(rendered.find("Requested"), 0u);
}

std::string freshCacheDir(const std::string& tag) {
  const auto dir = fs::temp_directory_path() / "epub_pipeline_test" / tag;
  fs::remove_all(dir);
  fs::create_directories(dir);
  return dir.string();
}

std::string runOnce(const std::string& epubPath, const std::string& cacheDir) {
  std::ostringstream dump;
  const bool ok = pipeline_harness::runAndDump(epubPath, cacheDir, pipeline_harness::Profile{}, dump);
  EXPECT_TRUE(ok) << "pipeline failed for " << epubPath << "\n" << dump.str();
  return dump.str();
}

std::string stem(const std::string& path) { return fs::path(path).stem().string(); }

class EpubPipelineTest : public testing::TestWithParam<std::string> {};

TEST_P(EpubPipelineTest, ColdRunsAreDeterministic) {
  const std::string epub = GetParam();
  const std::string dump1 = runOnce(epub, freshCacheDir(stem(epub) + "_a"));
  const std::string dump2 = runOnce(epub, freshCacheDir(stem(epub) + "_b"));
  EXPECT_EQ(dump1, dump2) << "two cold builds of " << epub << " diverged";
}

TEST_P(EpubPipelineTest, WarmRunMatchesColdRun) {
  const std::string epub = GetParam();
  const std::string cacheDir = freshCacheDir(stem(epub) + "_warm");
  const std::string cold = runOnce(epub, cacheDir);
  const std::string warm = runOnce(epub, cacheDir);  // same cacheDir: cache-hit path
  EXPECT_EQ(cold, warm) << "cache-served layout of " << epub << " differs from the build that wrote it";
}

TEST_P(EpubPipelineTest, MatchesGolden) {
  const std::string epub = GetParam();
  const std::string dump = runOnce(epub, freshCacheDir(stem(epub) + "_golden"));
  const fs::path goldenPath = fs::path(GOLDEN_DIR) / (stem(epub) + ".golden.txt");

  if (std::getenv("UPDATE_GOLDENS")) {
    std::ofstream(goldenPath) << dump;
    GTEST_SKIP() << "golden regenerated: " << goldenPath;
  }
  std::ifstream in(goldenPath);
  ASSERT_TRUE(in) << "missing golden " << goldenPath << " — run with UPDATE_GOLDENS=1 to create it";
  std::stringstream golden;
  golden << in.rdbuf();
  EXPECT_EQ(golden.str(), dump) << "layout drift vs golden for " << epub
                                << " — if intentional, regenerate with UPDATE_GOLDENS=1 and explain in the commit";
}

std::vector<std::string> corpusEpubs() {
  std::vector<std::string> paths;
  for (const auto& entry : fs::directory_iterator(CORPUS_DIR)) {
    if (entry.path().extension() == ".epub") paths.push_back(entry.path().string());
  }
  std::sort(paths.begin(), paths.end());
  return paths;
}

INSTANTIATE_TEST_SUITE_P(SyntheticCorpus, EpubPipelineTest, testing::ValuesIn(corpusEpubs()),
                         [](const testing::TestParamInfo<std::string>& info) {
                           std::string name = stem(info.param);
                           for (char& c : name)
                             if (!isalnum(static_cast<unsigned char>(c))) c = '_';
                           return name;
                         });

}  // namespace
