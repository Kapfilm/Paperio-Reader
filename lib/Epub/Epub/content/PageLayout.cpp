#include "PageLayout.h"

#include "Epub/Page.h"

namespace compiled {

LaidOutPage layoutPage(BlockStreamReader& reader, GfxRenderer& renderer, const LayoutParams& params,
                       const PagePosition& start) {
  LaidOutPage out;
  out.start = start;
  out.end = start;

  if (!reader.spineAvailable(start.spineIndex)) return out;
  if (!reader.openSpine(start.spineIndex)) return out;
  // G2a serves page-boundary cursors only (block start). seekToBlock repositions the stream; a
  // 0 blockIndex is the spine start (openSpine already left the cursor there, but seek is harmless).
  if (start.blockIndex != 0 && !reader.seekToBlock(start.blockIndex)) return out;

  // Capture the FIRST completed page and stop. Because `start` is a page boundary, LayoutSink's
  // cross-block accumulators begin zeroed here and its first emitPage() is exactly this page.
  std::unique_ptr<Page> firstPage;
  bool pageDone = false;
  LayoutSink sink(renderer, params, [&](std::unique_ptr<Page> p) {
    if (!pageDone) {
      firstPage = std::move(p);
      pageDone = true;
    }
    // Later pages (a block that overflowed past this one) are dropped — we want only the first.
  });

  // Drive the block stream, reproducing replaySpineStep's side-data ordering (anchors/labels/
  // footnotes/xpath before onBlock, chapters after) so the produced page is identical to a full
  // replay. Stop as soon as the first page completes: the block being processed at that moment is
  // where this page ended.
  const auto& anchors = reader.spineAnchors();
  const auto& labels = reader.spineLabels();
  const auto& stylePool = reader.spineStylePool();
  const auto& chapters = reader.spineChapters();
  static const CssStyle kEmptyStyle{};

  uint32_t lastBlockIndex = start.blockIndex;
  Block lb;
  while (!pageDone && reader.nextLogicalBlock(lb)) {
    const uint32_t bi = reader.currentFirstRecordIndex();
    lastBlockIndex = bi;
    for (const auto& a : anchors)
      if (a.blockIndex == bi) sink.onAnchor(a.id);
    for (const auto& pl : labels)
      if (pl.blockIndex == bi) sink.onPageBreakLabel(pl.label);
    for (const auto& fn : lb.footnotes) sink.onFootnote(static_cast<int>(fn.wordIndex), fn.entry);
    if (lb.hasXPath) sink.onXPathAdvance(lb.xpath.paragraphIndex, lb.xpath.listItemIndex, lb.xpath.bodyChildByteOffset);
    const CssStyle& style = (lb.styleId < stylePool.size()) ? stylePool[lb.styleId] : kEmptyStyle;
    sink.onBlock(std::move(lb), style);
    for (const auto& ch : chapters)
      if (ch.blockIndex == bi) sink.onChapter(ch.level, ch.title);
  }
  if (!reader.ok()) return out;

  // No page completed mid-stream → the spine's remaining content fit in one final page. Flush it.
  if (!pageDone) {
    sink.onSpineEnd();
  }

  if (!firstPage) return out;  // empty spine / no content

  out.page = std::move(firstPage);
  // G2a records the END cursor at BLOCK granularity (the block that triggered the page break, or the
  // spine end). The precise intra-block line/row offset is a G2b concern (needed for exact next-page
  // navigation); the position-identical page-content gate does not depend on it.
  out.atSpineEnd = !pageDone;  // page came from onSpineEnd → last page of the spine
  out.end.blockIndex = out.atSpineEnd ? 0 : static_cast<uint16_t>(lastBlockIndex);
  out.end.offset = 0;
  out.ok = true;
  return out;
}

}  // namespace compiled
