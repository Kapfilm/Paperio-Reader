// Round-trip tests for the WBC1 compiled-content container (Phase 3 sub-step 2a).
// Writes a CompiledContent to an in-memory FsFile, reads it back, and asserts the
// model survives byte-for-byte — the equivalence foundation the Stage-1 writer and
// Stage-2 reader build on.

#include <gtest/gtest.h>

#include "Arduino.h"
#include "CompiledContent.h"
#include "HalStorage.h"

namespace {

using compiled::Anchor;
using compiled::Block;
using compiled::BlockType;
using compiled::Chapter;
using compiled::CompiledContent;
using compiled::SpineContent;
using compiled::Word;

bool lengthEq(const CssLength& a, const CssLength& b) { return a.value == b.value && a.unit == b.unit; }

bool styleEq(const CssStyle& a, const CssStyle& b) {
  auto d = [](const CssPropertyFlags& f) {
    return std::vector<bool>{f.textAlign,     f.fontStyle,          f.fontWeight,      f.textDecoration,
                             f.textIndent,    f.marginTop,          f.marginBottom,    f.marginLeft,
                             f.marginRight,   f.paddingTop,         f.paddingBottom,   f.paddingLeft,
                             f.paddingRight,  f.imageHeight,        f.imageWidth,      f.display,
                             f.verticalAlign, f.listStyleNone,      f.pageBreakBefore, f.pageBreakAfter,
                             f.lineHeight,    f.fontSizeMultiplier, f.cssFloat,        f.smallCaps};
  };
  return a.textAlign == b.textAlign && a.fontStyle == b.fontStyle && a.fontWeight == b.fontWeight &&
         a.textDecoration == b.textDecoration && a.display == b.display && a.verticalAlign == b.verticalAlign &&
         a.listStyleNone == b.listStyleNone && a.pageBreakBefore == b.pageBreakBefore &&
         a.pageBreakAfter == b.pageBreakAfter && a.cssFloat == b.cssFloat && a.smallCaps == b.smallCaps &&
         a.lineHeightMultiplier == b.lineHeightMultiplier && a.fontSizeMultiplier == b.fontSizeMultiplier &&
         lengthEq(a.textIndent, b.textIndent) && lengthEq(a.marginTop, b.marginTop) &&
         lengthEq(a.marginBottom, b.marginBottom) && lengthEq(a.marginLeft, b.marginLeft) &&
         lengthEq(a.marginRight, b.marginRight) && lengthEq(a.paddingTop, b.paddingTop) &&
         lengthEq(a.paddingBottom, b.paddingBottom) && lengthEq(a.paddingLeft, b.paddingLeft) &&
         lengthEq(a.paddingRight, b.paddingRight) && lengthEq(a.imageHeight, b.imageHeight) &&
         lengthEq(a.imageWidth, b.imageWidth) && d(a.defined) == d(b.defined);
}

CssStyle makeStyle(CssTextAlign align, float marginTopEm, bool bold, float fontMul) {
  CssStyle s;
  s.textAlign = align;
  s.defined.textAlign = 1;
  s.marginTop = CssLength(marginTopEm, CssUnit::Em);
  s.defined.marginTop = 1;
  if (bold) {
    s.fontWeight = CssFontWeight::Bold;
    s.defined.fontWeight = 1;
  }
  s.fontSizeMultiplier = fontMul;
  s.defined.fontSizeMultiplier = 1;
  s.pageBreakBefore = align == CssTextAlign::Center;  // arbitrary, to exercise flags
  s.defined.pageBreakBefore = s.pageBreakBefore;
  return s;
}

Block textBlock(uint16_t styleId, uint8_t flags, uint32_t charOffset, const std::vector<std::string>& words) {
  Block b;
  b.type = BlockType::Text;
  b.styleId = styleId;
  b.flags = flags;
  b.charOffset = charOffset;
  for (uint8_t i = 0; i < words.size(); ++i) {
    Word w;
    w.textOff = static_cast<uint32_t>(b.text.size());
    w.styleSpan = i;  // arbitrary per-word data to check it survives
    w.sizePct = 90 + i;
    w.bidiLevel = i % 3;
    b.words.push_back(w);
    b.text.append(words[i]);
    b.text.push_back('\0');
  }
  return b;
}

Block imageBlock(uint16_t styleId, const std::string& path, int16_t w, int16_t h, uint8_t floatSide,
                 const std::string& alt) {
  Block b;
  b.type = BlockType::Image;
  b.styleId = styleId;
  b.entryPath = path;
  b.width = w;
  b.height = h;
  b.floatSide = floatSide;
  b.alt = alt;
  return b;
}

void expectEqual(const CompiledContent& in, const CompiledContent& out) {
  ASSERT_EQ(in.stylePool.size(), out.stylePool.size());
  for (size_t i = 0; i < in.stylePool.size(); ++i) {
    EXPECT_TRUE(styleEq(in.stylePool[i], out.stylePool[i])) << "style " << i;
  }
  ASSERT_EQ(in.spines.size(), out.spines.size());
  for (size_t s = 0; s < in.spines.size(); ++s) {
    EXPECT_EQ(in.spines[s].firstCharOffset, out.spines[s].firstCharOffset);
    ASSERT_EQ(in.spines[s].blocks.size(), out.spines[s].blocks.size()) << "spine " << s;
    for (size_t bi = 0; bi < in.spines[s].blocks.size(); ++bi) {
      const Block& a = in.spines[s].blocks[bi];
      const Block& b = out.spines[s].blocks[bi];
      EXPECT_EQ(static_cast<int>(a.type), static_cast<int>(b.type));
      EXPECT_EQ(a.styleId, b.styleId);
      EXPECT_EQ(a.flags, b.flags);
      EXPECT_EQ(a.charOffset, b.charOffset);
      EXPECT_EQ(a.text, b.text);
      ASSERT_EQ(a.words.size(), b.words.size());
      for (size_t wi = 0; wi < a.words.size(); ++wi) {
        EXPECT_EQ(a.words[wi].textOff, b.words[wi].textOff);
        EXPECT_EQ(a.words[wi].styleSpan, b.words[wi].styleSpan);
        EXPECT_EQ(a.words[wi].sizePct, b.words[wi].sizePct);
        EXPECT_EQ(a.words[wi].bidiLevel, b.words[wi].bidiLevel);
      }
      EXPECT_EQ(a.entryPath, b.entryPath);
      EXPECT_EQ(a.width, b.width);
      EXPECT_EQ(a.height, b.height);
      EXPECT_EQ(a.floatSide, b.floatSide);
      EXPECT_EQ(a.alt, b.alt);
    }
    ASSERT_EQ(in.spines[s].anchors.size(), out.spines[s].anchors.size()) << "anchors, spine " << s;
    for (size_t ai = 0; ai < in.spines[s].anchors.size(); ++ai) {
      const Anchor& a = in.spines[s].anchors[ai];
      const Anchor& b = out.spines[s].anchors[ai];
      EXPECT_EQ(a.id, b.id);
      EXPECT_EQ(a.blockIndex, b.blockIndex);
      EXPECT_EQ(a.charOffsetInBlock, b.charOffsetInBlock);
    }
  }
  ASSERT_EQ(in.chapters.size(), out.chapters.size());
  for (size_t ci = 0; ci < in.chapters.size(); ++ci) {
    EXPECT_EQ(in.chapters[ci].spineIndex, out.chapters[ci].spineIndex);
    EXPECT_EQ(in.chapters[ci].blockIndex, out.chapters[ci].blockIndex);
    EXPECT_EQ(in.chapters[ci].level, out.chapters[ci].level);
    EXPECT_EQ(in.chapters[ci].title, out.chapters[ci].title);
  }
}

CompiledContent roundTrip(const CompiledContent& in) {
  FsFile f = FsFile::forReadWrite();
  EXPECT_TRUE(compiled::writeContentBin(f, in));
  EXPECT_TRUE(f.seek(0));
  CompiledContent out;
  EXPECT_TRUE(compiled::readContentBin(f, out));
  return out;
}

}  // namespace

TEST(CompiledContent, RoundTripPreservesModel) {
  CompiledContent in;
  in.stylePool.push_back(makeStyle(CssTextAlign::Justify, 0.0f, false, 1.0f));
  in.stylePool.push_back(makeStyle(CssTextAlign::Center, 1.5f, true, 1.6f));
  in.stylePool.push_back(makeStyle(CssTextAlign::Right, 0.25f, false, 0.87f));

  SpineContent s0;
  s0.firstCharOffset = 0;
  s0.blocks.push_back(textBlock(1, compiled::kStartsChapter | compiled::kPageBreakBefore, 0, {"Chapter", "One"}));
  s0.blocks.push_back(textBlock(0, 0, 10, {"Call", "me", "Ishmael."}));
  s0.blocks.push_back(imageBlock(2, "OEBPS/images/whale.jpg", 640, 480, 1, "a whale"));
  s0.anchors.push_back({"chap01", 0, 0});
  s0.anchors.push_back({"para_ishmael", 1, 5});
  in.spines.push_back(std::move(s0));

  SpineContent s1;
  s1.firstCharOffset = 4096;
  s1.blocks.push_back(textBlock(2, compiled::kPageBreakAfter, 4096, {"The", "end."}));
  in.spines.push_back(std::move(s1));

  in.chapters.push_back({0, 0, 1, "Chapter One"});
  in.chapters.push_back({1, 0, 2, "The End"});

  expectEqual(in, roundTrip(in));
}

TEST(CompiledContent, InternStyleDedupsByValue) {
  CompiledContent c;
  const CssStyle justify = makeStyle(CssTextAlign::Justify, 1.0f, false, 1.0f);
  const CssStyle justifyCopy = makeStyle(CssTextAlign::Justify, 1.0f, false, 1.0f);
  const CssStyle center = makeStyle(CssTextAlign::Center, 1.0f, false, 1.0f);

  const uint16_t id0 = compiled::internStyle(c, justify);
  const uint16_t id1 = compiled::internStyle(c, justifyCopy);  // equal → same id, no new entry
  const uint16_t id2 = compiled::internStyle(c, center);       // different → new id

  EXPECT_EQ(id0, 0u);
  EXPECT_EQ(id1, 0u);
  EXPECT_EQ(id2, 1u);
  EXPECT_EQ(c.stylePool.size(), 2u);
  EXPECT_TRUE(compiled::styleEquals(c.stylePool[id0], justify));
  EXPECT_FALSE(compiled::styleEquals(c.stylePool[id0], center));
}

TEST(CompiledContent, EmptyContentRoundTrips) {
  CompiledContent in;
  expectEqual(in, roundTrip(in));
}

TEST(CompiledContent, RejectsBadMagicAndVersion) {
  // A buffer that is not WBC1 must be rejected (treated as a stale cache).
  FsFile f = FsFile::forReadWrite();
  const char junk[8] = {'N', 'O', 'P', 'E', 1, 2, 3, 4};
  f.write(junk, sizeof(junk));
  EXPECT_TRUE(f.seek(0));
  CompiledContent out;
  EXPECT_FALSE(compiled::readContentBin(f, out));
}
