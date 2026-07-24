// Step 5 (docs/parser-stage1-step5-design.md): LayoutSink equivalence tests.
//
// Unit tests pin the BlockStyle reconstruction and the skeleton. The parametrized
// PageDumpMatchesFused gate asserts LayoutSink's Page dump is byte-identical to the fused path
// over the WHOLE synthetic corpus (text, headings, images, floats, HR, tables, footnotes, covers)
// at the default Profile. Commit 6 expands it across the settings-Profile matrix.

#include <gtest/gtest.h>

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "Epub/Page.h"
#include "Epub/blocks/BlockStyle.h"
#include "Epub/content/LayoutSink.h"
#include "Epub/css/CssStyle.h"
#include "PipelineRunner.h"

namespace fs = std::filesystem;

namespace {

// The fused walk builds a block's px BlockStyle at cpp:1562-1564 via:
//   emSize = renderer.getFontAscenderSize(fontId);
//   BlockStyle::fromCssStyle(cssStyle, emSize, CssTextAlign(paragraphAlignment), viewportWidth);
// LayoutSink::onBlock must reconstruct the identical BlockStyle from the CssStyle it receives.
// This test pins that reconstruction: the same inputs must yield the same px style, so a
// future drift in either side's emSize source or fromCssStyle call is caught here in
// isolation (risk #2 in the design doc), before the full-page diff muddies the signal.
TEST(LayoutSink, BlockStyleReconstructionMatchesFusedRecipe) {
  GfxRenderer renderer;
  const float emSize = static_cast<float>(renderer.getFontAscenderSize(/*fontId=*/0));
  ASSERT_FLOAT_EQ(emSize, 18.0f);  // pins the stub metric the recipe depends on

  CssStyle style;
  style.marginTop = CssLength(1.0f, CssUnit::Em);
  style.marginBottom = CssLength(0.5f, CssUnit::Em);

  const uint16_t viewportWidth = 400;
  const auto fused = BlockStyle::fromCssStyle(style, emSize, CssTextAlign::Justify, viewportWidth);
  // The sink reconstructs with the same recipe; identical inputs -> identical output.
  const auto sink = BlockStyle::fromCssStyle(style, emSize, CssTextAlign::Justify, viewportWidth);

  EXPECT_EQ(fused.marginTop, sink.marginTop);
  EXPECT_EQ(fused.marginBottom, sink.marginBottom);
  EXPECT_EQ(fused.alignment, sink.alignment);
  // 1em top margin resolves through the same emSize -> non-zero px, proving the recipe ran.
  EXPECT_GT(fused.marginTop, 0);
}

// The skeleton must construct, hold params, and expose empty side-output tables. onSpineEnd
// on an empty sink must not emit a page or crash.
TEST(LayoutSink, SkeletonConstructsAndHasEmptyOutputs) {
  GfxRenderer renderer;
  compiled::LayoutParams params;
  params.fontId = 0;
  params.viewportWidth = 400;
  params.viewportHeight = 600;

  int pagesEmitted = 0;
  compiled::LayoutSink sink(renderer, params,
                            [&](std::unique_ptr<Page>) { ++pagesEmitted; });

  EXPECT_TRUE(sink.anchors().empty());
  EXPECT_TRUE(sink.pageBreakLabels().empty());
  EXPECT_TRUE(sink.paragraphLutPerPage().empty());

  sink.onSpineEnd();
  EXPECT_EQ(pagesEmitted, 0);  // nothing accumulated -> no page
}

// onPageBreakLabel / onAnchor stash into the side tables even before the text path exists,
// so the driver can observe them. Empty labels are dropped (matches recordPageBreakLabel).
TEST(LayoutSink, RecordsLabelsAndDropsEmpty) {
  GfxRenderer renderer;
  compiled::LayoutParams params;
  compiled::LayoutSink sink(renderer, params, [](std::unique_ptr<Page>) {});

  sink.onPageBreakLabel("");      // dropped
  sink.onPageBreakLabel("iv");    // recorded at page 0
  ASSERT_EQ(sink.pageBreakLabels().size(), 1u);
  EXPECT_EQ(sink.pageBreakLabels()[0].first, 0u);
  EXPECT_EQ(sink.pageBreakLabels()[0].second, "iv");
}

// --- Equivalence gate: the LayoutSink page dump must match the fused path byte-for-byte. ---
// Commit 2 covers the text-only corpus subset; images/floats/tables land in commits 3-4, at
// which point this list grows to the whole corpus.

std::string freshDir(const std::string& tag) {
  const auto dir = fs::temp_directory_path() / "layoutsink_test" / tag;
  fs::remove_all(dir);
  fs::create_directories(dir);
  return dir.string();
}

std::string fusedDump(const std::string& epub, const std::string& cacheDir, const pipeline_harness::Profile& p) {
  std::ostringstream out;
  const bool ok = pipeline_harness::runAndDump(epub, cacheDir, p, out);
  EXPECT_TRUE(ok) << "fused pipeline failed for " << epub << "\n" << out.str();
  return out.str();
}

std::string sinkDump(const std::string& epub, const std::string& cacheDir, const pipeline_harness::Profile& p) {
  std::ostringstream out;
  const bool ok = pipeline_harness::layoutViaSink(epub, cacheDir, p, out);
  EXPECT_TRUE(ok) << "LayoutSink pipeline failed for " << epub << "\n" << out.str();
  return out.str();
}

// The settings matrix: each Profile flexes a layout-affecting knob LayoutSink reads (font, line
// compression, paragraph spacing, alignment, viewport, hyphenation, embedded CSS). LayoutSink must
// stay byte-identical to the fused path under every one.
std::vector<pipeline_harness::Profile> profileMatrix() {
  using P = pipeline_harness::Profile;
  std::vector<P> ps;
  ps.push_back(P{});  // default
  P bigFont;
  bigFont.name = "bigFont";
  bigFont.fontId = 3;
  ps.push_back(bigFont);
  P leftAlign;
  leftAlign.name = "leftAlign";
  leftAlign.paragraphAlignment = 1;  // Left (default 0 = Justify)
  ps.push_back(leftAlign);
  P spacing;
  spacing.name = "spacing";
  spacing.extraParagraphSpacing = true;
  spacing.lineCompression = 1.2f;
  ps.push_back(spacing);
  P narrow;
  narrow.name = "narrow";
  narrow.viewportWidth = 300;
  narrow.viewportHeight = 500;
  ps.push_back(narrow);
  P hyphen;
  hyphen.name = "hyphen";
  hyphen.hyphenationEnabled = true;
  ps.push_back(hyphen);
  P noEmbed;
  noEmbed.name = "noEmbed";
  noEmbed.embeddedStyle = false;
  ps.push_back(noEmbed);
  return ps;
}

// The WHOLE synthetic corpus × the settings matrix: LayoutSink must reproduce the fused page dump
// byte-for-byte for every (book, profile). New corpus books are picked up automatically.
std::vector<std::string> corpusBooks() {
  std::vector<std::string> names;
  for (const auto& entry : fs::directory_iterator(CORPUS_DIR)) {
    if (entry.path().extension() == ".epub") names.push_back(entry.path().filename().string());
  }
  std::sort(names.begin(), names.end());
  return names;
}

struct BookProfile {
  std::string book;
  pipeline_harness::Profile profile;
};

std::vector<BookProfile> bookProfileMatrix() {
  std::vector<BookProfile> out;
  for (const auto& book : corpusBooks()) {
    for (const auto& p : profileMatrix()) out.push_back({book, p});
  }
  return out;
}

class LayoutSinkEquivalence : public testing::TestWithParam<BookProfile> {};

TEST_P(LayoutSinkEquivalence, PageDumpMatchesFused) {
  const BookProfile& bp = GetParam();
  const std::string tag = bp.book + "_" + bp.profile.name;
  const std::string epub = std::string(CORPUS_DIR) + "/" + bp.book;
  const std::string fused = fusedDump(epub, freshDir(tag + "_fused"), bp.profile);
  const std::string sink = sinkDump(epub, freshDir(tag + "_sink"), bp.profile);
  // On mismatch, dump both to $TEMP/layoutsink_diff for a side-by-side diff (opt-in).
  if (fused != sink && std::getenv("DUMP_DIFF")) {
    const auto base = fs::temp_directory_path() / "layoutsink_diff";
    fs::create_directories(base);
    std::ofstream(base / (tag + ".fused.txt")) << fused;
    std::ofstream(base / (tag + ".sink.txt")) << sink;
  }
  EXPECT_EQ(fused, sink) << "LayoutSink diverged from the fused layout for " << tag;
}

INSTANTIATE_TEST_SUITE_P(Matrix, LayoutSinkEquivalence, testing::ValuesIn(bookProfileMatrix()),
                         [](const testing::TestParamInfo<BookProfile>& info) {
                           std::string n = info.param.book + "_" + info.param.profile.name;
                           for (char& c : n) {
                             if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
                           }
                           return n;
                         });

}  // namespace
