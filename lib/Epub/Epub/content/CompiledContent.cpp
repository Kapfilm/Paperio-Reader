#include "CompiledContent.h"

#include <HalStorage.h>

#include "BlockSerialization.h"
#include "Serialization.h"

namespace compiled {
namespace {

using serialization::readPod;
using serialization::writePod;

}  // namespace

bool writeContentBin(FsFile& out, const CompiledContent& content) {
  if (!out) return false;
  out.write(reinterpret_cast<const uint8_t*>(kMagic), 4);
  writePod(out, kVersion);
  writePod(out, content.sourceFingerprint);  // v4: source book's ZIP content fingerprint

  // Body uses the shared per-block/aux serializers (BlockSerialization.cpp) so the whole-book form
  // and the streaming ContentBinWriter emit identical block records.
  writeStylePool(out, content.stylePool);
  writePod(out, static_cast<uint32_t>(content.spines.size()));
  for (const SpineContent& spine : content.spines) {
    writePod(out, spine.firstCharOffset);
    writePod(out, static_cast<uint32_t>(spine.blocks.size()));
    for (const Block& b : spine.blocks) writeBlock(out, b);
    writeAnchors(out, spine.anchors);
    writeLabels(out, spine.pageBreakLabels);
  }
  writeChapters(out, content.chapters);
  return static_cast<bool>(out);
}

bool readContentBin(FsFile& in, CompiledContent& content) {
  if (!in) return false;
  content.stylePool.clear();
  content.spines.clear();
  content.chapters.clear();
  content.sourceFingerprint = 0;

  char magic[4] = {};
  if (in.read(reinterpret_cast<uint8_t*>(magic), 4) != 4) return false;
  for (int i = 0; i < 4; ++i) {
    if (magic[i] != kMagic[i]) return false;
  }
  uint8_t version = 0;
  readPod(in, version);
  if (version != kVersion) return false;
  readPod(in, content.sourceFingerprint);  // v4

  if (!readStylePool(in, content.stylePool)) return false;

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
      if (!readBlock(in, spine.blocks[bi])) return false;
    }
    if (!readAnchors(in, spine.anchors)) return false;
    if (!readLabels(in, spine.pageBreakLabels)) return false;
  }

  return readChapters(in, content.chapters);
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
