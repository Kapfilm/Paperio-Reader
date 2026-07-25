#include "CompiledContent.h"

#include <HalStorage.h>

#include "Serialization.h"

namespace compiled {
namespace {

using serialization::readPod;
using serialization::readString;
using serialization::writePod;
using serialization::writeString;

void writeLength(FsFile& f, const CssLength& len) {
  writePod(f, len.value);
  writePod(f, static_cast<uint8_t>(len.unit));
}

// Word run + its backing text — shared by text blocks and table cells.
void writeWords(FsFile& f, const std::vector<Word>& words, const std::string& text) {
  writePod(f, static_cast<uint32_t>(words.size()));
  for (const Word& w : words) {
    writePod(f, w.textOff);
    writePod(f, w.styleSpan);
    writePod(f, w.sizePct);
    writePod(f, w.bidiLevel);
  }
  writeString(f, text);
}

bool readWords(FsFile& f, std::vector<Word>& words, std::string& text) {
  uint32_t count = 0;
  readPod(f, count);
  words.resize(count);
  for (Word& w : words) {
    readPod(f, w.textOff);
    readPod(f, w.styleSpan);
    readPod(f, w.sizePct);
    readPod(f, w.bidiLevel);
  }
  return readString(f, text);
}

void readLength(FsFile& f, CssLength& len) {
  readPod(f, len.value);
  uint8_t unit = 0;
  readPod(f, unit);
  len.unit = static_cast<CssUnit>(unit);
}

// The 24 explicit-set flags packed into one u32, LSB-first in this fixed order.
uint32_t packDefined(const CssPropertyFlags& d) {
  uint32_t b = 0;
  int i = 0;
  auto set = [&](bool v) {
    if (v) b |= (1u << i);
    ++i;
  };
  set(d.textAlign);
  set(d.fontStyle);
  set(d.fontWeight);
  set(d.textDecoration);
  set(d.textIndent);
  set(d.marginTop);
  set(d.marginBottom);
  set(d.marginLeft);
  set(d.marginRight);
  set(d.paddingTop);
  set(d.paddingBottom);
  set(d.paddingLeft);
  set(d.paddingRight);
  set(d.imageHeight);
  set(d.imageWidth);
  set(d.display);
  set(d.verticalAlign);
  set(d.listStyleNone);
  set(d.pageBreakBefore);
  set(d.pageBreakAfter);
  set(d.lineHeight);
  set(d.fontSizeMultiplier);
  set(d.cssFloat);
  set(d.smallCaps);
  return b;
}

void unpackDefined(uint32_t b, CssPropertyFlags& d) {
  int i = 0;
  auto get = [&]() { return (b >> i++) & 1u; };
  d.textAlign = get();
  d.fontStyle = get();
  d.fontWeight = get();
  d.textDecoration = get();
  d.textIndent = get();
  d.marginTop = get();
  d.marginBottom = get();
  d.marginLeft = get();
  d.marginRight = get();
  d.paddingTop = get();
  d.paddingBottom = get();
  d.paddingLeft = get();
  d.paddingRight = get();
  d.imageHeight = get();
  d.imageWidth = get();
  d.display = get();
  d.verticalAlign = get();
  d.listStyleNone = get();
  d.pageBreakBefore = get();
  d.pageBreakAfter = get();
  d.lineHeight = get();
  d.fontSizeMultiplier = get();
  d.cssFloat = get();
  d.smallCaps = get();
}

void writeStyle(FsFile& f, const CssStyle& s) {
  writePod(f, static_cast<uint8_t>(s.textAlign));
  writePod(f, static_cast<uint8_t>(s.fontStyle));
  writePod(f, static_cast<uint8_t>(s.fontWeight));
  writePod(f, static_cast<uint8_t>(s.textDecoration));
  writeLength(f, s.textIndent);
  writeLength(f, s.marginTop);
  writeLength(f, s.marginBottom);
  writeLength(f, s.marginLeft);
  writeLength(f, s.marginRight);
  writeLength(f, s.paddingTop);
  writeLength(f, s.paddingBottom);
  writeLength(f, s.paddingLeft);
  writeLength(f, s.paddingRight);
  writeLength(f, s.imageHeight);
  writeLength(f, s.imageWidth);
  writePod(f, static_cast<uint8_t>(s.display));
  writePod(f, static_cast<uint8_t>(s.verticalAlign));
  writePod(f, static_cast<uint8_t>(s.listStyleNone));
  writePod(f, static_cast<uint8_t>(s.pageBreakBefore));
  writePod(f, static_cast<uint8_t>(s.pageBreakAfter));
  writePod(f, s.lineHeightMultiplier);
  writePod(f, s.fontSizeMultiplier);
  writePod(f, static_cast<uint8_t>(s.cssFloat));
  writePod(f, static_cast<uint8_t>(s.smallCaps));
  writePod(f, packDefined(s.defined));
}

void readStyle(FsFile& f, CssStyle& s) {
  uint8_t e = 0;
  readPod(f, e);
  s.textAlign = static_cast<CssTextAlign>(e);
  readPod(f, e);
  s.fontStyle = static_cast<CssFontStyle>(e);
  readPod(f, e);
  s.fontWeight = static_cast<CssFontWeight>(e);
  readPod(f, e);
  s.textDecoration = static_cast<CssTextDecoration>(e);
  readLength(f, s.textIndent);
  readLength(f, s.marginTop);
  readLength(f, s.marginBottom);
  readLength(f, s.marginLeft);
  readLength(f, s.marginRight);
  readLength(f, s.paddingTop);
  readLength(f, s.paddingBottom);
  readLength(f, s.paddingLeft);
  readLength(f, s.paddingRight);
  readLength(f, s.imageHeight);
  readLength(f, s.imageWidth);
  readPod(f, e);
  s.display = static_cast<CssDisplay>(e);
  readPod(f, e);
  s.verticalAlign = static_cast<CssVerticalAlign>(e);
  readPod(f, e);
  s.listStyleNone = e != 0;
  readPod(f, e);
  s.pageBreakBefore = e != 0;
  readPod(f, e);
  s.pageBreakAfter = e != 0;
  readPod(f, s.lineHeightMultiplier);
  readPod(f, s.fontSizeMultiplier);
  readPod(f, e);
  s.cssFloat = static_cast<CssFloat>(e);
  readPod(f, e);
  s.smallCaps = e != 0;
  uint32_t defined = 0;
  readPod(f, defined);
  unpackDefined(defined, s.defined);
}

}  // namespace

bool writeContentBin(FsFile& out, const CompiledContent& content) {
  if (!out) return false;
  out.write(reinterpret_cast<const uint8_t*>(kMagic), 4);
  writePod(out, kVersion);

  writePod(out, static_cast<uint32_t>(content.stylePool.size()));
  for (const CssStyle& s : content.stylePool) writeStyle(out, s);

  writePod(out, static_cast<uint32_t>(content.spines.size()));
  for (const SpineContent& spine : content.spines) {
    writePod(out, spine.firstCharOffset);
    writePod(out, static_cast<uint32_t>(spine.blocks.size()));
    for (const Block& b : spine.blocks) {
      writePod(out, static_cast<uint8_t>(b.type));
      writePod(out, b.styleId);
      writePod(out, b.flags);
      writePod(out, b.charOffset);
      if (b.type == BlockType::Text) {
        writeWords(out, b.words, b.text);
        writePod(out, static_cast<uint32_t>(b.footnotePreviews.size()));
        for (const PreviewRun& pr : b.footnotePreviews) {
          writePod(out, pr.startWord);
          writePod(out, pr.count);
        }
        const bool hasInline = !b.inlineImageEntryPath.empty();
        writePod(out, static_cast<uint8_t>(hasInline));
        if (hasInline) {
          writeString(out, b.inlineImageEntryPath);
          writePod(out, b.inlineImageWidth);
          writePod(out, b.inlineImageHeight);
          writePod(out, b.inlineImageSide);
          writeString(out, b.inlineImageAlt);
        }
      } else if (b.type == BlockType::Table) {
        writePod(out, static_cast<uint8_t>(b.hasBorder));
        writePod(out, static_cast<uint32_t>(b.rows.size()));
        for (const TableRow& r : b.rows) {
          writePod(out, static_cast<uint8_t>(r.isHeaderRow));
          writePod(out, static_cast<uint32_t>(r.cells.size()));
          for (const TableCell& c : r.cells) {
            writePod(out, static_cast<uint8_t>(c.isHeader));
            writePod(out, c.colSpan);
            writeWords(out, c.words, c.text);
            writeString(out, c.imageEntryPath);
            writePod(out, c.imageWidth);
            writePod(out, c.imageHeight);
            writeString(out, c.imageAlt);
          }
        }
      } else if (b.type == BlockType::Hr) {
        // A horizontal rule carries no body: type + charOffset (already written) are enough.
        // Stage-2 derives its centered geometry from the viewport at layout time.
      } else {  // BlockType::Image — the reader dispatches the mirror image case explicitly.
        writeString(out, b.entryPath);
        writePod(out, b.width);
        writePod(out, b.height);
        writePod(out, b.floatSide);
        writeString(out, b.alt);
      }
      // Shared per-block tail (any type): footnote anchors + optional XPath counters. Kept out of
      // the type branches so image/hr/table blocks can also carry an XPath advance.
      writePod(out, static_cast<uint32_t>(b.footnotes.size()));
      for (const FootnoteRef& fn : b.footnotes) {
        writePod(out, fn.wordIndex);
        out.write(reinterpret_cast<const uint8_t*>(fn.entry.number), FOOTNOTE_NUMBER_LEN);
        out.write(reinterpret_cast<const uint8_t*>(fn.entry.href), FOOTNOTE_HREF_LEN);
      }
      writePod(out, static_cast<uint8_t>(b.hasXPath));
      if (b.hasXPath) {
        writePod(out, b.xpath.paragraphIndex);
        writePod(out, b.xpath.listItemIndex);
        writePod(out, b.xpath.bodyChildByteOffset);
      }
    }
    writePod(out, static_cast<uint32_t>(spine.anchors.size()));
    for (const Anchor& a : spine.anchors) {
      writeString(out, a.id);
      writePod(out, a.blockIndex);
      writePod(out, a.charOffsetInBlock);
    }
    writePod(out, static_cast<uint32_t>(spine.pageBreakLabels.size()));
    for (const PageBreakLabel& pl : spine.pageBreakLabels) {
      writeString(out, pl.label);
      writePod(out, pl.blockIndex);
    }
  }

  writePod(out, static_cast<uint32_t>(content.chapters.size()));
  for (const Chapter& c : content.chapters) {
    writePod(out, c.spineIndex);
    writePod(out, c.blockIndex);
    writePod(out, c.level);
    writeString(out, c.title);
  }
  return true;
}

bool readContentBin(FsFile& in, CompiledContent& content) {
  if (!in) return false;
  content.stylePool.clear();
  content.spines.clear();

  char magic[4] = {};
  if (in.read(reinterpret_cast<uint8_t*>(magic), 4) != 4) return false;
  for (int i = 0; i < 4; ++i) {
    if (magic[i] != kMagic[i]) return false;
  }
  uint8_t version = 0;
  readPod(in, version);
  if (version != kVersion) return false;

  uint32_t styleCount = 0;
  readPod(in, styleCount);
  content.stylePool.resize(styleCount);
  for (uint32_t i = 0; i < styleCount; ++i) readStyle(in, content.stylePool[i]);

  uint32_t spineCount = 0;
  readPod(in, spineCount);
  content.spines.resize(spineCount);
  for (uint32_t si = 0; si < spineCount; ++si) {
    SpineContent& spine = content.spines[si];
    readPod(in, spine.firstCharOffset);
    uint32_t blockCount = 0;
    readPod(in, blockCount);
    spine.blocks.resize(blockCount);
    for (uint32_t bi = 0; bi < blockCount; ++bi) {
      Block& b = spine.blocks[bi];
      uint8_t type = 0;
      readPod(in, type);
      b.type = static_cast<BlockType>(type);
      readPod(in, b.styleId);
      readPod(in, b.flags);
      readPod(in, b.charOffset);
      if (b.type == BlockType::Text) {
        if (!readWords(in, b.words, b.text)) return false;
        uint32_t previewCount = 0;
        readPod(in, previewCount);
        b.footnotePreviews.resize(previewCount);
        for (PreviewRun& pr : b.footnotePreviews) {
          readPod(in, pr.startWord);
          readPod(in, pr.count);
        }
        uint8_t hasInline = 0;
        readPod(in, hasInline);
        if (hasInline) {
          if (!readString(in, b.inlineImageEntryPath)) return false;
          readPod(in, b.inlineImageWidth);
          readPod(in, b.inlineImageHeight);
          readPod(in, b.inlineImageSide);
          if (!readString(in, b.inlineImageAlt)) return false;
        }
      } else if (b.type == BlockType::Table) {
        uint8_t hasBorder = 1;
        readPod(in, hasBorder);
        b.hasBorder = hasBorder != 0;
        uint32_t rowCount = 0;
        readPod(in, rowCount);
        b.rows.resize(rowCount);
        for (uint32_t ri = 0; ri < rowCount; ++ri) {
          TableRow& r = b.rows[ri];
          uint8_t isHeaderRow = 0;
          readPod(in, isHeaderRow);
          r.isHeaderRow = isHeaderRow != 0;
          uint32_t cellCount = 0;
          readPod(in, cellCount);
          r.cells.resize(cellCount);
          for (uint32_t ci = 0; ci < cellCount; ++ci) {
            TableCell& c = r.cells[ci];
            uint8_t isHeader = 0;
            readPod(in, isHeader);
            c.isHeader = isHeader != 0;
            readPod(in, c.colSpan);
            if (!readWords(in, c.words, c.text)) return false;
            if (!readString(in, c.imageEntryPath)) return false;
            readPod(in, c.imageWidth);
            readPod(in, c.imageHeight);
            if (!readString(in, c.imageAlt)) return false;
          }
        }
      } else if (b.type == BlockType::Hr) {
        // No body — the writer emitted only the header (type/charOffset).
      } else if (b.type == BlockType::Image) {
        if (!readString(in, b.entryPath)) return false;
        readPod(in, b.width);
        readPod(in, b.height);
        readPod(in, b.floatSide);
        if (!readString(in, b.alt)) return false;
      } else {
        // Unknown block type: a newer WBC1 wrote a variant this reader doesn't know. The body
        // layout is unknown, so continuing would misalign the whole stream — fail cleanly (the
        // version check should have caught this; this is defense in depth against a bad file).
        return false;
      }
      // Shared per-block tail: footnote anchors + optional XPath counters (mirror of the writer).
      uint32_t footnoteCount = 0;
      readPod(in, footnoteCount);
      b.footnotes.resize(footnoteCount);
      for (FootnoteRef& fn : b.footnotes) {
        readPod(in, fn.wordIndex);
        if (in.read(reinterpret_cast<uint8_t*>(fn.entry.number), FOOTNOTE_NUMBER_LEN) != FOOTNOTE_NUMBER_LEN)
          return false;
        if (in.read(reinterpret_cast<uint8_t*>(fn.entry.href), FOOTNOTE_HREF_LEN) != FOOTNOTE_HREF_LEN) return false;
      }
      uint8_t hasXPath = 0;
      readPod(in, hasXPath);
      b.hasXPath = hasXPath != 0;
      if (b.hasXPath) {
        readPod(in, b.xpath.paragraphIndex);
        readPod(in, b.xpath.listItemIndex);
        readPod(in, b.xpath.bodyChildByteOffset);
      }
    }
    uint32_t anchorCount = 0;
    readPod(in, anchorCount);
    spine.anchors.resize(anchorCount);
    for (uint32_t ai = 0; ai < anchorCount; ++ai) {
      Anchor& a = spine.anchors[ai];
      if (!readString(in, a.id)) return false;
      readPod(in, a.blockIndex);
      readPod(in, a.charOffsetInBlock);
    }
    uint32_t labelCount = 0;
    readPod(in, labelCount);
    spine.pageBreakLabels.resize(labelCount);
    for (PageBreakLabel& pl : spine.pageBreakLabels) {
      if (!readString(in, pl.label)) return false;
      readPod(in, pl.blockIndex);
    }
  }

  uint32_t chapterCount = 0;
  readPod(in, chapterCount);
  content.chapters.resize(chapterCount);
  for (uint32_t ci = 0; ci < chapterCount; ++ci) {
    Chapter& c = content.chapters[ci];
    readPod(in, c.spineIndex);
    readPod(in, c.blockIndex);
    readPod(in, c.level);
    if (!readString(in, c.title)) return false;
  }
  return true;
}

bool styleEquals(const CssStyle& a, const CssStyle& b) {
  const auto lenEq = [](const CssLength& x, const CssLength& y) { return x.value == y.value && x.unit == y.unit; };
  const auto definedEq = [](const CssPropertyFlags& x, const CssPropertyFlags& y) {
    return packDefined(x) == packDefined(y);
  };
  return a.textAlign == b.textAlign && a.fontStyle == b.fontStyle && a.fontWeight == b.fontWeight &&
         a.textDecoration == b.textDecoration && a.display == b.display && a.verticalAlign == b.verticalAlign &&
         a.listStyleNone == b.listStyleNone && a.pageBreakBefore == b.pageBreakBefore &&
         a.pageBreakAfter == b.pageBreakAfter && a.cssFloat == b.cssFloat && a.smallCaps == b.smallCaps &&
         a.lineHeightMultiplier == b.lineHeightMultiplier && a.fontSizeMultiplier == b.fontSizeMultiplier &&
         lenEq(a.textIndent, b.textIndent) && lenEq(a.marginTop, b.marginTop) &&
         lenEq(a.marginBottom, b.marginBottom) && lenEq(a.marginLeft, b.marginLeft) &&
         lenEq(a.marginRight, b.marginRight) && lenEq(a.paddingTop, b.paddingTop) &&
         lenEq(a.paddingBottom, b.paddingBottom) && lenEq(a.paddingLeft, b.paddingLeft) &&
         lenEq(a.paddingRight, b.paddingRight) && lenEq(a.imageHeight, b.imageHeight) &&
         lenEq(a.imageWidth, b.imageWidth) && definedEq(a.defined, b.defined);
}

uint16_t internStyle(CompiledContent& content, const CssStyle& style) {
  for (size_t i = 0; i < content.stylePool.size(); ++i) {
    if (styleEquals(content.stylePool[i], style)) return static_cast<uint16_t>(i);
  }
  content.stylePool.push_back(style);
  return static_cast<uint16_t>(content.stylePool.size() - 1);
}

}  // namespace compiled
