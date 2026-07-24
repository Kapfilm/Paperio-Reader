#include "LayoutSink.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "Epub/Page.h"
#include "Epub/ParsedText.h"
#include "Epub/blocks/ImageBlock.h"
#include "Epub/blocks/TextBlock.h"
#include "ImageLayout.h"

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

  if (blockStyle.marginBottom > 0) {
    currentPageNextY_ += blockStyle.marginBottom;
    lastBlockMarginBottom_ = blockStyle.marginBottom;
  } else {
    lastBlockMarginBottom_ = 0;
  }
  if (blockStyle.paddingBottom > 0) {
    currentPageNextY_ += blockStyle.paddingBottom;
  }

  // Extra paragraph spacing (default). The <pre>-suppression the fused path applies is a
  // walk concern (preUntilDepth); a compiled block carries no pre flag yet, so this always
  // applies here — the text corpus has no <pre>, and pre handling folds in with a later flag.
  if (extraParagraphSpacing_) {
    currentPageNextY_ += lineHeight / 2;
  }
}

void LayoutSink::layoutTextBlock(Block&& block, const BlockStyle& blockStyle) {
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

void LayoutSink::placeHr() {
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
  if (block.type != BlockType::Text) {
    // Table blocks land in commit 4.
    return;
  }

  const bool isHeading = (block.flags & kStartsChapter) != 0;
  const bool fromBr = (block.flags & kFromBrElement) != 0;

  BlockStyle blockStyle;
  if (fromBr) {
    // EVERY <br> block gets a NEUTRAL layout style (fused cpp:1639-1642): only the current
    // alignment context, never the element's CSS margins — those would over-space and (for an
    // empty <br>) mis-place the injected line-gap. This holds whether the block stays empty (a
    // section separator) or later receives text (an inline <br>, or a poem line). The one CSS
    // property a <br> block DOES carry is a span-level text-indent (poem stanza pattern): it is
    // applied to the open <br> block after brStyle, so the producer transmits it via textIndent.
    blockStyle.alignment = lastBlockAlignment_;
    blockStyle.textAlignDefined = lastBlockAlignmentDefined_;
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

void LayoutSink::onFootnote(int /*wordIndex*/, const FootnoteEntry& /*entry*/) {}

void LayoutSink::onSpineEnd() {
  // Flush the last accumulated block and emit the final page (finalize() cpp:2493-2509).
  if (currentTextBlock_) {
    makePages();
    if (!layoutFailed_) {
      const bool hasFinalPageContent = currentPage_ && !currentPage_->elements.empty();
      if (hasFinalPageContent) {
        emitPage(0u);
      }
    }
  }
}

}  // namespace compiled
