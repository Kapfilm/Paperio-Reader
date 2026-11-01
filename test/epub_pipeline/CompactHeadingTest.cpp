#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>

#include "Epub/Page.h"
#include "Epub/parsers/ChapterHtmlSlimParser.h"
#include "GfxRenderer.h"

namespace {

struct HeadingWord {
  RenderStyle block;
  uint8_t sizePct;
  EpdFontFamily::Style style;
};

// Read the actual layout product, including the per-word size channel. A compact
// BlockStyle alone is insufficient if publisher spans enlarge every heading word.
std::map<std::string, HeadingWord> parseHeadingWords(const std::string& body, bool embeddedStyle = true,
                                                     const FontSizeLadder& ladder = {}) {
  GfxRenderer renderer;
  CssParser css("");
  std::map<std::string, HeadingWord> words;
  const std::string html = "<html><body>" + body + "</body></html>";
  ChapterHtmlSlimParser parser(
      nullptr, renderer, 1, 1.0f, false, 0, 460, 760, false, false, false,
      [&](std::unique_ptr<Page> page) {
        for (const auto& element : page->elements) {
          if (element->getTag() != TAG_PageLine) continue;
          const auto& block = *static_cast<const PageLine&>(*element).getBlock();
          for (uint16_t i = 0; i < block.wordCount(); ++i) {
            words.emplace(block.wordText(i),
                          HeadingWord{block.getRenderStyle(), block.wordSizePct(i), block.wordStyle(i)});
          }
        }
      },
      embeddedStyle, "", "", 0, {}, nullptr, &css);
  parser.setFontSizeLadder(ladder);
  EXPECT_TRUE(parser.setup(html.size()));
  EXPECT_EQ(parser.write(reinterpret_cast<const uint8_t*>(html.data()), html.size()), html.size());
  EXPECT_TRUE(parser.finalize());
  return words;
}

TEST(CompactHeadingTest, EverySemanticLevelUsesCompactScaleWithAndWithoutEmbeddedStyles) {
  const float expected[] = {1.15f, 1.08f, 1.0f, 1.0f, 1.0f, 1.0f};
  for (const bool embeddedStyle : {false, true}) {
    for (const bool publisherSize : {false, true}) {
      SCOPED_TRACE(::testing::Message() << "embedded=" << embeddedStyle << " publisher=" << publisherSize);
      std::string body;
      for (int level = 1; level <= 6; ++level) {
        const std::string tag = "h" + std::to_string(level);
        body += "<" + tag + (publisherSize ? " style=\"font-size:3em\"" : "") + ">HEAD" + std::to_string(level) + "</" +
                tag + ">";
      }
      const auto words = parseHeadingWords(body, embeddedStyle);
      ASSERT_EQ(words.size(), 6u);
      for (int level = 1; level <= 6; ++level) {
        const auto& word = words.at("HEAD" + std::to_string(level));
        EXPECT_FLOAT_EQ(word.block.fontSizeMultiplier, expected[level - 1]);
        EXPECT_EQ(word.block.headingFontId, 0);
        EXPECT_EQ(word.block.alignment, CssTextAlign::Center);
        EXPECT_NE(static_cast<uint8_t>(word.style) & EpdFontFamily::BOLD, 0);
        EXPECT_EQ(word.sizePct, 100);
      }
    }
  }
}

TEST(CompactHeadingTest, EnlargedInlineAndAncestorStylesCannotUndoCompactHeading) {
  const auto words = parseHeadingWords(
      "<div style=\"font-size:2em\"><h1 style=\"font-size:3em\">"
      "PLAIN <span style=\"font-size:2em\">SPAN <big>BIG</big></span> "
      "<span style=\"float:left;font-size:4em\">FLOAT</span>"
      "</h1></div>");
  for (const char* text : {"PLAIN", "SPAN", "BIG", "FLOAT"}) {
    SCOPED_TRACE(text);
    ASSERT_EQ(words.count(text), 1u);
    EXPECT_FLOAT_EQ(words.at(text).block.fontSizeMultiplier, 1.15f);
    EXPECT_EQ(words.at(text).sizePct, 100);
  }
}

TEST(CompactHeadingTest, SmallerAndSuperscriptWordsSurviveWithoutLeakingIntoFollowingProse) {
  const auto words = parseHeadingWords(
      "<h1>HEADER <span style=\"font-size:0.8em\">SMALL</span> <sup>SUP</sup></h1>"
      "<p>BODY <span style=\"font-size:2em\">LARGE</span> TAIL</p>");
  ASSERT_EQ(words.size(), 6u);
  EXPECT_FLOAT_EQ(words.at("HEADER").block.fontSizeMultiplier, 1.15f);
  EXPECT_EQ(words.at("SMALL").sizePct, 80);
  EXPECT_EQ(words.at("SUP").sizePct, 50);
  EXPECT_NE(static_cast<uint8_t>(words.at("SUP").style) & EpdFontFamily::SUP, 0);
  for (const char* text : {"BODY", "LARGE", "TAIL"}) {
    EXPECT_FLOAT_EQ(words.at(text).block.fontSizeMultiplier, 1.0f);
    EXPECT_EQ(static_cast<uint8_t>(words.at(text).style) & EpdFontFamily::BOLD, 0);
  }
  EXPECT_EQ(words.at("BODY").sizePct, 100);
  EXPECT_EQ(words.at("LARGE").sizePct, 200);
  EXPECT_EQ(words.at("TAIL").sizePct, 100);
}

TEST(CompactHeadingTest, RealFontLadderAndAuxiliaryBudgetPreserveEffectiveHeadingSize) {
  FontSizeLadder ladder;
  ladder.addRung(1, 100);
  ladder.addRung(2, 115);
  ladder.addRung(3, 108);
  const auto words = parseHeadingWords("<h1>FIRST</h1><h2>SECOND</h2><h3>THIRD</h3>", true, ladder);
  ASSERT_EQ(words.size(), 3u);
  EXPECT_EQ(words.at("FIRST").block.headingFontId, 2);
  EXPECT_FLOAT_EQ(words.at("FIRST").block.fontSizeMultiplier, 1.0f);
  // The first heading claims the one auxiliary-font slot; H2 must retain its
  // requested scale when its different nearest native font cannot be loaded.
  EXPECT_EQ(words.at("SECOND").block.headingFontId, 0);
  EXPECT_FLOAT_EQ(words.at("SECOND").block.fontSizeMultiplier, 1.08f);
  EXPECT_EQ(words.at("THIRD").block.headingFontId, 0);
  EXPECT_FLOAT_EQ(words.at("THIRD").block.fontSizeMultiplier, 1.0f);
}

}  // namespace
