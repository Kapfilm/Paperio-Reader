#include "LayoutSink.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "Epub/Page.h"
#include "Epub/ParsedText.h"
#include "Epub/blocks/ImageBlock.h"
#include "Epub/blocks/TextBlock.h"
#include "ImageLayout.h"
#include "TableLayout.h"

namespace compiled {
namespace {

// Reverse of stage1MapStyleSpan (ChapterHtmlSlimParser.cpp): the on-disk styleSpan bitmask
// back to the EpdFontFamily::Style the layout's ParsedText::addWord expects.
EpdFontFamily::Style spanToFontStyle(uint8_t span) {
  int s = EpdFontFamily::REGULAR;
  if (span & kSpanBold) s |= EpdFontFamily::BOLD;
  if (span & kSpanItalic) s |= EpdFontFamily::ITALIC;
  if (span & kSpanUnderline) s |= EpdFontFamily::UNDERLINE;
  if (span & kSpanStrikethrough) s |= EpdFontFamily::STRIKETHROUGH;
  if (span & kSpanSuper) s |= EpdFontFamily::SUP;
  if (span & kSpanSub) s |= EpdFontFamily::SUB;
  if (span & kSpanSmallCaps) s |= EpdFontFamily::SMALL_CAPS;
  return static_cast<EpdFontFamily::Style>(s);
}

}  // namespace

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
      embeddedStyle_(params.embeddedStyle),
      fontSizeLadder_(std::move(params.fontSizeLadder)),
      imageBasePath_(std::move(params.imageBasePath)),
      epubFilePath_(std::move(params.epubFilePath)) {}

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

BlockStyle LayoutSink::buildBlockStyle(const CssStyle& style, const bool isHeading) const {
  const float emSize = static_cast<float>(renderer_.getFontAscenderSize(fontId_));
  // Headings default to Center (cpp:1582); blocks default to paragraphAlignment (cpp:1563).
  const CssTextAlign defaultAlign = isHeading ? CssTextAlign::Center : static_cast<CssTextAlign>(paragraphAlignment_);
  BlockStyle bs = BlockStyle::fromCssStyle(style, emSize, defaultAlign, viewportWidth_);
  if (isHeading) bs.textAlignDefined = true;  // cpp:1583
  // Publisher text-align overrides the default when embeddedStyle and the user left alignment
  // at None (cpp:1584-1586 for headings, cpp:1644-1648 for blocks — identical condition).
  if (embeddedStyle_ && style.hasTextAlign() &&
      paragraphAlignment_ == static_cast<uint8_t>(CssTextAlign::None)) {
    bs.alignment = style.textAlign;
    bs.textAlignDefined = true;
  }
  return bs;
}

// Port of ChapterHtmlSlimParser::addLineToPage (cpp:2539). Image/float branches land in
// commit 3; the text-line placement + page-break + footnote assignment are complete here.
int LayoutSink::addLineToPage(const std::shared_ptr<TextBlock>& line, const bool lineEndsWithHyphenatedWord,
                              const bool suppressHyphenationRetry) {
  int lineHeight = effectiveLineHeight(line->getBlockStyle());
  const uint8_t maxPct = line->maxSizePct();
  if (maxPct != 100) {
    lineHeight = lineHeight * maxPct / 100;
  }

  if (!currentPage_) {
    currentPage_.reset(new Page());
    currentPageNextY_ = 0;
  }

  if (currentPageNextY_ + lineHeight > viewportHeight_) {
    emitPage(lastBodyChildByteOffset_);
  }

  const bool noRoomForAnotherLine = currentPageNextY_ + lineHeight <= viewportHeight_ &&
                                    currentPageNextY_ + (lineHeight * 2) > viewportHeight_;
  if (lineEndsWithHyphenatedWord && !suppressHyphenationRetry && noRoomForAnotherLine) {
    return static_cast<int>(ParsedText::LineProcessResult::RetryWithoutHyphenation);
  }

  const bool isFirstLineOfBlock = (wordsExtractedInBlock_ == 0);
  wordsExtractedInBlock_ += line->wordCount();

  // Assign any footnotes whose anchor word has now been laid out to the current page (cpp:2540).
  auto footnoteIt = pendingFootnotes_.begin();
  while (footnoteIt != pendingFootnotes_.end() && footnoteIt->first <= wordsExtractedInBlock_) {
    currentPage_->addFootnote(footnoteIt->second.number, footnoteIt->second.href);
    ++footnoteIt;
  }
  pendingFootnotes_.erase(pendingFootnotes_.begin(), footnoteIt);

  // Apply horizontal left inset. For lines overlapping an active LEFT float zone, also shift
  // right by the zone width so text starts after the image (cpp:2580-2593). Right floats narrow
  // the line width (handled in widthForLine) but don't shift text.
  int16_t xOffset = line->getBlockStyle().leftInset();
  {
    const auto& bs = line->getBlockStyle();
    for (int zi = 0; zi < bs.floatZoneCount; ++zi) {
      const auto& z = bs.floatZones[zi];
      if (!z.isRight && currentPageNextY_ < z.bottom && currentPageNextY_ + lineHeight > z.top) {
        xOffset = static_cast<int16_t>(xOffset + z.width);
      }
    }
  }
  currentPage_->elements.push_back(std::make_shared<PageLine>(line, xOffset, currentPageNextY_));

  // On the first line of a block with a deferred inline float image, fix the image's yPos to the
  // line top (cpp:2596-2603).
  if (isFirstLineOfBlock && deferredPageImage_) {
    deferredPageImage_->yPos = static_cast<int16_t>(currentPageNextY_);
    deferredPageImage_.reset();
  }

  currentPageNextY_ += lineHeight;
  return static_cast<int>(ParsedText::LineProcessResult::Accepted);
}

// Port of ChapterHtmlSlimParser::makePages (cpp:2609), text path. Active-float propagation
// (cpp:2664-2699) lands in commit 3; everything else — font resolve, margin collapse,
// effective width, layout, bottom spacing — is reproduced here.
void LayoutSink::makePages() {
  if (layoutFailed_) {
    currentTextBlock_.reset();
    return;
  }
  if (!currentTextBlock_) return;

  if (!currentPage_) {
    currentPage_.reset(new Page());
    currentPageNextY_ = 0;
  }

  if (!currentTextBlock_->isContinuation()) {
    currentTextBlock_->foldUniformWordSizes();
  }
  resolveBlockFont(currentTextBlock_->getBlockStyle());

  const BlockStyle& blockStyle = currentTextBlock_->getBlockStyle();
  const int lineHeight = effectiveLineHeight(blockStyle);

  if (!currentTextBlock_->isContinuation()) {
    if (blockStyle.marginTop > 0) {
      const int16_t collapse = std::min(lastBlockMarginBottom_, blockStyle.marginTop);
      currentPageNextY_ += static_cast<int16_t>(blockStyle.marginTop - collapse);
    }
    if (blockStyle.paddingTop > 0) {
      currentPageNextY_ += blockStyle.paddingTop;
    }
  }
  lastBlockMarginBottom_ = 0;

  const int horizontalInset = blockStyle.totalHorizontalInset();
  const uint16_t effectiveWidth =
      (horizontalInset < viewportWidth_) ? static_cast<uint16_t>(viewportWidth_ - horizontalInset) : viewportWidth_;

  // Active-float propagation (cpp:2664-2699). A tall float spans several blocks: it is attached
  // to the first (originating) block; here we re-inject the same zone into every later block that
  // still overlaps it vertically, then drop it once layout passes the image bottom.
  const bool isOriginatingBlock = static_cast<bool>(deferredPageImage_);
  if (activeFloatBottom_ > 0 && currentPageNextY_ >= activeFloatBottom_) {
    activeFloatBottom_ = 0;  // layout has moved past the image
  }
  if (!isOriginatingBlock && activeFloatBottom_ > 0 && currentPageNextY_ < activeFloatBottom_ &&
      currentTextBlock_->getBlockStyle().floatZoneCount == 0) {
    BlockStyle& mbs = currentTextBlock_->getBlockStyle();
    auto& z = mbs.floatZones[mbs.floatZoneCount++];
    z.top = activeFloatTop_;  // absolute (already-anchored) image coordinates
    z.bottom = activeFloatBottom_;
    z.width = activeFloatWidth_;
    z.isRight = activeFloatIsRight_;
  }

  // Pre-correct float zone coordinates before line-breaking so widthForLine and the xOffset
  // check in addLineToPage use the same y values (cpp:2683-2699). Only the originating block
  // re-anchors its zone (and the image) to the first line top; injected zones already carry
  // absolute image coordinates.
  const int lineHeightForFloat = (blockStyle.floatZoneCount > 0) ? effectiveLineHeight(blockStyle) : 0;
  if (isOriginatingBlock && blockStyle.floatZoneCount > 0) {
    BlockStyle& mbs = currentTextBlock_->getBlockStyle();
    for (int zi = 0; zi < mbs.floatZoneCount; ++zi) {
      const int imgH = mbs.floatZones[zi].bottom - mbs.floatZones[zi].top;
      mbs.floatZones[zi].top = static_cast<int16_t>(currentPageNextY_);
      mbs.floatZones[zi].bottom = static_cast<int16_t>(currentPageNextY_ + imgH);
    }
    activeFloatTop_ = static_cast<int16_t>(currentPageNextY_);
    activeFloatBottom_ = static_cast<int16_t>(currentPageNextY_ + (mbs.floatZones[0].bottom - mbs.floatZones[0].top));
  }

  currentTextBlock_->layoutAndExtractLines(
      renderer_, fontId_, effectiveWidth,
      [this](const std::shared_ptr<TextBlock>& textBlock, const bool lineEndsWithHyphenatedWord,
             const bool suppressHyphenationRetry) {
        return static_cast<ParsedText::LineProcessResult>(
            addLineToPage(textBlock, lineEndsWithHyphenatedWord, suppressHyphenationRetry));
      },
      /*includeLastLine=*/true, static_cast<int16_t>(currentPageNextY_), lineHeightForFloat);

  // Fallback: transfer any remaining pending footnotes to the current page (cpp:2678-2682).
  // Catches edge cases where a footnote's word index equals the exact block size.
  if (!pendingFootnotes_.empty() && currentPage_) {
    for (const auto& [idx, fn] : pendingFootnotes_) {
      currentPage_->addFootnote(fn.number, fn.href);
    }
    pendingFootnotes_.clear();
  }

  if (blockStyle.marginBottom > 0) {
    currentPageNextY_ += blockStyle.marginBottom;
    lastBlockMarginBottom_ = blockStyle.marginBottom;
  } else {
    lastBlockMarginBottom_ = 0;
  }
  if (blockStyle.paddingBottom > 0) {
    currentPageNextY_ += blockStyle.paddingBottom;
  }

  // Extra paragraph spacing, suppressed inside <pre> (kPreformatted) so code/preformatted lines
  // are single-spaced — matching the fused makePages gate on preUntilDepth (cpp:2733).
  if (extraParagraphSpacing_ && !currentBlockPreformatted_) {
    currentPageNextY_ += lineHeight / 2;
  }
}

void LayoutSink::layoutTextBlock(Block&& block, const BlockStyle& blockStyle) {
  currentBlockPreformatted_ = (block.flags & kPreformatted) != 0;
  currentTextBlock_.reset(new (std::nothrow) ParsedText(extraParagraphSpacing_, hyphenationEnabled_, blockStyle,
                                                        bionicReadingEnabled_));
  if (!currentTextBlock_) return;
  wordsExtractedInBlock_ = 0;

  // A float image rides on this Text block: attach it before layout so its zone is present when
  // the first line breaks (fused attaches on the first word, cpp:403-405). The image's own CSS
  // is not transmitted for floats — the intrinsic dims + default sizing suffice (the float gate
  // already capped size), so pass an empty style to the shared helper.
  if (!block.inlineImageEntryPath.empty()) {
    attachFloatImage(block, CssStyle{}, currentTextBlock_->getBlockStyle());
  }

  // Add words through the same ParsedText::addWord path the fused walk uses, replaying the
  // >96-word mid-block split (flushPartWordBuffer cpp:409-459) so page breaks land identically.
  for (const Word& w : block.words) {
    const char* text = &block.text[w.textOff];
    const EpdFontFamily::Style fontStyle = spanToFontStyle(w.styleSpan);
    const bool attach = (w.styleSpan & kSpanAttachPrev) != 0;
    currentTextBlock_->addWord(text, fontStyle, /*underline=*/false, attach, w.sizePct);

    if (currentTextBlock_->size() > 96) {
      auto& splitBlockStyle = currentTextBlock_->getBlockStyle();
      resolveBlockFont(splitBlockStyle);
      const int horizontalInset = splitBlockStyle.totalHorizontalInset();
      const uint16_t effectiveWidth = (horizontalInset < viewportWidth_)
                                          ? static_cast<uint16_t>(viewportWidth_ - horizontalInset)
                                          : viewportWidth_;
      currentTextBlock_->layoutAndExtractLines(
          renderer_, fontId_, effectiveWidth,
          [this](const std::shared_ptr<TextBlock>& textBlock, const bool lineEndsWithHyphenatedWord,
                 const bool suppressHyphenationRetry) {
            return static_cast<ParsedText::LineProcessResult>(
                addLineToPage(textBlock, lineEndsWithHyphenatedWord, suppressHyphenationRetry));
          },
          /*includeLastLine=*/false, static_cast<int16_t>(currentPageNextY_), /*lineHeight=*/0);
    }
  }
  makePages();
}

std::string LayoutSink::nextImageCachePath(const std::string& entryPath) {
  std::string ext;
  const size_t extPos = entryPath.rfind('.');
  if (extPos != std::string::npos) ext = entryPath.substr(extPos);
  return imageBasePath_ + std::to_string(imageCounter_++) + ext;
}

// Port of the fused <img> block-image path (ChapterHtmlSlimParser.cpp block branch). The pending
// empty-block style (figure/div/h1 wrapper margins) becomes the image's surrounding spacing —
// this is the LayoutSink analogue of the fused pendingImageBlockStyle, sourced from the pending
// merge accumulated by onBlock.
void LayoutSink::placeBlockImage(const Block& block, const CssStyle& imgStyle) {
  // Container width for CSS sizing = viewport minus the pending block's horizontal inset.
  int containerWidth = viewportWidth_;
  if (hasPendingMerge_) {
    const int inset = pendingMergeStyle_.totalHorizontalInset();
    if (inset > 0 && inset < viewportWidth_) containerWidth = viewportWidth_ - inset;
  }
  const float emSize = static_cast<float>(renderer_.getFontAscenderSize(fontId_));
  const ImageDisplaySize ds =
      computeImageDisplaySize(block.width, block.height, imgStyle, viewportWidth_, viewportHeight_, containerWidth,
                              emSize);

  // Spacing from the pending empty-block wrapper style (fused cpp:1289-1298).
  int spacingTop = 0;
  int spacingBottom = 0;
  if (hasPendingMerge_) {
    spacingTop = std::max(0, static_cast<int>(pendingMergeStyle_.marginTop)) +
                 std::max(0, static_cast<int>(pendingMergeStyle_.paddingTop));
    spacingBottom = std::max(0, static_cast<int>(pendingMergeStyle_.marginBottom)) +
                    std::max(0, static_cast<int>(pendingMergeStyle_.paddingBottom));
  }
  // The pending wrapper spacing is consumed around the image; it must not leak into the next
  // paragraph (fused resets the empty block after the image).
  hasPendingMerge_ = false;
  pendingMergeFromBr_ = false;

  const int totalHeight = spacingTop + ds.height + spacingBottom;
  if (currentPage_ && !currentPage_->elements.empty() && (currentPageNextY_ + totalHeight > viewportHeight_)) {
    emitPage(lastBodyChildByteOffset_);
  } else if (!currentPage_) {
    currentPage_.reset(new Page());
    currentPageNextY_ = 0;
  }

  currentPageNextY_ += static_cast<int16_t>(spacingTop);

  const std::string cachePath = nextImageCachePath(block.entryPath);
  auto imageBlock = std::make_shared<ImageBlock>(cachePath, static_cast<int16_t>(ds.width),
                                                 static_cast<int16_t>(ds.height), block.alt, epubFilePath_,
                                                 block.entryPath);
  const int16_t xPos = static_cast<int16_t>((viewportWidth_ - ds.width) / 2);
  currentPage_->elements.push_back(std::make_shared<PageImage>(imageBlock, xPos, currentPageNextY_));
  currentPageNextY_ += static_cast<int16_t>(ds.height);
  currentPageNextY_ += static_cast<int16_t>(spacingBottom);
}

std::unique_ptr<ParsedText> LayoutSink::buildCellText(const TableCell& cell) const {
  // Table cells are built with NO paragraph spacing and NO hyphenation, matching the fused
  // BufferedTableCell (ChapterHtmlSlimParser.cpp:1077 `new ParsedText(false, false)`). Using the
  // sink's profile flags here would give cells a first-line indent / hyphenation the grid + the
  // paragraph fallback never apply.
  auto pt = std::unique_ptr<ParsedText>(new ParsedText(/*extraParagraphSpacing=*/false,
                                                       /*hyphenationEnabled=*/false));
  for (const Word& w : cell.words) {
    const char* text = &cell.text[w.textOff];
    const EpdFontFamily::Style fontStyle = spanToFontStyle(w.styleSpan);
    const bool attach = (w.styleSpan & kSpanAttachPrev) != 0;
    pt->addWord(text, fontStyle, /*underline=*/false, attach, w.sizePct);
  }
  return pt;
}

// Paragraph fallback: each non-empty cell's text becomes a sequential paragraph laid out at full
// viewport width (mirrors emitTableAsParagraphs cpp:3030-3051).
void LayoutSink::placeTableAsParagraphs(const Block& block) {
  for (const TableRow& row : block.rows) {
    for (const TableCell& cell : row.cells) {
      if (cell.words.empty()) continue;
      // The fused fallback (cpp:3041-3048) calls startNewTextBlock(cellBlockStyle) to open a block
      // but then lays out cell.text — which keeps its OWN default BlockStyle (Justify, no indent),
      // NOT cellBlockStyle. So the cell alignment is the ParsedText default here, not
      // paragraphAlignment. wordsExtractedInBlock resets per cell (a fresh startNewTextBlock).
      wordsExtractedInBlock_ = 0;
      auto cellText = buildCellText(cell);
      cellText->layoutAndExtractLines(
          renderer_, fontId_, viewportWidth_,
          [this](const std::shared_ptr<TextBlock>& tb, const bool h, const bool s) {
            return static_cast<ParsedText::LineProcessResult>(addLineToPage(tb, h, s));
          });
    }
  }
}

// Grid table layout (mirrors emitTableAsFragments cpp:2839-3028). Wraps each cell, computes row
// heights, then packs fragments via the shared helper. Cell images are NOT reproduced: the
// producer does not transmit their intrinsic dims (stage1EmitTableBlock), and no corpus book uses
// grid cell images. Falls back to paragraphs on the same conditions as the fused path.
void LayoutSink::placeTable(const Block& block) {
  if (block.rows.empty()) {
    placeTableAsParagraphs(block);
    return;
  }
  uint8_t columnCount = 0;
  for (const TableRow& r : block.rows) columnCount = std::max(columnCount, static_cast<uint8_t>(r.cells.size()));
  // colSpan-aware column count (fused uses table.maxCols); recompute from the max effective cols.
  for (const TableRow& r : block.rows) {
    uint8_t eff = 0;
    for (const TableCell& c : r.cells) eff = static_cast<uint8_t>(eff + c.colSpan);
    columnCount = std::max(columnCount, eff);
  }
  if (columnCount == 0 || columnCount > MAX_TABLE_COLS) {
    placeTableAsParagraphs(block);
    return;
  }

  const uint16_t totalWidth = viewportWidth_;
  const uint16_t colWidth = totalWidth / columnCount;
  const uint16_t innerColWidth =
      (colWidth > 2 * TABLE_CELL_PADDING) ? static_cast<uint16_t>(colWidth - 2 * TABLE_CELL_PADDING) : 0;
  if (innerColWidth < MIN_COL_INNER_WIDTH) {
    placeTableAsParagraphs(block);
    return;
  }

  const int lineHeight = static_cast<int>(renderer_.getLineHeight(fontId_) * lineCompression_ + 0.5f);
  const bool hasBorder = block.hasBorder;

  std::vector<TableLayoutRow> layoutRows;
  layoutRows.reserve(block.rows.size());
  bool needsParagraphFallback = false;

  for (const TableRow& bufRow : block.rows) {
    const bool isFullWidthSpan = bufRow.cells.size() == 1 && bufRow.cells[0].colSpan == columnCount;
    const uint8_t renderCols = isFullWidthSpan ? 1 : columnCount;
    const uint16_t renderColWidth = totalWidth / renderCols;
    const uint16_t renderInnerWidth =
        (renderColWidth > 2 * TABLE_CELL_PADDING) ? static_cast<uint16_t>(renderColWidth - 2 * TABLE_CELL_PADDING) : 0;

    const bool hasMergedCell =
        std::any_of(bufRow.cells.begin(), bufRow.cells.end(), [](const TableCell& c) { return c.colSpan != 1; });
    if (hasMergedCell && !isFullWidthSpan) {
      needsParagraphFallback = true;
      break;
    }

    TableLayoutRow lr;
    lr.isHeaderRow = bufRow.isHeaderRow;
    lr.renderCols = renderCols;
    lr.cells.reserve(renderCols);
    uint16_t maxContentHeight = 0;

    for (const TableCell& bufCell : bufRow.cells) {
      ::TableCell cell;
      cell.isHeader = bufCell.isHeader;
      if (!bufCell.words.empty()) {
        auto cellText = buildCellText(bufCell);
        cellText->layoutAndExtractLines(renderer_, fontId_, renderInnerWidth,
                                        [&cell](const std::shared_ptr<TextBlock>& tb, bool, bool) {
                                          if (cell.lines.size() < MAX_CELL_LINES) cell.lines.push_back(tb);
                                          return ParsedText::LineProcessResult::Accepted;
                                        });
      }
      // Cell images intentionally not reproduced (see the method comment).
      uint16_t contentHeight = static_cast<uint16_t>(cell.lines.size() * lineHeight);
      if (contentHeight > maxContentHeight) maxContentHeight = contentHeight;
      lr.cells.push_back(std::move(cell));
    }
    if (!isFullWidthSpan) {
      while (lr.cells.size() < columnCount) lr.cells.emplace_back();
    }
    if (maxContentHeight == 0) maxContentHeight = static_cast<uint16_t>(lineHeight);
    lr.height = static_cast<uint16_t>(maxContentHeight + 2 * TABLE_CELL_PADDING);
    layoutRows.push_back(std::move(lr));
  }

  if (needsParagraphFallback) {
    placeTableAsParagraphs(block);
    return;
  }

  struct SinkTableCtx final : TablePageContext {
    LayoutSink* self;
    int currentY() const override { return self->currentPageNextY_; }
    void ensurePage() override {
      if (!self->currentPage_) {
        self->currentPage_.reset(new Page());
        self->currentPageNextY_ = 0;
      }
    }
    void emitPageAndReset() override { self->emitPage(self->lastBodyChildByteOffset_); }
    void advanceY(int delta) override { self->currentPageNextY_ = static_cast<int16_t>(self->currentPageNextY_ + delta); }
    void pushFragment(uint8_t cols, uint16_t tw, uint16_t th, std::vector<::TableRow>&& rows, int16_t yPos,
                      bool border) override {
      self->currentPage_->elements.push_back(
          std::make_shared<PageTableFragment>(cols, tw, th, std::move(rows), /*xPos=*/0, yPos, border));
    }
    void onOversizeRow(const TableLayoutRow& lr) override {
      // Over-tall row → flatten to paragraphs. Rebuild a minimal compiled block from the wrapped
      // lines' words and route through placeTableAsParagraphs (cell images are already dropped).
      Block fb;
      fb.type = BlockType::Table;
      TableRow r;
      r.isHeaderRow = lr.isHeaderRow;
      for (const auto& c : lr.cells) {
        TableCell cc;
        cc.isHeader = c.isHeader;
        for (const auto& line : c.lines) {
          for (uint16_t i = 0; i < line->wordCount(); ++i) {
            Word w;
            w.textOff = static_cast<uint32_t>(cc.text.size());
            cc.words.push_back(w);
            cc.text.append(line->wordText(i));
            cc.text.push_back('\0');
          }
        }
        r.cells.push_back(std::move(cc));
      }
      fb.rows.push_back(std::move(r));
      self->placeTableAsParagraphs(fb);
    }
  } ctx;
  ctx.self = this;
  packTableFragments(layoutRows, totalWidth, viewportHeight_, hasBorder, ctx);
}

void LayoutSink::placeHr() {
  // The fused <hr> handler calls makePages() first, which lays out the currentTextBlock — even
  // when it is empty (e.g. the empty block between two consecutive <hr/>s). An empty block still
  // advances Y by its margins + extra paragraph spacing. Reproduce that: if an empty-merge block
  // is pending, materialize and lay it out (no words → just its spacing) before the rule.
  if (hasPendingMerge_) {
    currentBlockPreformatted_ = false;
    currentTextBlock_.reset(new (std::nothrow) ParsedText(extraParagraphSpacing_, hyphenationEnabled_,
                                                          pendingMergeStyle_, bionicReadingEnabled_));
    hasPendingMerge_ = false;
    pendingMergeFromBr_ = false;
    if (currentTextBlock_) {
      wordsExtractedInBlock_ = 0;
      makePages();
    }
  }

  if (!currentPage_) {
    currentPage_.reset(new Page());
    currentPageNextY_ = 0;
  }
  const int lineHeight = static_cast<int>(renderer_.getLineHeight(fontId_) * lineCompression_ + 0.5f);
  const int16_t marginV = static_cast<int16_t>(lineHeight / 2);
  currentPageNextY_ += marginV;
  if (currentPageNextY_ + 1 + marginV > viewportHeight_) {
    emitPage(lastBodyChildByteOffset_);
    currentPage_.reset(new Page());
    currentPageNextY_ = 0;
  }
  const int16_t hrWidth = static_cast<int16_t>(viewportWidth_ / 2);
  const int16_t hrX = static_cast<int16_t>(viewportWidth_ / 4);
  currentPage_->elements.push_back(std::make_shared<PageHR>(hrX, currentPageNextY_, hrWidth));
  currentPageNextY_ += 1 + marginV;
}

void LayoutSink::attachFloatImage(const Block& block, const CssStyle& imgStyle, BlockStyle& bs) {
  if (!currentPage_) currentPage_.reset(new (std::nothrow) Page());

  // The producer transmits INTRINSIC dims; recompute the display dims exactly as the fused
  // isInlineCandidate gate did (via the shared helper) before using them as the float size.
  int containerWidth = viewportWidth_;
  const int inset = bs.totalHorizontalInset();
  if (inset > 0 && inset < viewportWidth_) containerWidth = viewportWidth_ - inset;
  const float emSize = static_cast<float>(renderer_.getFontAscenderSize(fontId_));
  const ImageDisplaySize ds = computeImageDisplaySize(block.inlineImageWidth, block.inlineImageHeight, imgStyle,
                                                      viewportWidth_, viewportHeight_, containerWidth, emSize);
  const int16_t imgH = static_cast<int16_t>(ds.height);
  const int16_t imgW = static_cast<int16_t>(ds.width);
  const bool imgIsRight = (block.inlineImageSide == 2);

  // A float never crosses a page boundary: break first if it would not fit (cpp:532-534).
  if (imgH > static_cast<int16_t>(viewportHeight_ - currentPageNextY_) && currentPage_ &&
      !currentPage_->elements.empty()) {
    emitPage(lastBodyChildByteOffset_);
  }

  const int16_t imgX = imgIsRight ? static_cast<int16_t>(viewportWidth_ - imgW) : 0;
  const int16_t top = static_cast<int16_t>(currentPageNextY_);

  const std::string cachePath = nextImageCachePath(block.inlineImageEntryPath);
  auto fullImageBlock =
      std::make_shared<ImageBlock>(cachePath, imgW, imgH, block.inlineImageAlt, epubFilePath_, block.inlineImageEntryPath);
  deferredPageImage_ = std::make_shared<PageImage>(fullImageBlock, imgX, top);
  currentPage_->elements.push_back(deferredPageImage_);

  if (bs.floatZoneCount < BlockStyle::kMaxFloatZones) {
    auto& z = bs.floatZones[bs.floatZoneCount++];
    z.top = top;
    z.bottom = static_cast<int16_t>(top + imgH);
    z.width = static_cast<int16_t>(imgW + 4);
    z.isRight = imgIsRight;
  }
  activeFloatTop_ = top;
  activeFloatBottom_ = static_cast<int16_t>(top + imgH);
  activeFloatWidth_ = static_cast<int16_t>(imgW + 4);
  activeFloatIsRight_ = imgIsRight;
}

// --- BlockSink overrides. ---

void LayoutSink::onBlock(Block&& block, const CssStyle& style) {
  // A TOC-boundary (or CSS page-break-before) block starts a fresh page (fused forces this in
  // startNewTextBlock when the pending anchor is a TOC anchor).
  if ((block.flags & kPageBreakBefore) && currentPage_ && !currentPage_->elements.empty()) {
    emitPage(lastBodyChildByteOffset_);
  }

  if (block.type == BlockType::Image) {
    // A standalone (centered) block image. floatSide != 0 would be a float, but the producer
    // emits floats as fields on the following Text block, not as Image blocks — so every Image
    // block here is a centered block image.
    placeBlockImage(block, style);
    return;
  }
  if (block.type == BlockType::Hr) {
    placeHr();
    return;
  }
  if (block.type == BlockType::Table) {
    placeTable(block);
    return;
  }
  if (block.type != BlockType::Text) {
    return;
  }

  const bool isHeading = (block.flags & kStartsChapter) != 0;
  const bool fromBr = (block.flags & kFromBrElement) != 0;

  // The alignment CONTEXT a <br> inherits is the fused parser's *current block* alignment at the
  // <br> (cpp:1633) — which, when an empty block is being reused, is that empty block's alignment
  // AFTER the endElement reset (see below). Model it as the live merge context: the pending empty
  // chain's alignment if one is open, else the last laid-out block's. NOT lastBlockAlignment_
  // alone, which ignores intervening (reset) empty blocks and wrongly carried a heading's Center
  // across the chain (Moby Dick <p class="toc"> regression).
  const CssTextAlign contextAlign = hasPendingMerge_ ? pendingMergeStyle_.alignment : lastBlockAlignment_;
  const bool contextAlignDefined = hasPendingMerge_ ? pendingMergeStyle_.textAlignDefined : lastBlockAlignmentDefined_;

  BlockStyle blockStyle;
  if (fromBr) {
    // EVERY <br> block gets a NEUTRAL layout style (fused cpp:1639-1642): only the current
    // alignment context, never the element's CSS margins — those would over-space and (for an
    // empty <br>) mis-place the injected line-gap. This holds whether the block stays empty (a
    // section separator) or later receives text (an inline <br>, or a poem line). The one CSS
    // property a <br> block DOES carry is a span-level text-indent (poem stanza pattern): it is
    // applied to the open <br> block after brStyle, so the producer transmits it via textIndent.
    blockStyle.alignment = contextAlign;
    blockStyle.textAlignDefined = contextAlignDefined;
    blockStyle.fromBrElement = true;
    if (style.hasTextIndent()) {
      const float emSize = static_cast<float>(renderer_.getFontAscenderSize(fontId_));
      blockStyle.textIndent = style.textIndent.toPixelsInt16(emSize, static_cast<float>(viewportWidth_));
      blockStyle.textIndentDefined = true;
    }
  } else {
    blockStyle = buildBlockStyle(style, isHeading);
  }

  // Empty wrapper / <br> transcript block: don't lay it out. Accumulate its style into a
  // pending merge that folds into the next non-empty block, reproducing the fused path's
  // empty-currentTextBlock reuse (startNewTextBlock cpp:752-796).
  if (block.words.empty()) {
    // Reproduce the fused endElement empty-block ALIGNMENT RESET (cpp:2365-2377, issue #1026): on
    // closing a header/block tag that stayed empty, the fused path clears the block's alignment so
    // a centered empty heading does not bleed its (Center, textAlignDefined=true) alignment through
    // the empty-block-reuse chain into the next paragraph. The reset fires for every empty block
    // EXCEPT a <br> (which preserves alignment for text in the same container, cpp:2370). This
    // reset alignment also becomes the context a following <br> inherits (above) — matching the
    // fused <br> reading the just-reset current block. Alignment only; margins/padding accumulate
    // through the merge untouched. The producer can't transmit this (a tag-close layout mutation).
    if (!blockStyle.fromBrElement) {
      blockStyle.textAlignDefined = false;
      blockStyle.alignment = (paragraphAlignment_ == static_cast<uint8_t>(CssTextAlign::None))
                                 ? CssTextAlign::Justify
                                 : static_cast<CssTextAlign>(paragraphAlignment_);
    }
    if (hasPendingMerge_) {
      // Chain of consecutive empty blocks: combine styles as the fused reuse path does.
      BlockStyle incoming = blockStyle;
      if (pendingMergeFromBr_) {
        const int16_t lh = static_cast<int16_t>(renderer_.getLineHeight(fontId_) * lineCompression_ + 0.5f);
        incoming.marginTop = static_cast<int16_t>(incoming.marginTop + lh);
      }
      pendingMergeStyle_ = pendingMergeStyle_.getCombinedBlockStyle(incoming);
    } else {
      pendingMergeStyle_ = blockStyle;
    }
    pendingMergeFromBr_ = blockStyle.fromBrElement;
    hasPendingMerge_ = true;
    return;
  }

  // Non-empty block: fold any pending empty-block merge into its style first.
  if (hasPendingMerge_) {
    BlockStyle incoming = blockStyle;
    if (pendingMergeFromBr_) {
      const int16_t lh = static_cast<int16_t>(renderer_.getLineHeight(fontId_) * lineCompression_ + 0.5f);
      incoming.marginTop = static_cast<int16_t>(incoming.marginTop + lh);
    }
    blockStyle = pendingMergeStyle_.getCombinedBlockStyle(incoming);
    hasPendingMerge_ = false;
    pendingMergeFromBr_ = false;
  }

  // Track the alignment context for a subsequent <br> block (fused reads it from the current
  // block at the <br>). Captured from the final merged style actually laid out.
  lastBlockAlignment_ = blockStyle.alignment;
  lastBlockAlignmentDefined_ = blockStyle.textAlignDefined;

  layoutTextBlock(std::move(block), blockStyle);
}

void LayoutSink::onAnchor(const std::string& id) { pendingAnchorId_ = id; }

void LayoutSink::onChapter(uint8_t /*level*/, const std::string& /*title*/) {}

void LayoutSink::onPageBreakLabel(const std::string& label) {
  if (label.empty()) return;
  pageBreakLabels_.emplace_back(static_cast<uint16_t>(completedPageCount_), label);
}

void LayoutSink::onFootnote(int wordIndex, const FootnoteEntry& entry) {
  // Buffered until layout reaches this word position (addLineToPage), matching the fused
  // pendingFootnotes machinery. wordIndex is relative to the block currently being built.
  pendingFootnotes_.push_back({wordIndex, entry});
}

void LayoutSink::onXPathAdvance(uint16_t paragraphIndex, uint16_t listItemIndex, uint32_t bodyChildByteOffset) {
  // The walk's counters as of the block about to be laid out; emitPage records them into the LUT.
  xpathParagraphIndex_ = paragraphIndex;
  xpathListItemIndex_ = listItemIndex;
  lastBodyChildByteOffset_ = bodyChildByteOffset;
}

void LayoutSink::onSpineEnd() {
  // Flush the last accumulated text block (finalize() cpp:2493-2509).
  if (currentTextBlock_) {
    makePages();
    if (layoutFailed_) return;
  }
  // Emit the final page if it has content — independent of whether a text block is open. A spine
  // whose only element is an image/table/HR (e.g. a cover page) has no currentTextBlock_ but still
  // has a page to flush. (The fused parser always holds an initial empty block from setup(), so it
  // never hit this; the sink only creates one when a Text block arrives.)
  if (currentPage_ && !currentPage_->elements.empty()) {
    emitPage(0u);
  }
}

}  // namespace compiled
