#include "CompiledContent.h"

#include <HalStorage.h>

#include "BlockSerialization.h"
#include "BlockStreamReader.h"
#include "Serialization.h"

namespace compiled {
namespace {

using serialization::readPod;
using serialization::writePod;

}  // namespace

// Whole-book v5 writer. Produces the SAME on-disk layout as the streaming ContentBinWriter (header
// with back-patched section offsets → per-spine block streams → style pool → spine index →
// chapters), but from an already-materialized CompiledContent (test/tooling convenience). Blocks are
// written AS-IS (their existing styleId + already-split kContinuation records), so it faithfully
// round-trips whatever the caller built.
bool writeContentBin(FsFile& out, const CompiledContent& content) {
  if (!out) return false;
  const auto writeHeader = [&](uint32_t spineCount, uint32_t stylePoolOff, uint32_t spineIndexOff,
                               uint32_t chaptersOff) {
    out.write(reinterpret_cast<const uint8_t*>(kMagic), 4);
    writePod(out, kVersion);
    writePod(out, content.sourceFingerprint);
    writePod(out, spineCount);
    writePod(out, stylePoolOff);
    writePod(out, spineIndexOff);
    writePod(out, chaptersOff);
  };
  writeHeader(static_cast<uint32_t>(content.spines.size()), 0, 0, 0);  // placeholder

  std::vector<uint32_t> spineOffsets;
  spineOffsets.reserve(content.spines.size());
  for (const SpineContent& spine : content.spines) {
    const uint32_t spineStart = static_cast<uint32_t>(out.position());
    spineOffsets.push_back(spineStart);
    writePod(out, spine.firstCharOffset);
    writePod(out, static_cast<uint32_t>(spine.blocks.size()));
    writePod(out, static_cast<uint32_t>(0));  // auxOffset placeholder
    for (const Block& b : spine.blocks) writeBlock(out, b);
    const uint32_t auxOffset = static_cast<uint32_t>(out.position());
    writeAnchors(out, spine.anchors);
    writeLabels(out, spine.pageBreakLabels);
    const uint32_t afterAux = static_cast<uint32_t>(out.position());
    if (!out.seekSet(spineStart + 2 * sizeof(uint32_t))) return false;  // skip firstCharOffset+blockCount
    writePod(out, auxOffset);
    out.seekSet(afterAux);
  }
  const uint32_t stylePoolOff = static_cast<uint32_t>(out.position());
  writeStylePool(out, content.stylePool);
  const uint32_t spineIndexOff = static_cast<uint32_t>(out.position());
  writePod(out, static_cast<uint32_t>(spineOffsets.size()));
  for (uint32_t off : spineOffsets) writePod(out, off);
  const uint32_t chaptersOff = static_cast<uint32_t>(out.position());
  writeChapters(out, content.chapters);

  if (!out.seekSet(0)) return false;
  writeHeader(static_cast<uint32_t>(content.spines.size()), stylePoolOff, spineIndexOff, chaptersOff);
  return static_cast<bool>(out);
}

bool readContentBin(FsFile& in, CompiledContent& content) {
  content.stylePool.clear();
  content.spines.clear();
  content.chapters.clear();
  content.sourceFingerprint = 0;

  BlockStreamReader r;
  if (!r.open(in)) return false;
  content.sourceFingerprint = r.fingerprint();
  content.stylePool = r.stylePool();

  const uint32_t spineCount = r.spineCount();
  content.spines.resize(spineCount);
  for (uint32_t si = 0; si < spineCount; ++si) {
    SpineContent& spine = content.spines[si];
    if (!r.openSpine(si)) return false;
    spine.firstCharOffset = r.spineFirstCharOffset();
    spine.anchors = r.spineAnchors();  // openSpine pre-loaded these
    spine.pageBreakLabels = r.spineLabels();
    // Whole-book read keeps the RAW on-disk records (kContinuation splits stored as-is), so read
    // records directly rather than the merged logical blocks.
    Block b;
    while (r.nextRawRecord(b)) spine.blocks.push_back(std::move(b));
    if (!r.ok()) return false;
  }
  return r.readChapters(content.chapters);
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

uint16_t internStyle(std::vector<CssStyle>& pool, const CssStyle& style) {
  for (size_t i = 0; i < pool.size(); ++i) {
    if (styleEquals(pool[i], style)) return static_cast<uint16_t>(i);
  }
  pool.push_back(style);
  return static_cast<uint16_t>(pool.size() - 1);
}

uint16_t internStyle(CompiledContent& content, const CssStyle& style) {
  return internStyle(content.stylePool, style);
}

}  // namespace compiled
