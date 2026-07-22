#pragma once
// LayoutSink — the Stage-2 (measure+paginate) consumer of the walk's BlockSink stream
// (Phase 3 step 5 of docs/compiled-book-pipeline-plan.md; design in
// docs/parser-stage1-step5-design.md and stage1-extraction-design.md).
//
// Given the walk's onBlock(Block&&, CssStyle&) stream, LayoutSink reproduces the SAME
// sequence of Page objects the fused ChapterHtmlSlimParser layout produces via its
// completePageFn — byte-identical, across the settings matrix. It owns all the
// settings-dependent layout state the fused parser holds (currentTextBlock, currentPage,
// float state, anchor/label/LUT tables) and holds a GfxRenderer& for measurement.
//
// Step 5 is ADDITIVE: LayoutSink is a SECOND, parallel consumer. The fused layout path in
// ChapterHtmlSlimParser is untouched and keeps shipping. The equivalence test diffs
// LayoutSink's page dump against the fused goldens. The actual unify — removing the fused
// inline layout and driving only sinks, extracting HtmlWalkCore — is step 6.
//
// Compiles unconditionally but is inert on device: nothing on the shipping path constructs
// a LayoutSink (exactly like ContentSink). Only the host equivalence test / driver drive it.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "BlockSink.h"
#include "CompiledContent.h"
#include "Epub/FontSizeLadder.h"
#include "Epub/blocks/BlockStyle.h"
#include "Epub/css/CssStyle.h"

class GfxRenderer;
class Page;
class PageImage;
class ParsedText;
class TextBlock;
struct FootnoteEntry;

namespace compiled {

// The settings-dependent inputs the fused parser takes as ctor args. Bundled so the sink's
// layout math matches the fused path exactly (same font, viewport, spacing, alignment).
struct LayoutParams {
  int fontId = 0;
  float lineCompression = 1.0f;
  bool extraParagraphSpacing = false;
  uint8_t paragraphAlignment = 0;
  uint16_t viewportWidth = 0;
  uint16_t viewportHeight = 0;
  bool hyphenationEnabled = false;
  bool bionicReadingEnabled = false;
  bool embeddedStyle = true;  // honor publisher CSS text-align (see the alignment resolution)
  FontSizeLadder fontSizeLadder;  // body-font sibling-size ladder (see resolveBlockFont)
};

// Per-page XPath LUT entry — mirrors ChapterHtmlSlimParser::ParagraphLutEntry so the
// equivalence test can compare the two tables directly.
struct LayoutLutEntry {
  uint32_t xhtmlByteOffset = 0;
  uint16_t paragraphIndex = 0;
  uint16_t listItemIndex = 0;
};

class LayoutSink : public BlockSink {
 public:
  LayoutSink(GfxRenderer& renderer, LayoutParams params,
             std::function<void(std::unique_ptr<Page>)> completePageFn);
  ~LayoutSink() override;

  // BlockSink — driven by the walk during a Section build.
  void onBlock(Block&& block, const CssStyle& style) override;
  void onAnchor(const std::string& id) override;
  void onChapter(uint8_t level, const std::string& title) override;
  void onPageBreakLabel(const std::string& label) override;
  void onFootnote(int wordIndex, const FootnoteEntry& entry) override;
  void onSpineEnd() override;

  // Side outputs the caller pulls today via the parser getters (getAnchors / etc.).
  const std::vector<std::pair<std::string, uint16_t>>& anchors() const { return anchorData_; }
  const std::vector<std::pair<uint16_t, std::string>>& pageBreakLabels() const { return pageBreakLabels_; }
  const std::vector<LayoutLutEntry>& paragraphLutPerPage() const { return paragraphLutPerPage_; }

 private:
  // --- Layout helpers, copied verbatim from ChapterHtmlSlimParser (renderer-local). ---
  void resolveBlockFont(BlockStyle& bs);
  int effectiveFontId(const BlockStyle& bs) const { return bs.headingFontId != 0 ? bs.headingFontId : fontId_; }
  int effectiveLineHeight(const BlockStyle& bs) const;
  void emitPage(uint32_t xhtmlByteOffset);
  // Layout the current block into pages (port of ChapterHtmlSlimParser::makePages, text path).
  void makePages();
  // Append one laid-out line to the current page (port of addLineToPage).
  int addLineToPage(const std::shared_ptr<TextBlock>& line, bool lineEndsWithHyphenatedWord,
                    bool suppressHyphenationRetry);
  // Reconstruct the px BlockStyle from the block's pre-px CssStyle. Alignment stays sink-side
  // (it depends on paragraphAlignment, a user setting): headings default to Center, blocks to
  // paragraphAlignment, and publisher text-align overrides when embeddedStyle. The heading
  // font-size multiplier is already folded into `style` by the producer (settings-independent).
  BlockStyle buildBlockStyle(const CssStyle& style, bool isHeading) const;
  // Rebuild a ParsedText from a materialized text block, adding words through the same
  // ParsedText::addWord path the fused walk uses (and replaying the >96-word split).
  void layoutTextBlock(Block&& block, const BlockStyle& blockStyle);

  GfxRenderer& renderer_;
  std::function<void(std::unique_ptr<Page>)> completePageFn_;

  // Cached from params_ for the hot layout math (mirrors the parser's scalar members).
  const int fontId_;
  const float lineCompression_;
  const bool extraParagraphSpacing_;
  const uint8_t paragraphAlignment_;
  const uint16_t viewportWidth_;
  const uint16_t viewportHeight_;
  const bool hyphenationEnabled_;
  const bool bionicReadingEnabled_;
  const bool embeddedStyle_;
  FontSizeLadder fontSizeLadder_;

  // Empty-block merge reconstruction. The producer emits empty wrapper / <br> blocks as a
  // 1:1 transcript; the fused path merges their styles into the following paragraph (reusing
  // its empty currentTextBlock). We instead hold the accumulated merged style across empty
  // blocks and fold it into the next non-empty block's style — replaying the same margins.
  bool hasPendingMerge_ = false;
  BlockStyle pendingMergeStyle_;
  bool pendingMergeFromBr_ = false;
  // Alignment context a <br> block inherits (fused: the current block's alignment at <br>).
  CssTextAlign lastBlockAlignment_ = CssTextAlign::Justify;
  bool lastBlockAlignmentDefined_ = false;

  // Layout state (moved from ChapterHtmlSlimParser — see the design doc's state table).
  std::unique_ptr<ParsedText> currentTextBlock_;
  std::unique_ptr<Page> currentPage_;
  int16_t currentPageNextY_ = 0;
  int16_t lastBlockMarginBottom_ = 0;
  int32_t auxFontId_ = 0;
  int completedPageCount_ = 0;
  int wordsExtractedInBlock_ = 0;
  bool layoutFailed_ = false;

  // Float / deferred-image zone state.
  std::shared_ptr<PageImage> deferredPageImage_;
  int16_t activeFloatTop_ = 0;
  int16_t activeFloatBottom_ = 0;
  int16_t activeFloatWidth_ = 0;
  bool activeFloatIsRight_ = false;

  // Side-output tables (pulled by the caller after onSpineEnd).
  std::vector<std::pair<std::string, uint16_t>> anchorData_;
  std::string pendingAnchorId_;
  std::vector<std::pair<uint16_t, std::string>> pageBreakLabels_;
  std::vector<LayoutLutEntry> paragraphLutPerPage_;

  // XPath indices at page-break time (fed by onChapter/onBlock in later commits).
  uint16_t xpathParagraphIndex_ = 0;
  uint16_t xpathListItemIndex_ = 0;
  uint32_t lastBodyChildByteOffset_ = 0;
};

}  // namespace compiled
