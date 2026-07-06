// WordSizeLayoutTest.cpp
//
// Compiles the REAL ParsedText and TextBlock against a fixed-width GfxRenderer
// shim (see GfxRenderer.h in this directory) and verifies the per-word inline
// font-size channel introduced for size-aware rendering:
//
//   - addWord() clamps and carries sizePct into the TextBlock lines
//   - all-100% lines normalize to an empty wordSizes vector (zero-cost path)
//   - word measurement scales with the word's own size, changing line breaks
//   - hyphenation word splits keep the sizes vector in lockstep
//   - TextBlock serialization round-trips sizes (and stays 1 byte for uniform lines)
//   - render baseline-aligns mixed-size words (smaller words shift down)

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "Epub/ParsedText.h"
#include "Epub/blocks/TextBlock.h"
#include "GfxRenderer.h"

namespace {

constexpr int kFontId = 1;

struct LayoutResult {
  std::vector<std::shared_ptr<TextBlock>> lines;
};

// Lay out the given ParsedText at viewportWidth and collect the emitted lines.
LayoutResult layout(ParsedText& text, const GfxRenderer& renderer, const uint16_t viewportWidth) {
  LayoutResult result;
  text.layoutAndExtractLines(
      renderer, kFontId, viewportWidth,
      [&result](std::shared_ptr<TextBlock> line, bool, bool) {
        result.lines.push_back(std::move(line));
        return ParsedText::LineProcessResult::Accepted;
      },
      /*includeLastLine=*/true);
  return result;
}

// Suppress the automatic one-em paragraph indent so width math in tests stays simple.
BlockStyle noIndentStyle() {
  BlockStyle bs;
  bs.textIndent = 0;
  bs.textIndentDefined = true;
  return bs;
}

}  // namespace

// ---------------------------------------------------------------------------
// Size propagation and normalization
// ---------------------------------------------------------------------------

TEST(WordSizePropagation, SizesReachTextBlockLines) {
  GfxRenderer renderer;
  ParsedText text(/*extraParagraphSpacing=*/false, /*hyphenationEnabled=*/false, noIndentStyle());
  text.addWord("normal", EpdFontFamily::REGULAR);
  text.addWord("small", EpdFontFamily::REGULAR, false, false, 80);
  text.addWord("big", EpdFontFamily::REGULAR, false, false, 150);

  const auto result = layout(text, renderer, 400);
  ASSERT_EQ(result.lines.size(), 1u);
  const auto& line = *result.lines[0];
  ASSERT_TRUE(line.hasWordSizes());
  ASSERT_EQ(line.getWordSizes().size(), 3u);
  EXPECT_EQ(line.getWordSizes()[0], 100);
  EXPECT_EQ(line.getWordSizes()[1], 80);
  EXPECT_EQ(line.getWordSizes()[2], 150);
  EXPECT_EQ(line.maxSizePct(), 150);
}

TEST(WordSizePropagation, AddWordClampsToRange) {
  GfxRenderer renderer;
  ParsedText text(false, false, noIndentStyle());
  text.addWord("tiny", EpdFontFamily::REGULAR, false, false, 10);   // below MIN -> 30
  text.addWord("huge", EpdFontFamily::REGULAR, false, false, 255);  // above MAX -> 250

  const auto result = layout(text, renderer, 800);
  ASSERT_EQ(result.lines.size(), 1u);
  ASSERT_TRUE(result.lines[0]->hasWordSizes());
  EXPECT_EQ(result.lines[0]->getWordSizes()[0], ParsedText::MIN_WORD_SIZE_PCT);
  EXPECT_EQ(result.lines[0]->getWordSizes()[1], ParsedText::MAX_WORD_SIZE_PCT);
}

TEST(WordSizePropagation, UniformLinesNormalizeToEmpty) {
  GfxRenderer renderer;
  ParsedText text(false, false, noIndentStyle());
  text.addWord("all", EpdFontFamily::REGULAR, false, false, 100);
  text.addWord("default", EpdFontFamily::REGULAR);

  const auto result = layout(text, renderer, 400);
  ASSERT_EQ(result.lines.size(), 1u);
  EXPECT_FALSE(result.lines[0]->hasWordSizes());
  EXPECT_EQ(result.lines[0]->maxSizePct(), 100);
}

// ---------------------------------------------------------------------------
// Measurement: an enlarged word takes proportionally more line width
// ---------------------------------------------------------------------------

TEST(WordSizeMeasurement, EnlargedWordForcesEarlierBreak) {
  GfxRenderer renderer;
  // Four 4-glyph words: 4*40 + 3*5 = 175 px -> fits one 175 px line at 100%.
  {
    ParsedText text(false, false, noIndentStyle());
    for (const char* w : {"aaaa", "bbbb", "cccc", "dddd"}) text.addWord(w, EpdFontFamily::REGULAR);
    const auto result = layout(text, renderer, 175);
    EXPECT_EQ(result.lines.size(), 1u);
  }
  // Same words, but one at 200% (80 px instead of 40) no longer fits -> 2 lines.
  {
    ParsedText text(false, false, noIndentStyle());
    text.addWord("aaaa", EpdFontFamily::REGULAR);
    text.addWord("bbbb", EpdFontFamily::REGULAR, false, false, 200);
    text.addWord("cccc", EpdFontFamily::REGULAR);
    text.addWord("dddd", EpdFontFamily::REGULAR);
    const auto result = layout(text, renderer, 175);
    EXPECT_GE(result.lines.size(), 2u);
    // Every emitted line must keep words/sizes vectors in lockstep.
    for (const auto& line : result.lines) {
      if (line->hasWordSizes()) {
        EXPECT_EQ(line->getWordSizes().size(), line->wordCount());
      }
    }
  }
}

TEST(WordSizeMeasurement, ShrunkWordsAllowLaterBreak) {
  GfxRenderer renderer;
  // Five 4-glyph words at 100%: 5*40 + 4*5 = 220 px -> needs 2 lines at 200 px.
  {
    ParsedText text(false, false, noIndentStyle());
    for (const char* w : {"aaaa", "bbbb", "cccc", "dddd", "eeee"}) text.addWord(w, EpdFontFamily::REGULAR);
    const auto result = layout(text, renderer, 200);
    EXPECT_GE(result.lines.size(), 2u);
  }
  // All words at 50% (20 px each): 5*20 + 4*5 = 120 px -> one line.
  {
    ParsedText text(false, false, noIndentStyle());
    for (const char* w : {"aaaa", "bbbb", "cccc", "dddd", "eeee"})
      text.addWord(w, EpdFontFamily::REGULAR, false, false, 50);
    const auto result = layout(text, renderer, 200);
    EXPECT_EQ(result.lines.size(), 1u);
  }
}

// ---------------------------------------------------------------------------
// Word splitting keeps the sizes vector in lockstep
// ---------------------------------------------------------------------------

TEST(WordSizeSplitting, OversizedWordSplitInheritsSize) {
  GfxRenderer renderer;
  ParsedText text(false, /*hyphenationEnabled=*/false, noIndentStyle());
  // 30 glyphs at 50% = 150 px > 100 px viewport -> fallback split must fire.
  text.addWord("abcdefghijklmnopqrstuvwxyzabcd", EpdFontFamily::REGULAR, false, false, 50);

  const auto result = layout(text, renderer, 100);
  ASSERT_GE(result.lines.size(), 2u);
  size_t totalWords = 0;
  for (const auto& line : result.lines) {
    totalWords += line->wordCount();
    ASSERT_TRUE(line->hasWordSizes());
    ASSERT_EQ(line->getWordSizes().size(), line->wordCount());
    for (const uint8_t sz : line->getWordSizes()) {
      EXPECT_EQ(sz, 50);  // every fragment of the split word keeps the original size
    }
  }
  EXPECT_GE(totalWords, 2u);
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

TEST(WordSizeSerialization, RoundTripsMixedSizes) {
  TextBlock original({"one", "two", "three"}, {0, 40, 80},
                     {EpdFontFamily::REGULAR, EpdFontFamily::BOLD, EpdFontFamily::ITALIC}, BlockStyle(),
                     {100, 80, 150});

  FsFile file = HalFile::forReadWrite();
  ASSERT_TRUE(original.serialize(file));
  ASSERT_TRUE(file.seek(0));

  const auto restored = TextBlock::deserialize(file);
  ASSERT_NE(restored, nullptr);
  ASSERT_EQ(restored->wordCount(), 3u);
  ASSERT_TRUE(restored->hasWordSizes());
  EXPECT_EQ(restored->getWordSizes(), (std::vector<uint8_t>{100, 80, 150}));
  EXPECT_EQ(restored->getWords()[1], "two");
}

TEST(WordSizeSerialization, UniformBlockCostsOneFlagByte) {
  TextBlock uniform({"one", "two"}, {0, 40}, {EpdFontFamily::REGULAR, EpdFontFamily::REGULAR}, BlockStyle());
  TextBlock mixed({"one", "two"}, {0, 40}, {EpdFontFamily::REGULAR, EpdFontFamily::REGULAR}, BlockStyle(), {100, 80});

  FsFile uniformFile = HalFile::forReadWrite();
  FsFile mixedFile = HalFile::forReadWrite();
  ASSERT_TRUE(uniform.serialize(uniformFile));
  ASSERT_TRUE(mixed.serialize(mixedFile));
  // Mixed pays the flag byte plus one byte per word; uniform only the flag byte.
  EXPECT_EQ(mixedFile.size(), uniformFile.size() + 2);

  ASSERT_TRUE(uniformFile.seek(0));
  const auto restored = TextBlock::deserialize(uniformFile);
  ASSERT_NE(restored, nullptr);
  EXPECT_FALSE(restored->hasWordSizes());
}

// ---------------------------------------------------------------------------
// Render: baseline alignment of mixed-size words
// ---------------------------------------------------------------------------

TEST(WordSizeRender, MixedSizesBaselineAlign) {
  GfxRenderer renderer;
  // 200% word (ascender 32) next to 100% word (ascender 16): the smaller word's
  // glyph top must shift DOWN by the ascender difference so baselines meet.
  TextBlock line({"BIG", "small"}, {0, 70}, {EpdFontFamily::REGULAR, EpdFontFamily::REGULAR}, BlockStyle(), {200, 100});
  line.render(renderer, kFontId, 0, 100);

  ASSERT_EQ(renderer.drawCalls.size(), 2u);
  const auto& big = renderer.drawCalls[0];
  const auto& small = renderer.drawCalls[1];
  EXPECT_FLOAT_EQ(big.scale, 2.0f);
  EXPECT_FLOAT_EQ(small.scale, 1.0f);
  EXPECT_EQ(big.y, 100);                                                          // tallest word sits at line top
  EXPECT_EQ(small.y, 100 + (2 * GfxRenderer::ASCENDER - GfxRenderer::ASCENDER));  // shifted to shared baseline
}

TEST(WordSizeRender, UniformLineRendersAtLineTop) {
  GfxRenderer renderer;
  TextBlock line({"a", "b"}, {0, 15}, {EpdFontFamily::REGULAR, EpdFontFamily::REGULAR}, BlockStyle());
  line.render(renderer, kFontId, 0, 50);

  ASSERT_EQ(renderer.drawCalls.size(), 2u);
  EXPECT_EQ(renderer.drawCalls[0].y, 50);
  EXPECT_EQ(renderer.drawCalls[1].y, 50);
  EXPECT_FLOAT_EQ(renderer.drawCalls[0].scale, 1.0f);
}

// ---------------------------------------------------------------------------
// Interaction with the block-level multiplier (headings, block font-size)
// ---------------------------------------------------------------------------

TEST(WordSizeRender, BlockMultiplierComposesWithWordSize) {
  GfxRenderer renderer;
  BlockStyle heading;
  heading.fontSizeMultiplier = 1.5f;
  TextBlock line({"word", "small"}, {0, 60}, {EpdFontFamily::REGULAR, EpdFontFamily::REGULAR}, heading, {100, 50});
  line.render(renderer, kFontId, 0, 0);

  ASSERT_EQ(renderer.drawCalls.size(), 2u);
  EXPECT_FLOAT_EQ(renderer.drawCalls[0].scale, 1.5f);   // block multiplier alone
  EXPECT_FLOAT_EQ(renderer.drawCalls[1].scale, 0.75f);  // 1.5 x 50%
}
