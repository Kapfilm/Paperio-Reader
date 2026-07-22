#include "LayoutSink.h"

#include <GfxRenderer.h>

#include "Epub/Page.h"
#include "Epub/ParsedText.h"

namespace compiled {

LayoutSink::LayoutSink(GfxRenderer& renderer, LayoutParams params,
                       std::function<void(std::unique_ptr<Page>)> completePageFn)
    : renderer_(renderer),
      completePageFn_(std::move(completePageFn)),
      fontId_(params.fontId),
      lineCompression_(params.lineCompression),
      extraParagraphSpacing_(params.extraParagraphSpacing),
      paragraphAlignment_(params.paragraphAlignment),
      viewportWidth_(params.viewportWidth),
      viewportHeight_(params.viewportHeight),
      hyphenationEnabled_(params.hyphenationEnabled),
      bionicReadingEnabled_(params.bionicReadingEnabled),
      fontSizeLadder_(std::move(params.fontSizeLadder)) {}

LayoutSink::~LayoutSink() = default;

// --- Layout helpers, copied verbatim from ChapterHtmlSlimParser (renderer-local). ---
// These reproduce the fused path's font/line-height/page-emit math exactly; keep them in
// lockstep with the parser until step 6 removes the fused originals.

void LayoutSink::resolveBlockFont(BlockStyle& bs) {
  if (bs.fontResolved) return;
  bs.fontResolved = true;
  if (bs.headingFontId != 0 || bs.fontSizeMultiplier == 1.0f) return;
  const FontSizeLadder::Resolved r = fontSizeLadder_.resolve(bs.fontSizeMultiplier * 100.0f);
  if (r.fontId == 0) {
    // Nearest rung is the body font (or the ladder is empty): keep the pure-scale path.
    bs.fontSizeMultiplier = r.residual;
    return;
  }
  if (auxFontId_ == 0) auxFontId_ = r.fontId;
  if (r.fontId != auxFontId_) return;  // aux budget already claimed by another size — scale fallback
  bs.headingFontId = r.fontId;
  bs.fontSizeMultiplier = r.residual;
}

int LayoutSink::effectiveLineHeight(const BlockStyle& bs) const {
  return static_cast<int>(renderer_.getLineHeight(effectiveFontId(bs)) * lineCompression_ * bs.fontSizeMultiplier +
                          0.5f);
}

void LayoutSink::emitPage(uint32_t xhtmlByteOffset) {
  paragraphLutPerPage_.push_back({xhtmlByteOffset, xpathParagraphIndex_, xpathListItemIndex_});
  completePageFn_(std::move(currentPage_));
  completedPageCount_++;
  currentPage_.reset(new (std::nothrow) Page());
  currentPageNextY_ = 0;
  lastBlockMarginBottom_ = 0;
  deferredPageImage_.reset();  // the deferred yPos update is moot on a fresh page

  // A floated image never crosses a page boundary, so any active float ended on the
  // page we just emitted. Clear it and drop stale float zones from the block that
  // continues onto the new page.
  activeFloatTop_ = 0;
  activeFloatBottom_ = 0;
  if (currentTextBlock_) {
    currentTextBlock_->getBlockStyle().floatZoneCount = 0;
  }
}

// --- BlockSink overrides. Text/image/table paths land in commits 2-5; stubbed here so the
// skeleton compiles and the BlockStyle-reconstruction path is testable in isolation. ---

void LayoutSink::onBlock(Block&& /*block*/, const CssStyle& /*style*/) {
  // Commit 2+: reconstruct the px BlockStyle via BlockStyle::fromCssStyle(style, emSize,
  // align, viewportWidth) — the same call the fused walk makes at cpp:1562-1564 — then run
  // the empty-block merge / makePages / split. emSize = renderer_.getFontAscenderSize(fontId_).
}

void LayoutSink::onAnchor(const std::string& id) { pendingAnchorId_ = id; }

void LayoutSink::onChapter(uint8_t /*level*/, const std::string& /*title*/) {}

void LayoutSink::onPageBreakLabel(const std::string& label) {
  if (label.empty()) return;
  pageBreakLabels_.emplace_back(static_cast<uint16_t>(completedPageCount_), label);
}

void LayoutSink::onFootnote(int /*wordIndex*/, const FootnoteEntry& /*entry*/) {}

void LayoutSink::onSpineEnd() {
  // Commit 2+: flush the last accumulated block (makePages) and emit the final page.
}

}  // namespace compiled
