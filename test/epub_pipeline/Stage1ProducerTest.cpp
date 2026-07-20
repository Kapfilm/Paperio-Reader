// Stage-1 producer tap (docs/stage1-extraction-design.md, step 2): drives a real
// section build with a capturing BlockSink attached and checks the materialized
// compiled::Blocks. The fused layout output is asserted unchanged by the existing
// EpubPipelineTest goldens; this test validates the NEW producer path.

#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "Epub.h"
#include "Epub/Section.h"
#include "Epub/content/BlockSink.h"
#include "Epub/content/CompiledContent.h"

namespace fs = std::filesystem;

namespace {

// Records every block (with its resolved CssStyle) the walk emits.
struct CapturingSink : compiled::BlockSink {
  struct Captured {
    compiled::Block block;
    CssStyle style;
  };
  std::vector<Captured> blocks;
  int spineEnds = 0;

  void onBlock(compiled::Block&& b, const CssStyle& s) override { blocks.push_back({std::move(b), s}); }
  void onAnchor(const std::string&) override {}
  void onChapter(uint8_t, const std::string&) override {}
  void onPageBreakLabel(const std::string&) override {}
  void onFootnote(int, const FootnoteEntry&) override {}
  void onSpineEnd() override { ++spineEnds; }
};

// The i-th word's text within a block (words are NUL-terminated back-to-back).
std::string wordText(const compiled::Block& b, size_t i) { return std::string(&b.text[b.words[i].textOff]); }

std::vector<std::string> allWords(const compiled::Block& b) {
  std::vector<std::string> out;
  for (size_t i = 0; i < b.words.size(); ++i) out.push_back(wordText(b, i));
  return out;
}

std::string freshCacheDir(const std::string& tag) {
  const auto dir = fs::temp_directory_path() / "stage1_producer_test" / tag;
  fs::remove_all(dir);
  fs::create_directories(dir);
  return dir.string();
}

// Build spine 0 of the given corpus book with a capturing sink attached.
void compileSpine0(const std::string& epubName, const std::string& cacheDir, CapturingSink& sink) {
  GfxRenderer renderer;
  auto epub = std::make_shared<Epub>(std::string(CORPUS_DIR) + "/" + epubName, cacheDir);
  ASSERT_TRUE(epub->load(true));
  epub->loadImageManifest();

  Section section(epub, 0, renderer);
  section.setStage1Sink(&sink);
  // Default profile (matches EpubPipelineTest's golden profile).
  ASSERT_TRUE(section.createSectionFile(/*fontId=*/0, /*lineCompression=*/1.0f, /*extraParagraphSpacing=*/false,
                                        /*paragraphAlignment=*/0, /*viewportWidth=*/300, /*viewportHeight=*/400,
                                        /*hyphenationEnabled=*/false, /*embeddedStyle=*/true,
                                        /*bionicReadingEnabled=*/false, /*inlineFootnotePreviews=*/false,
                                        /*imageRendering=*/0, {}, /*skipEviction=*/true, {}));
}

}  // namespace

TEST(Stage1Producer, EmitsBlocksForHeadings) {
  CapturingSink sink;
  compileSpine0("test_headings.epub", freshCacheDir("headings"), sink);

  EXPECT_EQ(sink.spineEnds, 1) << "onSpineEnd must fire exactly once per spine build";
  ASSERT_GE(sink.blocks.size(), 4u) << "test_headings has several headings + paragraphs";

  // The first block is the h1 heading (golden: 'H1 Heading (default multiplier 1.6x)').
  EXPECT_EQ(allWords(sink.blocks[0].block),
            (std::vector<std::string>{"H1", "Heading", "(default", "multiplier", "1.6x)"}));

  // Structural invariants across every captured block.
  uint32_t prevCharOffset = 0;
  for (const auto& cap : sink.blocks) {
    const auto& b = cap.block;
    EXPECT_EQ(b.type, compiled::BlockType::Text);
    ASSERT_FALSE(b.words.empty()) << "empty blocks are dropped, never emitted";
    EXPECT_FALSE(b.text.empty());
    // First word of a block starts a new paragraph — never an attach-to-previous.
    EXPECT_EQ(b.words[0].styleSpan & compiled::kSpanAttachPrev, 0);
    for (const auto& w : b.words) {
      ASSERT_LT(w.textOff, b.text.size());
      EXPECT_NE(b.text[w.textOff], '\0') << "each word points at non-empty NUL-terminated text";
    }
    EXPECT_GE(b.charOffset, prevCharOffset) << "char offsets are monotonic in document order";
    prevCharOffset = b.charOffset;
  }
}

TEST(Stage1Producer, IsDeterministic) {
  CapturingSink a;
  CapturingSink b;
  compileSpine0("test_headings.epub", freshCacheDir("det_a"), a);
  compileSpine0("test_headings.epub", freshCacheDir("det_b"), b);

  ASSERT_EQ(a.blocks.size(), b.blocks.size());
  for (size_t i = 0; i < a.blocks.size(); ++i) {
    EXPECT_EQ(a.blocks[i].block.text, b.blocks[i].block.text) << "block " << i << " text";
    EXPECT_EQ(a.blocks[i].block.charOffset, b.blocks[i].block.charOffset) << "block " << i << " charOffset";
    ASSERT_EQ(a.blocks[i].block.words.size(), b.blocks[i].block.words.size());
    for (size_t w = 0; w < a.blocks[i].block.words.size(); ++w) {
      EXPECT_EQ(a.blocks[i].block.words[w].styleSpan, b.blocks[i].block.words[w].styleSpan);
      EXPECT_EQ(a.blocks[i].block.words[w].sizePct, b.blocks[i].block.words[w].sizePct);
    }
  }
}
