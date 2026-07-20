// Stage-1 producer tap (docs/stage1-extraction-design.md, step 2): drives a real
// section build with a capturing BlockSink attached and checks the materialized
// compiled::Blocks. The fused layout output is asserted unchanged by the existing
// EpubPipelineTest goldens; this test validates the NEW producer path.

#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "Epub.h"
#include "Epub/Page.h"
#include "Epub/Section.h"
#include "Epub/blocks/TextBlock.h"
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
  struct Chapter {
    uint8_t level;
    std::string title;
    size_t blockIndex;  // block emitted immediately before this onChapter
  };
  struct Anchor {
    std::string id;
    size_t blockIndex;  // block this anchor introduces (sink block count at emit time)
  };
  std::vector<Captured> blocks;
  std::vector<Chapter> chapters;
  std::vector<Anchor> anchors;
  int spineEnds = 0;

  void onBlock(compiled::Block&& b, const CssStyle& s) override { blocks.push_back({std::move(b), s}); }
  void onAnchor(const std::string& id) override { anchors.push_back({id, blocks.size()}); }
  void onChapter(uint8_t level, const std::string& title) override {
    chapters.push_back({level, title, blocks.empty() ? 0 : blocks.size() - 1});
  }
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

// Words joined honoring the attach-to-previous bit (matches the producer's title build).
std::string joinWords(const compiled::Block& b) {
  std::string s;
  for (size_t i = 0; i < b.words.size(); ++i) {
    if (i != 0 && (b.words[i].styleSpan & compiled::kSpanAttachPrev) == 0) s.push_back(' ');
    s += wordText(b, i);
  }
  return s;
}

std::string freshCacheDir(const std::string& tag) {
  const auto dir = fs::temp_directory_path() / "stage1_producer_test" / tag;
  fs::remove_all(dir);
  fs::create_directories(dir);
  return dir.string();
}

// Build the given spine of a corpus book with a capturing sink attached.
void compileSpine(const std::string& epubName, int spineIndex, const std::string& cacheDir, CapturingSink& sink) {
  GfxRenderer renderer;
  auto epub = std::make_shared<Epub>(std::string(CORPUS_DIR) + "/" + epubName, cacheDir);
  ASSERT_TRUE(epub->load(true));
  epub->loadImageManifest();

  Section section(epub, spineIndex, renderer);
  section.setStage1Sink(&sink);
  // Default profile (matches EpubPipelineTest's golden profile).
  ASSERT_TRUE(section.createSectionFile(/*fontId=*/0, /*lineCompression=*/1.0f, /*extraParagraphSpacing=*/false,
                                        /*paragraphAlignment=*/0, /*viewportWidth=*/300, /*viewportHeight=*/400,
                                        /*hyphenationEnabled=*/false, /*embeddedStyle=*/true,
                                        /*bionicReadingEnabled=*/false, /*inlineFootnotePreviews=*/false,
                                        /*imageRendering=*/0, {}, /*skipEviction=*/true, {}));
}

void compileSpine0(const std::string& epubName, const std::string& cacheDir, CapturingSink& sink) {
  compileSpine(epubName, 0, cacheDir, sink);
}

// The producer's text-word sequence for a spine (image blocks contribute no words).
std::vector<std::string> producerWords(const CapturingSink& sink) {
  std::vector<std::string> out;
  for (const auto& cap : sink.blocks) {
    if (cap.block.type != compiled::BlockType::Text) continue;
    for (size_t i = 0; i < cap.block.words.size(); ++i) out.push_back(wordText(cap.block, i));
  }
  return out;
}

// The layout's word sequence for a spine, read back from the built section cache (cache
// hit — the build already ran in compileSpine with the same cacheDir + default profile).
std::vector<std::string> layoutWords(const std::string& epubName, int spineIndex, const std::string& cacheDir) {
  std::vector<std::string> out;
  GfxRenderer renderer;
  auto epub = std::make_shared<Epub>(std::string(CORPUS_DIR) + "/" + epubName, cacheDir);
  EXPECT_TRUE(epub->load(true));
  epub->loadImageManifest();
  Section section(epub, spineIndex, renderer);
  if (!section.loadSectionFile(0, 1.0f, false, 0, 300, 400, false, true, false, false, 0)) return out;
  for (uint16_t p = 0; p < section.pageCount; ++p) {
    section.currentPage = p;
    const auto page = section.loadPageFromSectionFile();
    if (!page) break;
    for (const auto& el : page->elements) {
      if (el->getTag() != TAG_PageLine) continue;
      const auto& block = *static_cast<const PageLine&>(*el).getBlock();
      for (uint16_t w = 0; w < block.wordCount(); ++w) out.emplace_back(block.wordText(w));
    }
  }
  return out;
}

// Find the spine index whose href contains `needle`.
int spineIndexForHref(const std::string& epubName, const std::string& cacheDir, const std::string& needle) {
  auto epub = std::make_shared<Epub>(std::string(CORPUS_DIR) + "/" + epubName, cacheDir);
  EXPECT_TRUE(epub->load(true));
  for (int i = 0; i < epub->getSpineItemsCount(); ++i) {
    if (epub->getSpineItem(i).href.find(needle) != std::string::npos) return i;
  }
  return -1;
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

TEST(Stage1Producer, EmitsChaptersForHeadings) {
  CapturingSink sink;
  compileSpine0("test_headings.epub", freshCacheDir("chapters"), sink);

  ASSERT_FALSE(sink.chapters.empty()) << "test_headings has headings";

  // First chapter is the h1 (level 1) with the heading's text as title.
  EXPECT_EQ(sink.chapters[0].level, 1);
  EXPECT_EQ(sink.chapters[0].title, "H1 Heading (default multiplier 1.6x)");

  // Every chapter references a heading block: the block carries kStartsChapter and its
  // joined text equals the chapter title. Chapters and heading-blocks are 1:1.
  size_t headingBlocks = 0;
  for (const auto& cap : sink.blocks) {
    if (cap.block.flags & compiled::kStartsChapter) ++headingBlocks;
  }
  EXPECT_EQ(headingBlocks, sink.chapters.size());
  for (const auto& ch : sink.chapters) {
    ASSERT_LT(ch.blockIndex, sink.blocks.size());
    const auto& b = sink.blocks[ch.blockIndex].block;
    EXPECT_NE(b.flags & compiled::kStartsChapter, 0) << "chapter must point at a heading block";
    EXPECT_GE(ch.level, 1);
    EXPECT_LE(ch.level, 6);
    EXPECT_EQ(ch.title, joinWords(b));
  }
}

TEST(Stage1Producer, EmitsAnchorsForContentIds) {
  const std::string cacheDir = freshCacheDir("anchors");
  // frontmatter.xhtml carries <h2 id="dedication">, id="epigraph", id="foreword".
  const int spine = spineIndexForHref("test_spine_toc_edges.epub", freshCacheDir("anchors_find"), "frontmatter");
  ASSERT_GE(spine, 0);

  CapturingSink sink;
  compileSpine("test_spine_toc_edges.epub", spine, cacheDir, sink);

  // Collect the emitted anchor ids.
  std::vector<std::string> ids;
  for (const auto& a : sink.anchors) ids.push_back(a.id);
  for (const char* want : {"dedication", "epigraph", "foreword"}) {
    EXPECT_NE(std::find(ids.begin(), ids.end(), want), ids.end()) << "missing anchor id: " << want;
  }

  // Each anchor introduces a real block, and its target block's first word starts the
  // heading text (the id sits on the <h2>). 'dedication' -> the "Dedication" heading block.
  for (const auto& a : sink.anchors) {
    ASSERT_LE(a.blockIndex, sink.blocks.size());
    if (a.blockIndex < sink.blocks.size() && a.id == "dedication") {
      EXPECT_EQ(wordText(sink.blocks[a.blockIndex].block, 0), "Dedication");
    }
  }
}

TEST(Stage1Producer, EmitsImageBlocks) {
  // chapter2.xhtml embeds <img src="images/png_format.png">.
  const int spine = spineIndexForHref("test_png_images.epub", freshCacheDir("img_find"), "chapter2");
  ASSERT_GE(spine, 0);
  CapturingSink sink;
  compileSpine("test_png_images.epub", spine, freshCacheDir("images"), sink);

  size_t imageBlocks = 0;
  bool sawPngFormat = false;
  for (const auto& cap : sink.blocks) {
    const auto& b = cap.block;
    if (b.type != compiled::BlockType::Image) continue;
    ++imageBlocks;
    EXPECT_FALSE(b.entryPath.empty()) << "image block must carry an EPUB entry path";
    EXPECT_GT(b.width, 0) << "intrinsic width";
    EXPECT_GT(b.height, 0) << "intrinsic height";
    EXPECT_EQ(b.floatSide, 0) << "block images are centered, not floated";
    EXPECT_TRUE(b.words.empty()) << "image blocks carry no text words";
    if (b.entryPath.find("png_format.png") != std::string::npos) sawPngFormat = true;
  }
  EXPECT_GT(imageBlocks, 0u) << "chapter2 has a block image";
  EXPECT_TRUE(sawPngFormat) << "the image block's entryPath is the EPUB path, not the display cache path";
}

// Strong equivalence: the producer's text-word sequence must exactly equal the layout's,
// in reading order, for every construct the producer handles. This is the intermediate gate
// until step 6 (full Stage-1->Stage-2 golden diff). Add (book, href) pairs as producer
// coverage grows; a book with a construct the producer does not yet emit (e.g. table cells)
// would fail here, which is the point.
TEST(Stage1Producer, TextMatchesLayoutWords) {
  struct Case {
    const char* book;
    const char* href;  // spine href fragment (headings, paragraphs, block images, lists)
  };
  const std::vector<Case> cases = {
      {"test_headings.epub", "chapter1"},    // headings + paragraphs + a list
      {"test_font_sizes.epub", "chapter1"},  // inline font-size spans + a list
      {"test_png_images.epub", "chapter2"},  // text around a block image
  };
  for (const auto& c : cases) {
    const int spine = spineIndexForHref(c.book, freshCacheDir(std::string("eqv_find_") + c.book), c.href);
    ASSERT_GE(spine, 0) << c.book << " " << c.href;
    const std::string cacheDir = freshCacheDir(std::string("eqv_") + c.book + "_" + c.href);
    CapturingSink sink;
    compileSpine(c.book, spine, cacheDir, sink);
    EXPECT_EQ(producerWords(sink), layoutWords(c.book, spine, cacheDir))
        << "producer vs layout word mismatch: " << c.book << " " << c.href;
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
