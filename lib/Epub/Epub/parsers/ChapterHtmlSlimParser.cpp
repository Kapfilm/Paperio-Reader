#include "ChapterHtmlSlimParser.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SaxParser/SaxParser.h>
#include <Utf8.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cctype>

#include "../../Epub.h"
#include "../Page.h"
#include "../content/BlockSink.h"
#include "../content/ImageLayout.h"
#include "../content/LayoutSink.h"
#include "../content/TableLayout.h"
#include "../converters/ImageDecoderFactory.h"
#include "../converters/ImageToFramebufferDecoder.h"
#include "../htmlEntities.h"

const char* HEADER_TAGS[] = {"h1", "h2", "h3", "h4", "h5", "h6"};
constexpr int NUM_HEADER_TAGS = sizeof(HEADER_TAGS) / sizeof(HEADER_TAGS[0]);

// Size thresholds (bytes of XHTML) controlling indexing popup behavior.
// Each progress callback costs ~640ms of e-ink refresh, so we trade granularity off
// against indexing time based on expected duration.
//   < 15KB:  no popup at all - indexing finishes faster than the popup would draw
//   < 30KB:  popup only (one refresh up-front, no mid-parse updates)
//   < 80KB:  popup + one heartbeat at 50%
//   >= 80KB: popup + ticks at 25/50/75%
constexpr size_t MIN_SIZE_FOR_POPUP = 15 * 1024;
constexpr size_t SIZE_FOR_PROGRESS_HEARTBEAT = 30 * 1024;
constexpr size_t SIZE_FOR_PROGRESS_FINE = 80 * 1024;
constexpr size_t MIN_FREE_HEAP_FOR_INDEXING_POPUP = 32 * 1024;
constexpr size_t MIN_CONTIG_HEAP_FOR_INDEXING_POPUP = 12 * 1024;

constexpr size_t PARSE_BUFFER_SIZE = 1024;

// Hard cap on the number of anchor IDs recorded per chapter. Legitimate navigation
// anchors (TOC entries, footnotes, cross-references) rarely exceed a few hundred per
// chapter. A runaway count usually means a converter injected machine-generated IDs on
// every text fragment (e.g. Kobo KePub spans). The cap prevents unbounded heap growth
// on resource-constrained devices. TOC anchors bypass this cap.
constexpr size_t MAX_ANCHORS_PER_CHAPTER = 1024;

// Image extraction is now deferred to render time (ImageBlock::ensureExtracted).
// No heap guard needed at parse time — only a ZIP header read (~4 KB buffer on stack in
// getDimensionsFromZipEntry) happens during createSectionFile.

#ifndef EHP_TEXT_LAYOUT_SOFT_MIN_FREE_HEAP
#define EHP_TEXT_LAYOUT_SOFT_MIN_FREE_HEAP (18 * 1024)
#endif

#ifndef EHP_TEXT_LAYOUT_SOFT_MIN_MAX_ALLOC
#define EHP_TEXT_LAYOUT_SOFT_MIN_MAX_ALLOC (12 * 1024)
#endif

#ifndef EHP_TEXT_LAYOUT_HARD_MIN_FREE_HEAP
#define EHP_TEXT_LAYOUT_HARD_MIN_FREE_HEAP (9 * 1024)
#endif

#ifndef EHP_TEXT_LAYOUT_HARD_MIN_MAX_ALLOC
#define EHP_TEXT_LAYOUT_HARD_MIN_MAX_ALLOC (6 * 1024)
#endif

constexpr size_t MIN_FREE_HEAP_FOR_TEXT_LAYOUT = EHP_TEXT_LAYOUT_SOFT_MIN_FREE_HEAP;
constexpr size_t MIN_MAX_ALLOC_FOR_TEXT_LAYOUT = EHP_TEXT_LAYOUT_SOFT_MIN_MAX_ALLOC;
constexpr size_t MIN_FREE_HEAP_FOR_TEXT_LAYOUT_HARD = EHP_TEXT_LAYOUT_HARD_MIN_FREE_HEAP;
constexpr size_t MIN_MAX_ALLOC_FOR_TEXT_LAYOUT_HARD = EHP_TEXT_LAYOUT_HARD_MIN_MAX_ALLOC;

const char* BLOCK_TAGS[] = {"p", "li", "div", "br", "blockquote", "pre"};
constexpr int NUM_BLOCK_TAGS = sizeof(BLOCK_TAGS) / sizeof(BLOCK_TAGS[0]);

const char* BOLD_TAGS[] = {"b", "strong"};
constexpr int NUM_BOLD_TAGS = sizeof(BOLD_TAGS) / sizeof(BOLD_TAGS[0]);

const char* ITALIC_TAGS[] = {"i", "em"};
constexpr int NUM_ITALIC_TAGS = sizeof(ITALIC_TAGS) / sizeof(ITALIC_TAGS[0]);

const char* UNDERLINE_TAGS[] = {"u", "ins"};
constexpr int NUM_UNDERLINE_TAGS = sizeof(UNDERLINE_TAGS) / sizeof(UNDERLINE_TAGS[0]);

const char* STRIKETHROUGH_TAGS[] = {"s", "del", "strike"};
constexpr int NUM_STRIKETHROUGH_TAGS = sizeof(STRIKETHROUGH_TAGS) / sizeof(STRIKETHROUGH_TAGS[0]);

const char* IMAGE_TAGS[] = {"img", "image"};
constexpr int NUM_IMAGE_TAGS = sizeof(IMAGE_TAGS) / sizeof(IMAGE_TAGS[0]);

const char* SKIP_TAGS[] = {"head"};
constexpr int NUM_SKIP_TAGS = sizeof(SKIP_TAGS) / sizeof(SKIP_TAGS[0]);

bool isWhitespace(const char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }

bool hasAttributeToken(const char* value, const char* token) {
  if (!value || !token) return false;
  const size_t tokenLen = strlen(token);
  const char* cursor = value;
  while (*cursor != '\0') {
    while (*cursor != '\0' && isWhitespace(*cursor)) ++cursor;
    const char* start = cursor;
    while (*cursor != '\0' && !isWhitespace(*cursor)) ++cursor;
    if (static_cast<size_t>(cursor - start) == tokenLen && strncmp(start, token, tokenLen) == 0) return true;
  }
  return false;
}

// Returns true if the trailing UTF-8 codepoint in [buf, buf+len) is a dash that allows
// a line break opportunity after it. Inline-tag boundaries like "gone—<i>Umbriel</i>"
// would otherwise glue the dash to the following word via nextWordContinues, making the
// dash unbreakable; callers use this to skip setting that flag when the buffered text
// already ends at a natural break point.
//
// Soft hyphen (U+00AD) and non-breaking hyphen (U+2011) are intentionally excluded:
// soft hyphen is invisible (a hyphenation hint) and non-breaking hyphen forbids breaks
// by definition. Minus sign (U+2212) is excluded because it's mathematical, not a word
// separator.
bool bufferEndsWithBreakableDash(const char* buf, const int len) {
  if (len <= 0) return false;
  int start = len - 1;
  while (start > 0 && (static_cast<uint8_t>(buf[start]) & 0xC0) == 0x80) {
    --start;
  }
  const auto* ptr = reinterpret_cast<const unsigned char*>(buf + start);
  const uint32_t cp = utf8NextCodepoint(&ptr);
  switch (cp) {
    case '-':
    case 0x2010:  // HYPHEN
    case 0x2012:  // FIGURE DASH
    case 0x2013:  // EN DASH
    case 0x2014:  // EM DASH
    case 0x2015:  // HORIZONTAL BAR
    case 0x2E3A:  // TWO-EM DASH
    case 0x2E3B:  // THREE-EM DASH
      return true;
    default:
      return false;
  }
}

// given the start and end of a tag, check to see if it matches a known tag
bool matches(const char* tag_name, const char* possible_tags[], const int possible_tag_count) {
  for (int i = 0; i < possible_tag_count; i++) {
    if (strcmp(tag_name, possible_tags[i]) == 0) {
      return true;
    }
  }
  return false;
}

const char* getAttribute(const char** atts, const char* attrName) {
  if (!atts) return nullptr;
  for (int i = 0; atts[i]; i += 2) {
    if (strcmp(atts[i], attrName) == 0) return atts[i + 1];
  }
  return nullptr;
}

std::string ChapterHtmlSlimParser::abbreviateInlineFootnote(const char* text) const {
  if (!text || *text == '\0') return {};
  const int maxAdvance = static_cast<int>(viewportWidth) * 2;
  const int spaceAdvance = renderer.getSpaceWidth(fontId);
  int usedAdvance = 0;
  std::string result;
  const char* cursor = text;
  while (*cursor != '\0') {
    while (*cursor == ' ') ++cursor;
    if (*cursor == '\0') break;
    const char* wordStart = cursor;
    while (*cursor != '\0' && *cursor != ' ') ++cursor;
    const std::string previewWord(wordStart, static_cast<size_t>(cursor - wordStart));
    const int wordAdvance = renderer.getTextWidth(fontId, previewWord.c_str());
    const int separatorAdvance = result.empty() ? 0 : spaceAdvance;
    if (!result.empty() && usedAdvance + separatorAdvance + wordAdvance > maxAdvance) {
      result += "...";
      break;
    }
    if (!result.empty()) result += ' ';
    result += previewWord;
    usedAdvance += separatorAdvance + wordAdvance;
  }
  return result;
}

bool isInternalEpubLink(const char* href) {
  if (!href || href[0] == '\0') return false;
  if (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0) return false;
  if (strncmp(href, "mailto:", 7) == 0) return false;
  if (strncmp(href, "ftp://", 6) == 0) return false;
  if (strncmp(href, "tel:", 4) == 0) return false;
  if (strncmp(href, "javascript:", 11) == 0) return false;
  return true;
}

bool isHeaderOrBlock(const char* name) {
  return matches(name, HEADER_TAGS, NUM_HEADER_TAGS) || matches(name, BLOCK_TAGS, NUM_BLOCK_TAGS);
}

// Returns true if the HTML element is a purely inline, non-navigable wrapper.
// IDs on these elements are never meaningful navigation targets in epub content.
// Reading-system converters (Kobo KePub, Calibre, etc.) frequently inject thousands
// of such IDs for progress tracking or internal bookkeeping, and recording each one
// as a navigation anchor exhausts the heap on memory-constrained devices.
// Block-level, sectioning, and structural elements are always considered navigable.
bool isNonNavigableInlineElement(const char* name) { return strcmp(name, "span") == 0; }

bool isTableStructuralTag(const char* name) {
  return strcmp(name, "table") == 0 || strcmp(name, "tr") == 0 || strcmp(name, "td") == 0 || strcmp(name, "th") == 0;
}

std::string buildTextBlockPreview(const std::shared_ptr<TextBlock>& line, const size_t maxLen = 120) {
  if (!line) {
    return {};
  }

  std::string preview;
  const uint16_t wordCount = line->wordCount();
  for (uint16_t i = 0; i < wordCount; ++i) {
    if (i > 0) {
      preview.push_back(' ');
    }
    preview += line->wordText(i);
    if (preview.size() >= maxLen) {
      preview.resize(maxLen);
      preview += "...";
      break;
    }
  }
  return preview;
}

// Calibre sometimes injects empty <p style="margin:0; border:0; height:0">...</p>
// spacers inside running prose. Keep them as paragraph boundaries, but ignore
// their inner text payload (usually NBSP) to avoid no-break-space glue artifacts.
bool isZeroHeightSpacerParagraph(const char* name, const std::string& styleAttr) {
  if (strcmp(name, "p") != 0 || styleAttr.empty()) {
    return false;
  }

  std::string normalized;
  normalized.reserve(styleAttr.size());
  for (const char ch : styleAttr) {
    if (!isWhitespace(ch)) {
      normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
  }

  const bool hasZeroHeight = normalized.find("height:0") != std::string::npos;
  const bool hasZeroMargin = normalized.find("margin:0") != std::string::npos;
  const bool hasZeroBorder = normalized.find("border:0") != std::string::npos;
  return hasZeroHeight && hasZeroMargin && hasZeroBorder;
}

// Update effective bold/italic/underline based on block style and inline style stack
void ChapterHtmlSlimParser::updateEffectiveInlineStyle() {
  // Start with block-level styles
  effectiveBold = currentCssStyle.hasFontWeight() && currentCssStyle.fontWeight == CssFontWeight::Bold;
  effectiveItalic = currentCssStyle.hasFontStyle() && currentCssStyle.fontStyle == CssFontStyle::Italic;
  effectiveUnderline = currentCssStyle.hasTextDecoration() && (static_cast<uint8_t>(currentCssStyle.textDecoration) &
                                                               static_cast<uint8_t>(CssTextDecoration::Underline)) != 0;
  effectiveStrikethrough =
      currentCssStyle.hasTextDecoration() && (static_cast<uint8_t>(currentCssStyle.textDecoration) &
                                              static_cast<uint8_t>(CssTextDecoration::LineThrough)) != 0;
  effectiveSup = false;
  effectiveSub = false;
  effectiveSmallCaps = currentCssStyle.hasSmallCaps() && currentCssStyle.smallCaps;
  effectiveInlineMarginLeft = 0;
  // Inline font-size composes multiplicatively through the stack (em is relative to
  // the parent element); the block's own font-size lives in BlockStyle, so 100 here
  // means "the block size". Tracked in integer percent to match the per-word channel.
  int sizePct = 100;

  // Apply inline style stack in order
  for (const auto& entry : inlineStyleStack) {
    if (entry.hasBold) {
      effectiveBold = entry.bold;
    }
    if (entry.hasItalic) {
      effectiveItalic = entry.italic;
    }
    if (entry.hasUnderline) {
      effectiveUnderline = entry.underline;
    }
    if (entry.hasStrikethrough) {
      effectiveStrikethrough = entry.strikethrough;
    }
    if (entry.hasSup) {
      effectiveSup = entry.sup;
      if (entry.sup) effectiveSub = false;
    }
    if (entry.hasSub) {
      effectiveSub = entry.sub;
      if (entry.sub) effectiveSup = false;
    }
    if (entry.hasSmallCaps) {
      effectiveSmallCaps = entry.smallCaps;
    }
    if (entry.hasMarginLeft) {
      effectiveInlineMarginLeft = entry.marginLeftPx;
    }
    if (entry.hasFontSize) {
      sizePct = sizePct * entry.fontSizePct / 100;
    }
  }
  // Mirror FontSizeLadder::kResidualDeadZone at the word level: composed sizes within
  // 3% of 100 render as plain body text — imperceptible size-wise, and it keeps such
  // lines on the zero-cost uniform paths (no per-word size array, no scaled draws).
  if (sizePct >= 97 && sizePct <= 103) {
    sizePct = 100;
  }
  effectiveSizePct = static_cast<uint8_t>(
      std::min<int>(std::max<int>(sizePct, ParsedText::MIN_WORD_SIZE_PCT), ParsedText::MAX_WORD_SIZE_PCT));
}

void ChapterHtmlSlimParser::applyCssFontSizeToEntry(StyleStackEntry& entry, const CssStyle& cssStyle) {
  if (!cssStyle.hasFontSizeMultiplier()) return;
  const int pct = static_cast<int>(cssStyle.fontSizeMultiplier * 100.0f + 0.5f);
  entry.hasFontSize = true;
  entry.fontSizePct = static_cast<uint8_t>(
      std::min<int>(std::max<int>(pct, ParsedText::MIN_WORD_SIZE_PCT), ParsedText::MAX_WORD_SIZE_PCT));
}

void ChapterHtmlSlimParser::applySupSubDefaultSize(StyleStackEntry& entry) {
  if ((entry.hasSup && entry.sup) || (entry.hasSub && entry.sub)) {
    entry.hasFontSize = true;
    entry.fontSizePct = kSupSubDefaultSizePct;
  }
}

bool ChapterHtmlSlimParser::ensureHeapForTextLayout(const char* phase) {
  if (streamFailed) {
    return false;
  }

  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t maxAllocHeap = ESP.getMaxAllocHeap();
  if (freeHeap >= MIN_FREE_HEAP_FOR_TEXT_LAYOUT && maxAllocHeap >= MIN_MAX_ALLOC_FOR_TEXT_LAYOUT) {
    return true;
  }

  // Soft low-memory zone: keep parsing in degraded mode and only hard-abort when
  // both free and contiguous heap fall to critical levels.
  if (freeHeap >= MIN_FREE_HEAP_FOR_TEXT_LAYOUT_HARD && maxAllocHeap >= MIN_MAX_ALLOC_FOR_TEXT_LAYOUT_HARD) {
    lowMemoryImageFallback = true;
    LOG_DBG("EHP", "Low heap (%u free, %u max alloc) before %s; continuing in degraded mode", freeHeap, maxAllocHeap,
            phase);
    return true;
  }

  LOG_ERR("EHP", "Low heap (%u free, %u max alloc), aborting parse before %s", freeHeap, maxAllocHeap, phase);
  streamFailed = true;
  layoutFailed = true;
  saxParser_.stop();
  return false;
}

// flush the contents of partWordBuffer to currentTextBlock
bool ChapterHtmlSlimParser::flushPartWordBuffer() {
  if (streamFailed) {
    partWordBufferIndex = 0;
    nextWordContinues = false;
    return false;
  }

  // Determine font style from depth-based tracking and CSS effective style
  const bool isBold = boldUntilDepth < depth || effectiveBold;
  const bool isItalic = italicUntilDepth < depth || effectiveItalic;
  const bool isUnderline = underlineUntilDepth < depth || effectiveUnderline;
  const bool isStrikethrough = strikethroughUntilDepth < depth || effectiveStrikethrough;

  // Combine style flags using bitwise OR
  EpdFontFamily::Style fontStyle = EpdFontFamily::REGULAR;
  if (isBold) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::BOLD);
  }
  if (isItalic) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::ITALIC);
  }
  if (isUnderline) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::UNDERLINE);
  }
  if (isStrikethrough) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::STRIKETHROUGH);
  }
  if (effectiveSup) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::SUP);
  } else if (effectiveSub) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::SUB);
  }
  if (effectiveSmallCaps) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::SMALL_CAPS);
  }

  // flush the buffer — route to table cell text when inside a <td>/<th> (the buffered table the
  // producer serializes), otherwise emit the word to the producer. Layout (word measurement, the
  // >96-word mid-block split, float wrapping) is LayoutSink's job now.
  partWordBuffer[partWordBufferIndex] = '\0';
  if (currentTableCell) {
    currentTableCell->text->addWord(partWordBuffer, fontStyle, false, nextWordContinues, effectiveSizePct);
  } else {
    stage1AddWord(partWordBuffer, fontStyle, effectiveSizePct, nextWordContinues);
  }
  partWordBufferIndex = 0;
  nextWordContinues = false;
  return true;
}

// Emit the current page, keeping paragraphLutPerPage and completedPageCount in lockstep.
// Callers must ensure currentPage is non-null and carries content; the helper resets
// currentPage to a fresh Page and zeroes currentPageNextY so the caller can keep building.
void ChapterHtmlSlimParser::emitPage(uint32_t xhtmlByteOffset) {
  paragraphLutPerPage.push_back({xhtmlByteOffset, xpathParagraphIndex, xpathListItemIndex});
  // Step 6 unify: when the internal LayoutSink drives output it calls completePageFn itself, so the
  // fused emitPage must NOT also emit (double-emit). Only emit here when no internal sink exists —
  // i.e. an external ContentSink compile, where the fused path still produces the section pages.
  // The rest of this body keeps the fused layout's page/float scratch coherent either way.
  if (!layoutSink_) completePageFn(std::move(currentPage));
  completedPageCount++;
  currentPage.reset(new (std::nothrow) Page());
  currentPageNextY = 0;
  lastBlockMarginBottom = 0;
  deferredPageImage_.reset();  // the deferred yPos update is moot on a fresh page

  // A floated image never crosses a page boundary, so any active float ended on the
  // page we just emitted. Clear it and drop stale float zones from the block that
  // continues onto the new page, so its lines are not indented for a prior image.
  activeFloatTop_ = 0;
  activeFloatBottom_ = 0;
  if (currentTextBlock) {
    currentTextBlock->getBlockStyle().floatZoneCount = 0;
  }
}

void ChapterHtmlSlimParser::recordPageBreakLabel(const std::string& label) {
  if (label.empty()) {
    return;
  }

  // Record the printed page label for the current rendered section page.
  // Do not alter pagination; the reader keeps its own page breaks.
  pageBreakLabels.emplace_back(static_cast<uint16_t>(completedPageCount), label);
  // Stage-1: the label is position-anchored (like anchors, at block granularity); the
  // sink records it against its current block count and Stage-2 maps it to a page.
  if (auto* sink = effectiveSink()) sink->onPageBreakLabel(label);
}

void ChapterHtmlSlimParser::setExternalPageBreakAnchors(std::vector<std::pair<std::string, std::string>> anchors) {
  externalPageBreakAnchors.clear();
  topOfFilePageLabel.clear();
  topOfFilePageLabelEmitted = false;
  for (auto& [id, label] : anchors) {
    if (id.empty()) {
      // NCX pageTarget with no fragment (e.g. "OEBPS/c9_split_000.xhtml") — applies to the
      // first rendered page of this chapter. Keep only the first such entry if multiple.
      if (topOfFilePageLabel.empty()) {
        topOfFilePageLabel = std::move(label);
      }
    } else {
      externalPageBreakAnchors.emplace_back(std::move(id), std::move(label));
    }
  }
}

void ChapterHtmlSlimParser::attachPendingFloatImage(BlockStyle& bs) {
  if (!pendingInlineImage_.active) return;
  if (!currentPage) currentPage.reset(new (std::nothrow) Page());

  const int16_t imgH = pendingInlineImage_.height;
  const int16_t imgW = pendingInlineImage_.width;
  const bool imgIsRight = pendingInlineImage_.isRight;

  // A floated image is never split across a page boundary. If it would not fit in the
  // space left on this page, break first so it floats at the top of a fresh page —
  // its height is capped at one viewport by the float gate, so a fresh page always
  // has room. This avoids the fragile cross-page tile/continuation path entirely and
  // lets the whole image (with text wrapping beside it) live on a single page.
  if (imgH > static_cast<int16_t>(viewportHeight - currentPageNextY) && currentPage && !currentPage->elements.empty()) {
    emitPage(lastBodyChildByteOffset);  // resets currentPage + currentPageNextY=0, clears stale float state
  }

  const int16_t imgX = imgIsRight ? static_cast<int16_t>(viewportWidth - imgW) : 0;
  const int16_t top = static_cast<int16_t>(currentPageNextY);

  auto fullImageBlock =
      std::make_shared<ImageBlock>(pendingInlineImage_.cachedPath, imgW, imgH, pendingInlineImage_.alt, epub->getPath(),
                                   pendingInlineImage_.epubEntryPath);
  deferredPageImage_ = std::make_shared<PageImage>(fullImageBlock, imgX, top);
  currentPage->elements.push_back(deferredPageImage_);

  // Attach the float zone to the originating block (the caption/first paragraph).
  // makePages() re-anchors it to the first line and then propagates it to every
  // following block that overlaps the image, via the active-float state below.
  if (bs.floatZoneCount < BlockStyle::kMaxFloatZones) {
    auto& z = bs.floatZones[bs.floatZoneCount++];
    z.top = top;
    z.bottom = static_cast<int16_t>(top + imgH);
    z.width = static_cast<int16_t>(imgW + 4);
    z.isRight = imgIsRight;
  }
  // Provisional active-float extent; makePages() finalises top/bottom once the
  // originating block's first line (and thus the image top) is positioned.
  activeFloatTop_ = top;
  activeFloatBottom_ = static_cast<int16_t>(top + imgH);
  activeFloatWidth_ = static_cast<int16_t>(imgW + 4);
  activeFloatIsRight_ = imgIsRight;

  pendingInlineImage_.active = false;
  pendingInlineImage_.cachedPath.clear();
  pendingInlineImage_.epubEntryPath.clear();
  pendingInlineImage_.alt.clear();
}

// start a new text block if needed
// --- Stage-1 producer tap ---------------------------------------------------
// No-ops when stage1Sink_ is null, so the shipping fused path is byte-identical.
// Emits a materialized compiled::Block per text block; text is stored raw Unicode in
// LOGICAL order (RTL shaping/reordering is a Stage-2 concern). See
// docs/stage1-extraction-design.md.

// Map the layout font-style bitmask to the stable on-disk styleSpan layout.
static uint8_t stage1MapStyleSpan(const EpdFontFamily::Style style, const bool attachToPrevious) {
  uint8_t span = 0;
  if (style & EpdFontFamily::BOLD) span |= compiled::kSpanBold;
  if (style & EpdFontFamily::ITALIC) span |= compiled::kSpanItalic;
  if (style & EpdFontFamily::UNDERLINE) span |= compiled::kSpanUnderline;
  if (style & EpdFontFamily::STRIKETHROUGH) span |= compiled::kSpanStrikethrough;
  if (style & EpdFontFamily::SUP) span |= compiled::kSpanSuper;
  if (style & EpdFontFamily::SUB) span |= compiled::kSpanSub;
  if (style & EpdFontFamily::SMALL_CAPS) span |= compiled::kSpanSmallCaps;
  if (attachToPrevious) span |= compiled::kSpanAttachPrev;
  return span;
}

// Count Unicode codepoints (non-continuation bytes) for the reading-progress offset.
static uint32_t stage1CountCodepoints(const char* s) {
  uint32_t n = 0;
  for (const char* p = s; *p != '\0'; ++p) {
    if ((static_cast<uint8_t>(*p) & 0xC0) != 0x80) ++n;
  }
  return n;
}

// Join a block's words into a single string, honoring the attach-to-previous bit
// (no space before attached words) — used as a heading/chapter title.
static std::string stage1JoinWords(const compiled::Block& b) {
  std::string title;
  for (size_t i = 0; i < b.words.size(); ++i) {
    if (i != 0 && (b.words[i].styleSpan & compiled::kSpanAttachPrev) == 0) title.push_back(' ');
    title.append(&b.text[b.words[i].textOff]);
  }
  return title;
}

void ChapterHtmlSlimParser::stage1FlushBlock() {
  compiled::BlockSink* sink = effectiveSink();
  if (!sink || !stage1Block_) return;
  // Empty blocks are emitted too: they are the wrapper/spacer/<br> transcript. The fused
  // layout derives real spacing from them (empty-block margin merge, <br> gap injection,
  // pendingImageBlockStyle around images), so Stage-2 needs the same sequence to replay
  // those merges byte-identically. They carry no words, so they cost a few bytes each.
  stage1Block_->type = compiled::BlockType::Text;
  // Inside <pre>, the fused makePages suppresses the extra inter-paragraph spacing (gated on
  // preUntilDepth). Tag the block so Stage-2 does the same. This mirrors the layout-time gate:
  // the block is flushed as the NEXT block opens, and <pre>'s own last line is flushed after
  // </pre> resets preUntilDepth (so it correctly gets normal spacing, matching cpp:2733).
  if (preUntilDepth != INT_MAX) stage1Block_->flags |= compiled::kPreformatted;
  const uint8_t headingLevel = stage1BlockHeadingLevel_;
  const std::string title = headingLevel > 0 ? stage1JoinWords(*stage1Block_) : std::string();
  // Transmit the walk's current XPath counters so the sink's per-page LUT matches the fused one:
  // any page break the sink takes while laying out THIS block uses these values (the same the
  // fused emitPage would read, since the walk sets them before emitting the block).
  sink->onXPathAdvance(xpathParagraphIndex, xpathListItemIndex, lastBodyChildByteOffset);
  sink->onBlock(std::move(*stage1Block_), stage1BlockCssStyle_);
  stage1Block_.reset();
  stage1BlockHeadingLevel_ = 0;
  // A heading block is a chapter/heading boundary: report it against the block just emitted.
  if (headingLevel > 0) sink->onChapter(headingLevel, title);
}

// Emit a stashed anchor id. Call AFTER any prior block has been flushed, so the sink's
// block count equals the index of the block this anchor introduces (block granularity,
// charOffsetInBlock 0 — microreader's para_idx model; mid-block precision is a later refinement).
void ChapterHtmlSlimParser::stage1EmitPendingAnchor() {
  compiled::BlockSink* sink = effectiveSink();
  if (!sink || stage1PendingAnchor_.empty()) return;
  sink->onAnchor(stage1PendingAnchor_);
  stage1PendingAnchor_.clear();
}

void ChapterHtmlSlimParser::stage1EmitImageBlock(const std::string& entryPath, const int16_t width,
                                                 const int16_t height, const uint8_t floatSide,
                                                 const std::string& alt, const CssStyle& imgStyle) {
  compiled::BlockSink* sink = effectiveSink();
  if (!sink) return;
  stage1FlushBlock();         // emit any pending text block first (document order)
  stage1EmitPendingAnchor();  // an anchor introducing this image points at it
  compiled::Block b;
  b.type = compiled::BlockType::Image;
  b.charOffset = stage1CharOffset_;
  b.entryPath = entryPath;
  b.width = width;
  b.height = height;
  b.floatSide = floatSide;
  b.alt = alt;
  // Pass the image's resolved CSS (width/height) as the block style so Stage-2 reproduces the
  // display-dimension scaling; the intrinsic w/h ride on the block fields.
  sink->onXPathAdvance(xpathParagraphIndex, xpathListItemIndex, lastBodyChildByteOffset);
  sink->onBlock(std::move(b), imgStyle);
}

void ChapterHtmlSlimParser::stage1EmitHrBlock() {
  compiled::BlockSink* sink = effectiveSink();
  if (!sink) return;
  stage1FlushBlock();         // emit any pending text block first (document order)
  stage1EmitPendingAnchor();  // an anchor introducing the rule points at it
  compiled::Block b;
  b.type = compiled::BlockType::Hr;
  b.charOffset = stage1CharOffset_;
  sink->onXPathAdvance(xpathParagraphIndex, xpathListItemIndex, lastBodyChildByteOffset);
  sink->onBlock(std::move(b), CssStyle{});  // rule geometry is derived at layout time
}

void ChapterHtmlSlimParser::stage1EmitTableBlock(const BufferedTable& table) {
  compiled::BlockSink* sink = effectiveSink();
  if (!sink) return;
  stage1FlushBlock();         // emit any pending text block first (document order)
  stage1EmitPendingAnchor();  // an anchor introducing the table points at it
  compiled::Block b;
  b.type = compiled::BlockType::Table;
  b.charOffset = stage1CharOffset_;
  b.hasBorder = table.hasBorder;
  for (const BufferedTableRow& row : table.rows) {
    compiled::TableRow crow;
    crow.isHeaderRow = row.isHeaderRow;
    for (const BufferedTableCell& cell : row.cells) {
      compiled::TableCell ccell;
      ccell.isHeader = cell.isHeader;
      ccell.colSpan = cell.colSpan;
      if (cell.text) {
        for (size_t i = 0; i < cell.text->size(); ++i) {
          const std::string& word = cell.text->wordText(i);
          compiled::Word w;
          w.textOff = static_cast<uint32_t>(ccell.text.size());
          w.styleSpan = stage1MapStyleSpan(cell.text->wordStyle(i), cell.text->wordAttachesToPrevious(i));
          w.sizePct = cell.text->wordSizePct(i);
          w.bidiLevel = 0;
          ccell.words.push_back(w);
          ccell.text.append(word);
          ccell.text.push_back('\0');
          stage1CharOffset_ += stage1CountCodepoints(word.c_str());
        }
      }
      if (!cell.imageSrc.empty()) {
        // EPUB entry path; intrinsic dims for cell images are a follow-up (no corpus book
        // exercises them yet) — Stage-2 can still resolve via the image manifest.
        ccell.imageEntryPath = FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(contentBase + cell.imageSrc));
        ccell.imageAlt = cell.imageAlt;
      }
      crow.cells.push_back(std::move(ccell));
    }
    b.rows.push_back(std::move(crow));
  }
  sink->onXPathAdvance(xpathParagraphIndex, xpathListItemIndex, lastBodyChildByteOffset);
  sink->onBlock(std::move(b), CssStyle{});
}

void ChapterHtmlSlimParser::stage1OpenBlock(const CssStyle& style) {
  if (!effectiveSink()) return;
  stage1FlushBlock();         // hand off the previous block (even an empty wrapper — transcript)
  stage1EmitPendingAnchor();  // the anchor introduces the block we are about to open
  stage1Block_.reset(new (std::nothrow) compiled::Block());
  const uint8_t headingLevel = stage1PendingHeadingLevel_;
  const bool fromBr = stage1PendingFromBr_;
  stage1PendingHeadingLevel_ = 0;
  stage1PendingFromBr_ = false;
  if (!stage1Block_) return;  // OOM: skip Stage-1 for this block, layout is unaffected
  stage1Block_->charOffset = stage1CharOffset_;
  stage1BlockCssStyle_ = style;
  stage1BlockHeadingLevel_ = headingLevel;
  if (headingLevel > 0) stage1Block_->flags |= compiled::kStartsChapter;
  if (fromBr) stage1Block_->flags |= compiled::kFromBrElement;
  // A TOC-boundary anchor introducing this block forces a fresh page (fused emitPage above).
  if (stage1PendingPageBreak_) {
    stage1Block_->flags |= compiled::kPageBreakBefore;
    stage1PendingPageBreak_ = false;
  }
}

void ChapterHtmlSlimParser::stage1AddWord(const char* text, const EpdFontFamily::Style style, const uint8_t sizePct,
                                          const bool attachToPrevious) {
  if (!effectiveSink()) return;
  if (!stage1Block_) {
    // Words can reach the (still existing, empty) layout block without a new block-element
    // open — e.g. bare text directly after an image emit closed the accumulator. Reopen so
    // no text is silently dropped; the word-equivalence tests police this.
    stage1Block_.reset(new (std::nothrow) compiled::Block());
    if (!stage1Block_) return;
    stage1Block_->charOffset = stage1CharOffset_;
    stage1BlockCssStyle_ = currentCssStyle;
  }
  // A deferred float image attaches to the paragraph that receives the first following word.
  if (stage1InlineImagePending_ && stage1Block_->inlineImageEntryPath.empty()) {
    stage1Block_->inlineImageEntryPath = stage1InlineImagePath_;
    stage1Block_->inlineImageWidth = stage1InlineImageW_;
    stage1Block_->inlineImageHeight = stage1InlineImageH_;
    stage1Block_->inlineImageSide = stage1InlineImageSide_;
    stage1Block_->inlineImageAlt = stage1InlineImageAlt_;
    stage1InlineImagePending_ = false;
  }
  compiled::Word w;
  w.textOff = static_cast<uint32_t>(stage1Block_->text.size());
  w.styleSpan = stage1MapStyleSpan(style, attachToPrevious);
  w.sizePct = sizePct;
  w.bidiLevel = 0;  // LTR; RTL embedding levels are resolved in Stage-2 (see design "RTL / BiDi")
  stage1Block_->words.push_back(w);
  stage1Block_->text.append(text);
  stage1Block_->text.push_back('\0');  // words back-to-back, each NUL-terminated (format spec)
  stage1CharOffset_ += stage1CountCodepoints(text);
}

void ChapterHtmlSlimParser::startNewTextBlock(const BlockStyle& blockStyle) {
  nextWordContinues = false;  // New block = new paragraph, no continuation
  // Stage-1: remember whether the incoming block is a <br> separator; consumed by the
  // open below (either path) so the transcript block carries kFromBrElement.
  stage1PendingFromBr_ = blockStyle.fromBrElement;
  // Base style for the new block — normally the incoming blockStyle, but when falling
  // through from the empty-block merge path (see below) we use the merged style so that
  // accumulated parent-element margins are preserved for the inline-image paragraph.
  const BlockStyle* effectiveBase = &blockStyle;
  if (currentTextBlock) {
    // already have a text block running and it is empty - just reuse it
    if (currentTextBlock->isEmpty()) {
      // Merge with existing block style to accumulate CSS styling from parent block elements.
      // This handles cases like <div style="margin-bottom:2em"><h1>text</h1></div> where the
      // div's margin should be preserved, even though it has no direct text content.
      BlockStyle incoming = blockStyle;
      const bool brGapPending = currentTextBlock->getBlockStyle().fromBrElement;
      if (brGapPending) {
        // The empty block was created by a <br> section separator. Inject a full line of
        // blank space before the following paragraph so the scene/section break is visible.
        // This only fires when the <br> block stayed empty (i.e. no inline text was added).
        const int16_t lineHeight = static_cast<int16_t>(renderer.getLineHeight(fontId) * lineCompression + 0.5f);
        incoming.marginTop = static_cast<int16_t>(incoming.marginTop + lineHeight);
      }

      BlockStyle merged = currentTextBlock->getBlockStyle().getCombinedBlockStyle(incoming);
      // Preserve only whether the current empty block still represents <br> separators.
      // This lets consecutive <br> accumulate one line each without leaking the flag to real content blocks.
      merged.fromBrElement = blockStyle.fromBrElement;
      currentTextBlock->setBlockStyle(merged);

      if (!pendingAnchorId.empty()) {
        const bool tocBoundary = std::find(tocAnchors.begin(), tocAnchors.end(), pendingAnchorId) != tocAnchors.end();
        if (tocBoundary) {
          if (currentPage && !currentPage->elements.empty()) {
            emitPage(lastBodyChildByteOffset);
          }
        }
        if (effectiveSink()) {
          stage1PendingAnchor_ = pendingAnchorId;  // Stage-1: stash before the move
          // A TOC-boundary anchor forces a fresh page: transmit it as kPageBreakBefore on the
          // block this anchor introduces, so Stage-2 (LayoutSink) reproduces the page break.
          if (tocBoundary) stage1PendingPageBreak_ = true;
        }
        anchorData.push_back({std::move(pendingAnchorId), static_cast<uint16_t>(completedPageCount)});
        pendingAnchorId.clear();
      }
      wordsExtractedInBlock = 0;
      // If an inline image is waiting, fall through to place it now rather than
      // returning early — otherwise the image skips empty wrapper blocks and
      // attaches to the *second* paragraph instead of the first.
      if (!pendingInlineImage_.active) {
        // Stage-1: the layout reuses its empty block, but the transcript emits the wrapper
        // block (with its own style) and opens a fresh one for this element — Stage-2
        // replays the same empty-into-next margin merge the layout does here.
        stage1OpenBlock(currentCssStyle);
        return;
      }
      // Fall through: use the merged style as the base so parent-element margins
      // (accumulated into this empty block) are carried into the new paragraph.
      effectiveBase = &currentTextBlock->getBlockStyle();
    }

    if (!currentTextBlock->isEmpty()) makePages();
  }
  // If the pending anchor is a TOC chapter boundary, force a page break after the previous
  // block is flushed so the chapter starts on a fresh page.
  const bool tocBoundary =
      !pendingAnchorId.empty() && std::find(tocAnchors.begin(), tocAnchors.end(), pendingAnchorId) != tocAnchors.end();
  if (tocBoundary) {
    if (currentPage && !currentPage->elements.empty()) {
      emitPage(lastBodyChildByteOffset);
    }
  }
  // Record deferred anchor after previous block is flushed (and any TOC page break)
  if (!pendingAnchorId.empty()) {
    if (effectiveSink()) {
      stage1PendingAnchor_ = pendingAnchorId;  // Stage-1: stash before the move
      if (tocBoundary) stage1PendingPageBreak_ = true;  // -> kPageBreakBefore on the introduced block
    }
    anchorData.push_back({std::move(pendingAnchorId), static_cast<uint16_t>(completedPageCount)});
    pendingAnchorId.clear();
  }
  // Apply pending inline image: attach float zone and place image on current page.
  // The image's actual yPos will be fixed in addLineToPage once the baseline is known.
  BlockStyle blockStyleWithIndent = *effectiveBase;
  attachPendingFloatImage(blockStyleWithIndent);
  currentTextBlock.reset(new (std::nothrow) ParsedText(extraParagraphSpacing, hyphenationEnabled, blockStyleWithIndent,
                                                       bionicReadingEnabled));
  wordsExtractedInBlock = 0;
  // Stage-1: open a matching compiled block, carrying the block's pre-px CssStyle
  // (currentCssStyle was set to this block's resolved style just before the call).
  stage1OpenBlock(currentCssStyle);
}

void ChapterHtmlSlimParser::startElement(void* userData, const char* name, const char** atts) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);

  if (self->streamFailed) {
    return;
  }

  // Middle of skip
  if (self->skipUntilDepth < self->depth) {
    self->depth += 1;
    return;
  }

  // Track SVG nesting depth. Must be checked before the svgDepth>0 guard below so that
  // nested <svg> elements increment the counter rather than being swallowed as unknowns.
  if (strcmp(name, "svg") == 0) {
    self->svgDepth += 1;
    self->depth += 1;
    return;
  }

  // Inside SVG: only process <image> elements (raster images); skip everything else.
  // SVG child elements like <path>, <rect>, <circle>, <text> must not reach the layout
  // engine — they would accumulate path data and exhaust heap on large inline SVG.
  if (self->svgDepth > 0 && !matches(name, IMAGE_TAGS, NUM_IMAGE_TAGS)) {
    self->depth += 1;
    return;
  }

  // Extract class, style, id, and pagebreak metadata attributes
  std::string classAttr;
  std::string styleAttr;
  std::string idAttr;
  std::string ariaLabel;
  std::string titleAttr;
  bool isPageBreakMarker = false;
  if (atts != nullptr) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "class") == 0) {
        classAttr = atts[i + 1];
      } else if (strcmp(atts[i], "style") == 0) {
        styleAttr = atts[i + 1];
      } else if (strcmp(atts[i], "id") == 0) {
        idAttr = atts[i + 1];
      } else if (strcmp(atts[i], "aria-label") == 0) {
        ariaLabel = atts[i + 1];
      } else if (strcmp(atts[i], "title") == 0) {
        titleAttr = atts[i + 1];
      } else if (strcmp(atts[i], "role") == 0 && strcmp(atts[i + 1], "doc-pagebreak") == 0) {
        isPageBreakMarker = true;
      } else if (strcmp(atts[i], "epub:type") == 0 && strcmp(atts[i + 1], "pagebreak") == 0) {
        isPageBreakMarker = true;
      }
    }
  }

  // Emit any "top-of-file" printed-page label as soon as we see real markup. NCX entries
  // without a fragment refer to the start of this XHTML; record now so the label lands on
  // page 0 (completedPageCount is still 0 until the first emitPage()).
  if (!self->topOfFilePageLabelEmitted && !self->topOfFilePageLabel.empty()) {
    self->recordPageBreakLabel(self->topOfFilePageLabel);
    self->topOfFilePageLabelEmitted = true;
  }

  // Match id against NCX-supplied pagebreak anchors (printed page list). If matched,
  // treat this element as if it carried an inline doc-pagebreak marker.
  std::string externalLabel;
  if (!isPageBreakMarker && !idAttr.empty() && !self->externalPageBreakAnchors.empty()) {
    for (const auto& [extId, extLabel] : self->externalPageBreakAnchors) {
      if (extId == idAttr) {
        externalLabel = extLabel;
        isPageBreakMarker = true;
        break;
      }
    }
  }

  if (isPageBreakMarker) {
    std::string label = !ariaLabel.empty() ? ariaLabel : titleAttr;
    if (label.empty()) {
      label = std::move(externalLabel);
    }
    self->recordPageBreakLabel(label);
    if (!idAttr.empty()) {
      self->anchorData.emplace_back(idAttr, static_cast<uint16_t>(self->completedPageCount));
      self->pendingAnchorId = idAttr;
    }
  }

  // Defer generic anchor recording until startNewTextBlock, after the previous block
  // is flushed to pages via makePages(). Skip pagebreak anchors since they were already recorded.
  //
  // Skip IDs on non-navigable inline elements (e.g. <span>): these are never link targets
  // in epub content, but reading-system converters can inject tens of thousands of them per
  // chapter, exhausting the heap. The MAX_ANCHORS_PER_CHAPTER cap is a fallback against
  // unknown future ID-injection patterns on other elements. TOC anchors bypass both the
  // span filter and the cap, since they drive page breaks and core navigation.
  if (!isPageBreakMarker && !idAttr.empty()) {
    const bool isTocAnchor =
        std::find(self->tocAnchors.begin(), self->tocAnchors.end(), idAttr) != self->tocAnchors.end();
    if (isTocAnchor || (!isNonNavigableInlineElement(name) && self->anchorData.size() < MAX_ANCHORS_PER_CHAPTER)) {
      self->pendingAnchorId = idAttr;
    }
  }

  auto centeredBlockStyle = BlockStyle();
  centeredBlockStyle.textAlignDefined = true;
  centeredBlockStyle.alignment = CssTextAlign::Center;

  // Compute CSS style for this element early so display:none can short-circuit
  // before tag-specific branches emit any content or metadata.
  CssStyle cssStyle;
  if (self->cssParser) {
    {
      // ID-bearing elements are uncommon; only include idAttr in the cache key when
      // present, so the common case (no id) stays as the minimal "tag|class" key.
      std::string cacheKey(name);
      cacheKey += '|';
      cacheKey += classAttr;
      if (!idAttr.empty()) {
        cacheKey += '|';
        cacheKey += idAttr;
      }
      auto it = self->cssStyleCache_.find(cacheKey);
      if (it != self->cssStyleCache_.end()) {
        cssStyle = it->second;
      } else {
        CssStyle resolved = self->cssParser->resolveStyle(name, classAttr, idAttr);
        if (resolved.defined.anySet())
          cssStyle = self->cssStyleCache_.emplace(cacheKey, resolved).first->second;
        else
          cssStyle = resolved;  // transient fallback: skip cache so future calls can re-resolve
      }
    }
    if (!styleAttr.empty()) {
      auto it = self->inlineStyleCache_.find(styleAttr);
      if (it == self->inlineStyleCache_.end())
        it = self->inlineStyleCache_.emplace(styleAttr, CssParser::parseInlineStyle(styleAttr)).first;
      cssStyle.applyOver(it->second);
    }
  }

  // Skip elements with display:none before all fast paths (tables, links, etc.).
  if (cssStyle.hasDisplay() && cssStyle.display == CssDisplay::None) {
    self->skipUntilDepth = self->depth;
    self->depth += 1;
    return;
  }

  // Buffered table rendering: accumulate cells in memory, emit as PageTableFragment on </table>.
  if (strcmp(name, "table") == 0) {
    if (self->currentTable) {
      // Nested table — mark unsupported and track depth
      self->currentTable->depth += 1;
      self->currentTable->unsupported = true;
      self->depth += 1;
      return;
    }
    // Flush any pending text before starting the table
    if (self->partWordBufferIndex > 0) {
      if (!self->flushPartWordBuffer()) return;
    }
    if (self->currentTextBlock && !self->currentTextBlock->isEmpty()) {
      self->makePages();
    }
    self->currentTable = std::unique_ptr<BufferedTable>(new BufferedTable());
    self->currentTable->depth = 1;
    if (atts != nullptr) {
      for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "border") == 0 && strcmp(atts[i + 1], "0") == 0) {
          self->currentTable->hasBorder = false;
        }
      }
    }
    self->depth += 1;
    return;
  }

  if (self->currentTable && self->currentTable->depth == 1 && strcmp(name, "tr") == 0) {
    self->currentTable->rows.emplace_back();
    if (self->currentTable->rows.size() > MAX_TABLE_ROWS) {
      self->currentTable->unsupported = true;
    }
    self->depth += 1;
    return;
  }

  if (self->currentTable && self->currentTable->depth == 1 && (strcmp(name, "td") == 0 || strcmp(name, "th") == 0)) {
    if (self->partWordBufferIndex > 0) {
      if (!self->flushPartWordBuffer()) return;
    }
    if (self->currentTable->rows.empty()) {
      self->currentTable->rows.emplace_back();
    }
    BufferedTableRow& row = self->currentTable->rows.back();

    // Parse colspan attribute (inspired by uxjulia/CrossInk; rewritten for our codebase).
    // Any rowspan != 1 is unsupported; we ignore it and let the fallback handle those tables.
    uint8_t colSpan = 1;
    if (atts != nullptr) {
      for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "colspan") == 0) {
          char* end;
          const long v = std::strtol(atts[i + 1], &end, 10);
          if (end != atts[i + 1] && v >= 1 && v <= MAX_TABLE_COLS) {
            colSpan = static_cast<uint8_t>(v);
          }
        } else if (strcmp(atts[i], "rowspan") == 0) {
          char* end;
          const long v = std::strtol(atts[i + 1], &end, 10);
          if (end != atts[i + 1] && v != 1) {
            self->currentTable->unsupported = true;
          }
        }
      }
    }

    const bool isHeader = (strcmp(name, "th") == 0);
    row.cells.emplace_back();
    row.cells.back().isHeader = isHeader;
    row.cells.back().colSpan = colSpan;
    row.cells.back().text =
        std::unique_ptr<ParsedText>(new ParsedText(false, false));  // no paragraph spacing, no hyphenation in cells
    row.effectiveCols = static_cast<uint8_t>(row.effectiveCols + colSpan);
    if (row.effectiveCols > self->currentTable->maxCols) {
      self->currentTable->maxCols = row.effectiveCols;
    }
    if (row.cells.size() > MAX_TABLE_COLS || row.effectiveCols > MAX_TABLE_COLS) {
      self->currentTable->unsupported = true;
    }
    self->currentTableCell = &row.cells.back();
    self->depth += 1;
    return;
  }

  if (matches(name, IMAGE_TAGS, NUM_IMAGE_TAGS)) {
    std::string src;
    std::string alt;
    if (atts != nullptr) {
      for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "src") == 0 || strcmp(atts[i], "href") == 0 || strcmp(atts[i], "xlink:href") == 0) {
          if (src.empty()) {
            src = atts[i + 1];
            // Strip fragment anchors (e.g. "cover.jpg#xywh=0,0,100,100")
            auto hash = src.find('#');
            if (hash != std::string::npos) src.erase(hash);
          }
        } else if (strcmp(atts[i], "alt") == 0) {
          alt = atts[i + 1];
        }
      }

      // Image inside a table cell: attach it to the cell so the layout can place it inside the
      // grid (first image per cell wins). The fragment layout sizes the cell to fit it; the
      // paragraph fallback re-emits it as a block image below the table.
      if (self->currentTableCell && self->currentTable && !src.empty() && self->imageRendering != 2) {
        if (self->currentTableCell->imageSrc.empty()) {
          self->currentTableCell->imageSrc = src;
          self->currentTableCell->imageAlt = alt;
        }
        self->depth += 1;
        return;
      }

      // imageRendering: 0=display, 1=placeholder (alt text only), 2=suppress entirely
      if (self->imageRendering == 2) {
        // Suppressing an image should not leak accumulated wrapper block spacing
        // (e.g. figure/h1 margins) into the next text paragraph.
        if (self->currentTextBlock && self->currentTextBlock->isEmpty()) {
          BlockStyle resetStyle;
          resetStyle.textAlignDefined = true;
          const auto align = (self->paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None))
                                 ? CssTextAlign::Justify
                                 : static_cast<CssTextAlign>(self->paragraphAlignment);
          resetStyle.alignment = align;
          self->currentTextBlock->setBlockStyle(resetStyle);
          LOG_DBG("EHP", "Image suppressed: pending empty block style reset");
        }
        self->skipUntilDepth = self->depth;
        self->depth += 1;
        return;
      }

      // Skip image if CSS display:none
      if (self->cssParser) {
        std::string imgCacheKey("img|");
        imgCacheKey += classAttr;
        auto imgIt = self->cssStyleCache_.find(imgCacheKey);
        if (imgIt == self->cssStyleCache_.end())
          imgIt = self->cssStyleCache_.emplace(imgCacheKey, self->cssParser->resolveStyle("img", classAttr)).first;
        CssStyle imgDisplayStyle = imgIt->second;
        if (!styleAttr.empty()) {
          auto it = self->inlineStyleCache_.find(styleAttr);
          if (it == self->inlineStyleCache_.end())
            it = self->inlineStyleCache_.emplace(styleAttr, CssParser::parseInlineStyle(styleAttr)).first;
          imgDisplayStyle.applyOver(it->second);
        }
        if (imgDisplayStyle.hasDisplay() && imgDisplayStyle.display == CssDisplay::None) {
          // CSS-hidden images should behave like suppressed images for spacing.
          if (self->currentTextBlock && self->currentTextBlock->isEmpty()) {
            BlockStyle resetStyle;
            resetStyle.textAlignDefined = true;
            const auto align = (self->paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None))
                                   ? CssTextAlign::Justify
                                   : static_cast<CssTextAlign>(self->paragraphAlignment);
            resetStyle.alignment = align;
            self->currentTextBlock->setBlockStyle(resetStyle);
            LOG_DBG("EHP", "Image hidden via CSS display:none: pending empty block style reset");
          }
          self->skipUntilDepth = self->depth;
          self->depth += 1;
          return;
        }
      }

      const auto handleImageFallback = [&]() {
        // Fallback to alt text if image processing fails.
        if (!alt.empty()) {
          alt = "[Image: " + alt + "]";
          self->startNewTextBlock(centeredBlockStyle);
          self->italicUntilDepth = std::min(self->italicUntilDepth, self->depth);
          self->depth += 1;
          self->characterData(userData, alt.c_str(), alt.length());
          // Skip any child content (skip until parent as we pre-advanced depth above)
          self->skipUntilDepth = self->depth - 1;
          return;
        }

        // No alt text, skip.
        self->skipUntilDepth = self->depth;
        self->depth += 1;
      };

      if (!src.empty() && self->imageRendering != 1) {
        LOG_DBG("EHP", "Found image: src=%s", src.c_str());

        if (self->lowMemoryImageFallback) {
          handleImageFallback();
          return;
        }

        {
          // Resolve the image path relative to the HTML file
          std::string resolvedPath = FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(self->contentBase + src));

          if (ImageDecoderFactory::isFormatSupported(resolvedPath)) {
            // Determine SD cache path (image will be extracted here lazily at first render).
            std::string ext;
            size_t extPos = resolvedPath.rfind('.');
            if (extPos != std::string::npos) ext = resolvedPath.substr(extPos);
            std::string cachedImagePath = self->imageBasePath + std::to_string(self->imageCounter++) + ext;

            // Get dimensions from the pre-built manifest (fast, heap-safe) or fall back to
            // reading the ZIP entry header directly (safe outside a streaming inflate context).
            ImageDimensions dims = {0, 0};
            bool dimsOk = false;
            if (self->imageManifest) {
              // Resolve + cache on a miss: each image's header is read at most once ever.
              const ImageManifestEntry* entry =
                  self->imageManifest->ensureResolved(self->epub->getPath(), resolvedPath);
              if (entry) {
                dims.width = entry->width;
                dims.height = entry->height;
                dimsOk = true;
              }
            }
            if (!dimsOk) {
              dimsOk = ImageDecoderFactory::getDimensionsFromZipEntry(self->epub->getPath(), resolvedPath, dims);
            }
            if (dimsOk) {
              LOG_DBG("EHP", "Image dimensions: %dx%d", dims.width, dims.height);
              {
                int displayWidth = 0;
                int displayHeight = 0;
                const float emSize = static_cast<float>(self->renderer.getFontAscenderSize(self->fontId));
                std::string imgCacheKey("img|");
                imgCacheKey += classAttr;
                auto imgStyleIt = self->cssParser ? self->cssStyleCache_.find(imgCacheKey) : self->cssStyleCache_.end();
                if (self->cssParser && imgStyleIt == self->cssStyleCache_.end())
                  imgStyleIt =
                      self->cssStyleCache_.emplace(imgCacheKey, self->cssParser->resolveStyle("img", classAttr)).first;
                CssStyle imgStyle = self->cssParser ? imgStyleIt->second : CssStyle{};
                // Merge inline style (e.g. style="height: 2em") so it overrides stylesheet rules
                if (!styleAttr.empty()) {
                  auto it = self->inlineStyleCache_.find(styleAttr);
                  if (it == self->inlineStyleCache_.end())
                    it = self->inlineStyleCache_.emplace(styleAttr, CssParser::parseInlineStyle(styleAttr)).first;
                  imgStyle.applyOver(it->second);
                }
                int containerWidth = self->viewportWidth;
                if (self->currentTextBlock) {
                  const int inset = self->currentTextBlock->getBlockStyle().totalHorizontalInset();
                  if (inset > 0 && inset < self->viewportWidth) {
                    containerWidth = self->viewportWidth - inset;
                  }
                }

                // Display-dimension math lives in the shared helper so LayoutSink reproduces it
                // byte-for-byte (docs/parser-stage1-step5-design.md). Keep both in lockstep.
                {
                  const compiled::ImageDisplaySize ds = compiled::computeImageDisplaySize(
                      dims.width, dims.height, imgStyle, self->viewportWidth, self->viewportHeight, containerWidth,
                      emSize);
                  displayWidth = ds.width;
                  displayHeight = ds.height;
                  LOG_DBG("EHP", "Display size: %dx%d", displayWidth, displayHeight);
                }

                // Inline image path: if inside a CSS float context and the image leaves a
                // usable text column, defer placement beside the following paragraph rather
                // than emitting it as a centered block.
                // Concept inspired by CidVonHighwind/microreader and KOReader/CREngine research.
                //
                // Width: the float must leave at least half the column for text, otherwise the
                //   wrapped lines are too narrow to read — fall back to a centered block.
                // Height: a real figright/figleft illustration (e.g. a half-page Gutenberg plate)
                //   is much taller than a drop-cap or decorator, so allow up to a full viewport.
                //   attachPendingFloatImage() splits anything taller than the remaining page into
                //   a continuation tile on the next page; capping at viewportHeight keeps that
                //   single-continuation split correct (tileB never exceeds one page).
                // The float-vs-block decision is settings-dependent (uses the display dims), so the
                // walk still makes it here; it determines the block TYPE the producer emits. The
                // actual float placement lives in LayoutSink::attachFloatImage.
                const bool isInlineCandidate = self->floatDepth_ > 0 && displayWidth <= self->viewportWidth / 2 &&
                                               displayHeight <= self->viewportHeight;
                if (isInlineCandidate) {
                  // Stage-1: defer the float image (INTRINSIC dims — Stage-2 rescales) to attach to
                  // the paragraph that gets the first following word.
                  const bool isRight = (self->floatDepth_ > 0) && self->floatOpenSides_[self->floatDepth_ - 1];
                  self->stage1InlineImagePath_ = resolvedPath;
                  self->stage1InlineImageAlt_ = alt;
                  self->stage1InlineImageW_ = static_cast<int16_t>(dims.width);
                  self->stage1InlineImageH_ = static_cast<int16_t>(dims.height);
                  self->stage1InlineImageSide_ = isRight ? 2 : 1;
                  self->stage1InlineImagePending_ = true;
                  LOG_DBG("EHP", "Inline image deferred: w=%d h=%d", displayWidth, displayHeight);
                  // Don't flush the current text block — let it continue into the next paragraph.
                  self->depth += 1;
                  return;
                }

                // Block image path — flush any pending text so the image follows it in document
                // order, then emit the image as a standalone block with its INTRINSIC dims + CSS.
                // LayoutSink::placeBlockImage does the fit-to-viewport scaling, spacing, page-break
                // and PageImage placement (floatSide 0 = a centered block image, not a float).
                if (self->partWordBufferIndex > 0) {
                  if (!self->flushPartWordBuffer()) return;
                }
                if (self->currentTextBlock && !self->currentTextBlock->isEmpty()) {
                  const BlockStyle parentBlockStyle = self->currentTextBlock->getBlockStyle();
                  self->startNewTextBlock(parentBlockStyle);
                }
                self->stage1EmitImageBlock(resolvedPath, static_cast<int16_t>(dims.width),
                                           static_cast<int16_t>(dims.height), 0, alt, imgStyle);
                self->depth += 1;
                return;
              }  // dims-resolved block
            } else {
              LOG_ERR("EHP", "Failed to read image dimensions from ZIP: %s", resolvedPath.c_str());
            }
          }  // isFormatSupported
        }
      }

      handleImageFallback();
      return;
    }
  }

  // Track body element depth for paragraph index counting
  if (strcmp(name, "body") == 0 && self->xpathBodyDepth < 0) {
    self->xpathBodyDepth = self->depth;
  }

  // Count <p> sibling indices at body-child level. Must happen BEFORE the display:none
  // check so that hidden <p> elements are still counted, matching ChapterXPathIndexer's
  // counting (pure XML, no CSS). This ensures paragraph indices in the section cache LUT
  // align with KOReader's crengine XPath indices.
  // At the same time, record the byte offset of every direct-body-child element start:
  // the forward mapper's partial-parse heuristic requires the seek hint to land on a
  // body-child boundary, otherwise partialBaseDepth can misidentify wrapped paragraphs.
  if (self->xpathBodyDepth >= 0 && self->depth == self->xpathBodyDepth + 1) {
    self->lastBodyChildByteOffset = self->saxParser_.byteOffset();
    if (strcmp(name, "p") == 0) {
      self->xpathParagraphIndex++;
    }
  }

  // <li> can appear nested inside <ul>/<ol> at any depth, so count it globally —
  // not at body-child level. The running count must match what the runtime reverse
  // mapper sees so getPageForListItemIndex can snap a KOReader li XPath to a page.
  if (self->xpathBodyDepth >= 0 && strcmp(name, "li") == 0) {
    self->xpathListItemIndex++;
  }

  if (matches(name, SKIP_TAGS, NUM_SKIP_TAGS)) {
    // start skip
    self->skipUntilDepth = self->depth;
    self->depth += 1;
    return;
  }

  // removed skipping of doc-pagebreak and epub:type="pagebreak"
  // as publishers sometimes wrap actual content in these tags
  /*
  if (atts != nullptr) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "role") == 0 && strcmp(atts[i + 1], "doc-pagebreak") == 0 ||
          strcmp(atts[i], "epub:type") == 0 && strcmp(atts[i + 1], "pagebreak") == 0) {
        self->skipUntilDepth = self->depth;
        self->depth += 1;
        return;
      }
    }
  }
  */

  // Detect internal <a href="..."> links (footnotes, cross-references)
  // Note: <aside epub:type="footnote"> elements are rendered as normal content
  // without special handling. Links pointing to them are collected as footnotes.
  if (strcmp(name, "a") == 0) {
    const char* href = getAttribute(atts, "href");

    bool isInternalLink = isInternalEpubLink(href);

    // Special case: javascript:void(0) links with data attributes
    // Example: <a href="javascript:void(0)"
    // data-xyz="{&quot;name&quot;:&quot;OPS/ch2.xhtml&quot;,&quot;frag&quot;:&quot;id46&quot;}">
    if (href && strncmp(href, "javascript:", 11) == 0) {
      isInternalLink = false;
      // TODO: Parse data-* attributes to extract actual href
    }

    if (isInternalLink) {
      // Flush buffer before style change
      if (self->partWordBufferIndex > 0) {
        const bool endsAtDashBreak = bufferEndsWithBreakableDash(self->partWordBuffer, self->partWordBufferIndex);
        if (!self->flushPartWordBuffer()) return;
        if (!endsAtDashBreak) {
          self->nextWordContinues = true;
        }
      }
      self->insideFootnoteLink = true;
      self->footnoteLinkDepth = self->depth;
      strncpy(self->currentFootnote.href, href, sizeof(self->currentFootnote.href) - 1);
      self->currentFootnote.href[sizeof(self->currentFootnote.href) - 1] = '\0';
      self->currentFootnote.number[0] = '\0';
      self->currentFootnoteLinkTextLen = 0;

      // Apply underline style to visually indicate the link
      self->underlineUntilDepth = std::min(self->underlineUntilDepth, self->depth);
      StyleStackEntry entry;
      entry.depth = self->depth;
      entry.hasUnderline = true;
      entry.underline = true;
      self->inlineStyleStack.push_back(entry);
      self->updateEffectiveInlineStyle();

      // Skip CSS resolution — we already handled styling for this <a> tag
      self->depth += 1;
      return;
    }
  }

  // Track CSS float depth — used to detect inline images beside paragraph text.
  // Fixed-size array, cap at kMaxFloatDepth — deeper nesting is pathological.
  if (cssStyle.hasCssFloat() && cssStyle.cssFloat != CssFloat::None &&
      self->floatDepth_ < ChapterHtmlSlimParser::kMaxFloatDepth) {
    self->floatOpenDepths_[self->floatDepth_] = self->depth;
    self->floatOpenSides_[self->floatDepth_] = (cssStyle.cssFloat == CssFloat::Right);
    self->floatDepth_++;
  }

  if (strcmp(name, "ul") == 0 || strcmp(name, "ol") == 0) {
    int startCounter = 0;
    if (name[0] == 'o') {
      const char* startAttr = getAttribute(atts, "start");
      if (startAttr) {
        int v = atoi(startAttr);
        if (v > 0) startCounter = v - 1;  // counter is pre-incremented on each <li>
      }
    }
    self->listStack.push_back({self->depth, name[0] == 'o', startCounter, cssStyle.listStyleNone});
  }

  const float emSize = static_cast<float>(self->renderer.getFontAscenderSize(self->fontId));
  const auto userAlignmentBlockStyle = BlockStyle::fromCssStyle(
      cssStyle, emSize, static_cast<CssTextAlign>(self->paragraphAlignment), self->viewportWidth);

  // Block/header boundaries must flush any buffered trailing word first.
  // Otherwise tags like ..."item?"<p ...> can carry the final word into the next paragraph.
  if (self->partWordBufferIndex > 0 && ((matches(name, HEADER_TAGS, NUM_HEADER_TAGS)) ||
                                        (matches(name, BLOCK_TAGS, NUM_BLOCK_TAGS) && strcmp(name, "br") != 0))) {
    if (!self->flushPartWordBuffer()) return;
  }

  // CSS page-break-before: always — emit the current page before this block starts.
  if (cssStyle.pageBreakBefore &&
      (matches(name, HEADER_TAGS, NUM_HEADER_TAGS) || matches(name, BLOCK_TAGS, NUM_BLOCK_TAGS)) && self->currentPage &&
      !self->currentPage->elements.empty()) {
    // Stage-1: the fused break fires at element-open (possibly a wrapper div whose text lands in a
    // child block). Flag the NEXT opened block with kPageBreakBefore — reusing the pending-break
    // path the TOC-boundary case already uses — so the sink breaks before the same content.
    // Settings-independent CSS property (real-book: Project Gutenberg's `h2 { page-break-before }`).
    if (self->effectiveSink()) self->stage1PendingPageBreak_ = true;
    self->emitPage(self->lastBodyChildByteOffset);
  }

  if (matches(name, HEADER_TAGS, NUM_HEADER_TAGS)) {
    self->currentCssStyle = cssStyle;
    auto headerBlockStyle = BlockStyle::fromCssStyle(cssStyle, emSize, CssTextAlign::Center, self->viewportWidth);
    headerBlockStyle.textAlignDefined = true;
    if (self->embeddedStyle && cssStyle.hasTextAlign() &&
        self->paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None)) {
      headerBlockStyle.alignment = cssStyle.textAlign;
    }
    // Apply default heading sizing when no explicit CSS font-size is set.
    // Concept inspired by CidVonHighwind/microreader. h1-h3 get default multipliers;
    // h4-h6 stay at 1.0. The multiplier (default or CSS) is snapped to the size ladder
    // by resolveBlockFont() once the block is complete, so headings render with a real
    // taller font when one exists and only the residual is glyph-scaled.
    if (!cssStyle.hasFontSizeMultiplier()) {
      const int level = name[1] - '0';  // 'h1'->1, 'h2'->2, …
      if (level >= 1 && level <= 3) {
        headerBlockStyle.fontSizeMultiplier = kHeadingMultiplier[level - 1];
        // Stage-1: fold the settings-INDEPENDENT default multiplier into the captured
        // CssStyle so both sinks (ContentSink -> content.bin, LayoutSink) reproduce the
        // heading's effective size without re-deriving it from the tag. Alignment is NOT
        // folded — it depends on paragraphAlignment (a user setting) and stays sink-side.
        self->currentCssStyle.fontSizeMultiplier = kHeadingMultiplier[level - 1];
        self->currentCssStyle.defined.fontSizeMultiplier = 1;
      }
    }
    // Stage-1: tag the block about to open as a chapter/heading of this level.
    self->stage1PendingHeadingLevel_ = static_cast<uint8_t>(name[1] - '0');  // 'h1'->1 … 'h6'->6
    self->startNewTextBlock(headerBlockStyle);
    self->boldUntilDepth = std::min(self->boldUntilDepth, self->depth);
    self->updateEffectiveInlineStyle();
  } else if (matches(name, BLOCK_TAGS, NUM_BLOCK_TAGS)) {
    if (isZeroHeightSpacerParagraph(name, styleAttr)) {
      // Preserve paragraph break semantics for this <p>, but skip its inner text payload.
      self->currentCssStyle = cssStyle;
      auto blockStyle = userAlignmentBlockStyle;
      if (self->embeddedStyle && cssStyle.hasTextAlign() &&
          self->paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None)) {
        blockStyle.alignment = cssStyle.textAlign;
        blockStyle.textAlignDefined = true;
      }
      self->startNewTextBlock(blockStyle);
      self->updateEffectiveInlineStyle();

      self->skipTextUntilDepth = self->depth;
      self->depth += 1;
      return;
    }

    if (strcmp(name, "br") == 0) {
      if (self->partWordBufferIndex > 0) {
        // flush word preceding <br/> to currentTextBlock before calling startNewTextBlock
        if (!self->flushPartWordBuffer()) return;
      }
      // Tag the new block so startNewTextBlock can inject a full line-height gap if
      // the block remains empty (i.e. <br> is a section separator between paragraphs).
      // If the block gets text added before the next block opens it becomes non-empty,
      // goes through makePages() normally, and the flag has no effect (inline <br> case).
      // Build a neutral <br> style that keeps inline alignment/indent context but avoids
      // carrying cumulative margins from previous empty blocks (which can force spurious page breaks).
      const BlockStyle& currentStyle = self->currentTextBlock->getBlockStyle();
      BlockStyle brStyle;
      brStyle.alignment = currentStyle.alignment;
      brStyle.textAlignDefined = currentStyle.textAlignDefined;
      // text-indent is not inherited across <br>: it applies to the first line of a block only.
      // Span-based indents (poem stanza pattern) are applied directly to each block at span-open time.
      brStyle.fromBrElement = true;
      self->startNewTextBlock(brStyle);
      // Stage-1: startNewTextBlock captured currentCssStyle (the enclosing <p>'s style, which may
      // carry the paragraph's text-indent). The neutral brStyle above drops it, so drop it from
      // the transmitted style too — otherwise the sink indents every <br> continuation line. A
      // later span-margin (poem stanza) re-stamps textIndent on this block, which is correct.
      if (self->effectiveSink()) {
        self->stage1BlockCssStyle_.textIndent = CssLength{};
        self->stage1BlockCssStyle_.defined.textIndent = 0;
      }
    } else {
      self->currentCssStyle = cssStyle;
      auto blockStyle = userAlignmentBlockStyle;
      if (self->embeddedStyle && cssStyle.hasTextAlign() &&
          self->paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None)) {
        blockStyle.alignment = cssStyle.textAlign;
        blockStyle.textAlignDefined = true;
      }
      // For <li> with no CSS margin, apply depth-based indent so nested lists are visually
      // distinguishable. listStack.size() == 1 for top-level, 2 for first nested, etc.
      if (strcmp(name, "li") == 0 && !cssStyle.hasMarginLeft() && !self->listStack.empty()) {
        const int depth = static_cast<int>(std::min(self->listStack.size(), size_t(3)));
        blockStyle.marginLeft = static_cast<int16_t>(emSize * 1.5f * depth);
        // Stage-1: fold the list indent as an em-based CssLength (1.5em per nesting level) — NOT
        // the px value, which depends on emSize and would make content.bin settings-dependent. The
        // sink resolves 1.5*depth em with its own emSize to the identical px, keeping content.bin
        // settings-independent (compiled-content-format.md: px belongs to the Stage-2 cache).
        self->currentCssStyle.marginLeft = CssLength(1.5f * static_cast<float>(depth), CssUnit::Em);
        self->currentCssStyle.defined.marginLeft = 1;
      }
      self->startNewTextBlock(blockStyle);
      self->updateEffectiveInlineStyle();

      if (strcmp(name, "li") == 0) {
        if (!self->listStack.empty()) {
          if (self->listStack.back().isOrdered) {
            const char* valueAttr = getAttribute(atts, "value");
            if (valueAttr) {
              int v = atoi(valueAttr);
              if (v > 0) self->listStack.back().counter = v - 1;
            }
            self->listStack.back().counter += 1;
          }
          if (!self->listStack.back().suppressMarker) {
            char marker[16];
            if (self->listStack.back().isOrdered) {
              snprintf(marker, sizeof(marker), "%d.", self->listStack.back().counter);
            } else {
              strcpy(marker, "\xe2\x80\xa2");
            }
            self->currentTextBlock->addWord(marker, EpdFontFamily::REGULAR);
            self->stage1AddWord(marker, EpdFontFamily::REGULAR, ParsedText::DEFAULT_WORD_SIZE_PCT, false);
          }
        }
      } else if (strcmp(name, "pre") == 0) {
        // Record depth so characterData can treat \n as a hard line break inside <pre>.
        // depth has not been incremented yet here; it will be after startElement returns.
        self->preUntilDepth = std::min(self->preUntilDepth, self->depth);
      }
    }
  } else if (strcmp(name, "hr") == 0) {
    if (self->partWordBufferIndex > 0) {
      if (!self->flushPartWordBuffer()) return;
    }
    // Emit the rule as a bare HR block (flushes any pending text first, preserving document order),
    // then open the following empty block. LayoutSink::placeHr draws the centered rule + margins.
    self->stage1EmitHrBlock();
    BlockStyle emptyStyle;
    self->startNewTextBlock(emptyStyle);
  } else if (matches(name, UNDERLINE_TAGS, NUM_UNDERLINE_TAGS) ||
             matches(name, STRIKETHROUGH_TAGS, NUM_STRIKETHROUGH_TAGS)) {
    // Flush buffer before style change so preceding text gets current style
    if (self->partWordBufferIndex > 0) {
      const bool endsAtDashBreak = bufferEndsWithBreakableDash(self->partWordBuffer, self->partWordBufferIndex);
      if (!self->flushPartWordBuffer()) return;
      if (!endsAtDashBreak) {
        self->nextWordContinues = true;
      }
    }
    if (matches(name, UNDERLINE_TAGS, NUM_UNDERLINE_TAGS)) {
      self->underlineUntilDepth = std::min(self->underlineUntilDepth, self->depth);
    }
    if (matches(name, STRIKETHROUGH_TAGS, NUM_STRIKETHROUGH_TAGS)) {
      self->strikethroughUntilDepth = std::min(self->strikethroughUntilDepth, self->depth);
    }
    // Push inline style entry for underline/strikethrough tag
    StyleStackEntry entry;
    entry.depth = self->depth;  // Track depth for matching pop
    if (matches(name, UNDERLINE_TAGS, NUM_UNDERLINE_TAGS)) {
      entry.hasUnderline = true;
      entry.underline = true;
    }
    if (matches(name, STRIKETHROUGH_TAGS, NUM_STRIKETHROUGH_TAGS)) {
      entry.hasStrikethrough = true;
      entry.strikethrough = true;
    }
    if (cssStyle.hasTextDecoration()) {
      const uint8_t dec = static_cast<uint8_t>(cssStyle.textDecoration);
      if (dec & static_cast<uint8_t>(CssTextDecoration::Underline)) {
        entry.hasUnderline = true;
        entry.underline = true;
        self->underlineUntilDepth = std::min(self->underlineUntilDepth, self->depth);
      }
      if (dec & static_cast<uint8_t>(CssTextDecoration::LineThrough)) {
        entry.hasStrikethrough = true;
        entry.strikethrough = true;
        self->strikethroughUntilDepth = std::min(self->strikethroughUntilDepth, self->depth);
      }
    }
    if (cssStyle.hasFontWeight()) {
      entry.hasBold = true;
      entry.bold = cssStyle.fontWeight == CssFontWeight::Bold;
    }
    if (cssStyle.hasFontStyle()) {
      entry.hasItalic = true;
      entry.italic = cssStyle.fontStyle == CssFontStyle::Italic;
    }
    applyCssFontSizeToEntry(entry, cssStyle);
    self->inlineStyleStack.push_back(entry);
    self->updateEffectiveInlineStyle();
  } else if (matches(name, BOLD_TAGS, NUM_BOLD_TAGS)) {
    // Flush buffer before style change so preceding text gets current style
    if (self->partWordBufferIndex > 0) {
      const bool endsAtDashBreak = bufferEndsWithBreakableDash(self->partWordBuffer, self->partWordBufferIndex);
      if (!self->flushPartWordBuffer()) return;
      if (!endsAtDashBreak) {
        self->nextWordContinues = true;
      }
    }
    self->boldUntilDepth = std::min(self->boldUntilDepth, self->depth);
    // Push inline style entry for bold tag
    StyleStackEntry entry;
    entry.depth = self->depth;  // Track depth for matching pop
    entry.hasBold = true;
    entry.bold = true;
    if (cssStyle.hasFontStyle()) {
      entry.hasItalic = true;
      entry.italic = cssStyle.fontStyle == CssFontStyle::Italic;
    }
    if (cssStyle.hasTextDecoration()) {
      const uint8_t dec = static_cast<uint8_t>(cssStyle.textDecoration);
      if (dec & static_cast<uint8_t>(CssTextDecoration::Underline)) {
        entry.hasUnderline = true;
        entry.underline = true;
      }
      if (dec & static_cast<uint8_t>(CssTextDecoration::LineThrough)) {
        entry.hasStrikethrough = true;
        entry.strikethrough = true;
      }
    }
    applyCssFontSizeToEntry(entry, cssStyle);
    self->inlineStyleStack.push_back(entry);
    self->updateEffectiveInlineStyle();
  } else if (matches(name, ITALIC_TAGS, NUM_ITALIC_TAGS)) {
    // Flush buffer before style change so preceding text gets current style
    if (self->partWordBufferIndex > 0) {
      const bool endsAtDashBreak = bufferEndsWithBreakableDash(self->partWordBuffer, self->partWordBufferIndex);
      if (!self->flushPartWordBuffer()) return;
      if (!endsAtDashBreak) {
        self->nextWordContinues = true;
      }
    }
    self->italicUntilDepth = std::min(self->italicUntilDepth, self->depth);
    // Push inline style entry for italic tag
    StyleStackEntry entry;
    entry.depth = self->depth;  // Track depth for matching pop
    entry.hasItalic = true;
    entry.italic = true;
    if (cssStyle.hasFontWeight()) {
      entry.hasBold = true;
      entry.bold = cssStyle.fontWeight == CssFontWeight::Bold;
    }
    if (cssStyle.hasTextDecoration()) {
      const uint8_t dec = static_cast<uint8_t>(cssStyle.textDecoration);
      if (dec & static_cast<uint8_t>(CssTextDecoration::Underline)) {
        entry.hasUnderline = true;
        entry.underline = true;
      }
      if (dec & static_cast<uint8_t>(CssTextDecoration::LineThrough)) {
        entry.hasStrikethrough = true;
        entry.strikethrough = true;
      }
    }
    applyCssFontSizeToEntry(entry, cssStyle);
    self->inlineStyleStack.push_back(entry);
    self->updateEffectiveInlineStyle();
  } else if (strcmp(name, "sup") == 0 || strcmp(name, "sub") == 0) {
    if (self->partWordBufferIndex > 0) {
      if (!self->flushPartWordBuffer()) return;
      self->nextWordContinues = true;
    }
    StyleStackEntry entry;
    entry.depth = self->depth;
    if (strcmp(name, "sup") == 0) {
      entry.hasSup = true;
      entry.sup = true;
    } else {
      entry.hasSub = true;
      entry.sub = true;
    }
    applySupSubDefaultSize(entry);
    applyCssFontSizeToEntry(entry, cssStyle);  // explicit CSS font-size overrides the 50% default
    self->inlineStyleStack.push_back(entry);
    self->updateEffectiveInlineStyle();
  } else if (strcmp(name, "span") == 0 || !isHeaderOrBlock(name)) {
    // Handle span and other inline elements for CSS styling.
    // <small>/<big> carry UA-default sizes (80%/120%) even without any CSS rule.
    const bool isSmallTag = strcmp(name, "small") == 0;
    const bool isBigTag = strcmp(name, "big") == 0;
    if (cssStyle.hasFontWeight() || cssStyle.hasFontStyle() || cssStyle.hasTextDecoration() ||
        cssStyle.hasVerticalAlign() || cssStyle.hasSmallCaps() || cssStyle.hasMarginLeft() ||
        cssStyle.hasFontSizeMultiplier() || isSmallTag || isBigTag) {
      // Flush buffer before style change so preceding text gets current style
      if (self->partWordBufferIndex > 0) {
        const bool endsAtDashBreak = bufferEndsWithBreakableDash(self->partWordBuffer, self->partWordBufferIndex);
        if (!self->flushPartWordBuffer()) return;
        if (!endsAtDashBreak) {
          self->nextWordContinues = true;
        }
      }
      StyleStackEntry entry;
      entry.depth = self->depth;  // Track depth for matching pop
      if (cssStyle.hasFontWeight()) {
        entry.hasBold = true;
        entry.bold = cssStyle.fontWeight == CssFontWeight::Bold;
      }
      if (cssStyle.hasFontStyle()) {
        entry.hasItalic = true;
        entry.italic = cssStyle.fontStyle == CssFontStyle::Italic;
      }
      if (cssStyle.hasTextDecoration()) {
        const uint8_t dec = static_cast<uint8_t>(cssStyle.textDecoration);
        if (dec == static_cast<uint8_t>(CssTextDecoration::None)) {
          entry.hasUnderline = true;
          entry.underline = false;
          entry.hasStrikethrough = true;
          entry.strikethrough = false;
        } else {
          if (dec & static_cast<uint8_t>(CssTextDecoration::Underline)) {
            entry.hasUnderline = true;
            entry.underline = true;
          }
          if (dec & static_cast<uint8_t>(CssTextDecoration::LineThrough)) {
            entry.hasStrikethrough = true;
            entry.strikethrough = true;
          }
        }
      }
      if (cssStyle.hasVerticalAlign()) {
        if (cssStyle.verticalAlign == CssVerticalAlign::Super) {
          entry.hasSup = true;
          entry.sup = true;
        } else if (cssStyle.verticalAlign == CssVerticalAlign::Sub) {
          entry.hasSub = true;
          entry.sub = true;
        } else {
          // baseline: explicitly cancel any inherited sup/sub
          entry.hasSup = true;
          entry.sup = false;
          entry.hasSub = true;
          entry.sub = false;
        }
      }
      if (cssStyle.hasSmallCaps()) {
        entry.hasSmallCaps = true;
        entry.smallCaps = cssStyle.smallCaps;
      }
      if (cssStyle.hasMarginLeft()) {
        // margin-left on an inline span acts as a per-line indent (poem stanza pattern).
        // Applied immediately to the current block because the span closes before the
        // trailing <br>, so the indent must be on the block that receives the text.
        const int16_t marginPx = cssStyle.marginLeft.toPixelsInt16(emSize, static_cast<float>(self->viewportWidth));
        entry.hasMarginLeft = true;
        entry.marginLeftPx = marginPx;
        if (marginPx > 0 && self->currentTextBlock) {
          BlockStyle updatedStyle = self->currentTextBlock->getBlockStyle();
          updatedStyle.textIndent = marginPx;
          updatedStyle.textIndentDefined = true;
          self->currentTextBlock->setBlockStyle(updatedStyle);
          // Stage-1: this span-level poem indent mutates the OPEN block's style after it was
          // captured; transmit the ORIGINAL em/%-based CssLength (NOT the px-resolved marginPx —
          // px depends on font size + viewport, which would make content.bin settings-dependent).
          // The sink re-resolves em->px with its own emSize, so the result is byte-identical while
          // content.bin stays settings-independent (compiled-content-format.md: px stays Stage-2).
          if (self->effectiveSink()) {
            self->stage1BlockCssStyle_.textIndent = cssStyle.marginLeft;
            self->stage1BlockCssStyle_.defined.textIndent = 1;
          }
        }
      }
      applySupSubDefaultSize(entry);  // vertical-align: super/sub spans get the 50% default
      applyCssFontSizeToEntry(entry, cssStyle);
      if (!entry.hasFontSize && (isSmallTag || isBigTag)) {
        entry.hasFontSize = true;
        entry.fontSizePct = isSmallTag ? 80 : 120;
      }
      self->inlineStyleStack.push_back(entry);
      self->updateEffectiveInlineStyle();
    }
  }

  // Unprocessed tag, just increasing depth and continue forward
  self->depth += 1;
}

void ChapterHtmlSlimParser::characterData(void* userData, const char* s, const int len) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);

  if (self->streamFailed) {
    return;
  }

  // Skip content of nested tables (depth > 1 means we're inside a nested table)
  if (self->currentTable && self->currentTable->depth > 1) {
    return;
  }

  // Route character data into the active table cell's ParsedText
  if (self->currentTableCell) {
    // Use the existing partWordBuffer + word-level accumulation logic below,
    // but the flush target will be currentTableCell->text (handled in flushPartWordBuffer).
    // Fall through to the normal character accumulation path.
  }

  // Middle of skip
  if (self->skipUntilDepth < self->depth) {
    return;
  }

  // Ignore character data inside synthetic zero-height spacer <p> tags.
  if (self->skipTextUntilDepth < self->depth) {
    return;
  }

  // Skip SVG text content (path data, coordinates, etc.) — it would be treated as words
  // and exhaust heap on EPUBs with large inline SVG elements.
  if (self->svgDepth > 0) {
    return;
  }

  // Collect footnote link display text (for the number label)
  // Remove leading/trailing whitespace and square brackets from the
  // footnote link text to normalize noterefs like "[1]" → "1"
  if (self->insideFootnoteLink) {
    int start = 0;
    int end = len - 1;

    // Example input and output texts:
    // "     [  12  ]   " => "12"
    // "   turn to 256  " => "turn to 256"

    // Ignore leading whitespaces and left square brackets
    while (start < len && (isWhitespace(s[start]) || (s[start] == '['))) {
      ++start;
    }

    // Ignore trailing whitespaces and right square brackets
    while (end >= start && (isWhitespace(s[end]) || (s[end] == ']'))) {
      --end;
    }

    // Extract footnote link text
    for (int i = start; (self->currentFootnoteLinkTextLen < sizeof(self->currentFootnote.number) - 1) && (i <= end);
         ++i) {
      self->currentFootnote.number[self->currentFootnoteLinkTextLen++] = s[i];
    }
    self->currentFootnote.number[self->currentFootnoteLinkTextLen] = '\0';
  }

  for (int i = 0; i < len; i++) {
    const unsigned char c = static_cast<unsigned char>(s[i]);

    // Fast path for plain ASCII word characters (> 0x20 and < 0x80).
    // This covers the vast majority of characters in Latin-script text.
    // All multi-byte UTF-8 sequences start with a byte >= 0x80, so this
    // path is safe to take without any further multi-byte checks.
    if (c > 0x20 && c < 0x80) {
      if (self->partWordBufferIndex >= MAX_WORD_SIZE) {
        // Buffer is full — flush before appending. Pure ASCII means no
        // partial multi-byte sequence can be at the boundary.
        if (!self->flushPartWordBuffer()) return;
      }
      self->partWordBuffer[self->partWordBufferIndex++] = s[i];
      continue;
    }

    if (isWhitespace(s[i])) {
      // Inside <pre>: treat \n as a hard line break.
      if (s[i] == '\n' && self->preUntilDepth < self->depth) {
        if (self->partWordBufferIndex > 0) {
          if (!self->flushPartWordBuffer()) return;
        }
        // Blank line: the current block is empty, but we still need to emit a visible
        // empty line.  Add a single space so the block is non-empty and makePages()
        // will produce a line of the correct height instead of reusing the empty block.
        if (self->currentTextBlock->isEmpty()) {
          self->currentTextBlock->addWord(" ", EpdFontFamily::REGULAR);
          self->stage1AddWord(" ", EpdFontFamily::REGULAR, ParsedText::DEFAULT_WORD_SIZE_PCT, false);
        }
        self->startNewTextBlock(self->currentTextBlock->getBlockStyle());
        self->nextWordContinues = false;
        continue;
      }
      // Currently looking at whitespace, if there's anything in the partWordBuffer, flush it
      if (self->partWordBufferIndex > 0) {
        if (!self->flushPartWordBuffer()) return;
      }
      // Whitespace is a real word boundary — reset continuation state
      self->nextWordContinues = false;
      // Skip the whitespace char
      continue;
    }

    // Detect U+00A0 (non-breaking space, UTF-8: 0xC2 0xA0) or
    //        U+202F (narrow no-break space, UTF-8: 0xE2 0x80 0xAF).
    //
    // Both are rendered as a visible space but must never allow a line break around them.
    // We split the no-break space into its own word token and link the surrounding words
    // with continuation flags so the layout engine treats them as an indivisible group.
    //
    // Example: "200&#xA0;Quadratkilometer" or "200&#x202F;Quadratkilometer"
    //   Input bytes:  "200\xC2\xA0Quadratkilometer"  (or 0xE2 0x80 0xAF for U+202F)
    //   Tokens produced:
    //     [0] "200"               continues=false
    //     [1] " "                 continues=true   (attaches to "200", no gap)
    //     [2] "Quadratkilometer"  continues=true   (attaches to " ", no gap)
    //
    //   The continuation flags prevent the line-breaker from inserting a line break
    //   between "200" and "Quadratkilometer". However, "Quadratkilometer" is now a
    //   standalone word for hyphenation purposes, so Liang patterns can produce
    //   "200 Quadrat-" / "kilometer" instead of the unusable "200" / "Quadratkilometer".
    if (static_cast<uint8_t>(s[i]) == 0xC2 && i + 1 < len && static_cast<uint8_t>(s[i + 1]) == 0xA0) {
      if (self->partWordBufferIndex > 0) {
        if (!self->flushPartWordBuffer()) return;
      }

      self->partWordBuffer[0] = ' ';
      self->partWordBuffer[1] = '\0';
      self->partWordBufferIndex = 1;
      self->nextWordContinues = true;  // Attach space to previous word (no break).
      if (!self->flushPartWordBuffer()) return;

      self->nextWordContinues = true;  // Next real word attaches to this space (no break).

      i++;  // Skip the second byte (0xA0)
      continue;
    }

    // U+202F (narrow no-break space) — identical logic to U+00A0 above.
    if (static_cast<uint8_t>(s[i]) == 0xE2 && i + 2 < len && static_cast<uint8_t>(s[i + 1]) == 0x80 &&
        static_cast<uint8_t>(s[i + 2]) == 0xAF) {
      if (self->partWordBufferIndex > 0) {
        if (!self->flushPartWordBuffer()) return;
      }

      self->partWordBuffer[0] = ' ';
      self->partWordBuffer[1] = '\0';
      self->partWordBufferIndex = 1;
      self->nextWordContinues = true;
      if (!self->flushPartWordBuffer()) return;

      self->nextWordContinues = true;

      i += 2;  // Skip the remaining two bytes (0x80 0xAF)
      continue;
    }

    // Skip Zero Width No-Break Space / BOM (U+FEFF) = 0xEF 0xBB 0xBF
    const char FEFF_BYTE_1 = static_cast<char>(0xEF);
    const char FEFF_BYTE_2 = static_cast<char>(0xBB);
    const char FEFF_BYTE_3 = static_cast<char>(0xBF);

    if (s[i] == FEFF_BYTE_1) {
      // Check if the next two bytes complete the 3-byte sequence
      if ((i + 2 < len) && (s[i + 1] == FEFF_BYTE_2) && (s[i + 2] == FEFF_BYTE_3)) {
        // Sequence 0xEF 0xBB 0xBF found!
        i += 2;    // Skip the next two bytes
        continue;  // Move to the next iteration
      }
    }

    // If we're about to run out of space, then cut the word off and start a new one.
    // For CJK text (no spaces), this is the primary word-breaking mechanism.
    // We must avoid splitting multi-byte UTF-8 sequences across word boundaries,
    // otherwise the trailing bytes become orphaned continuation bytes that the
    // decoder can't interpret.
    if (self->partWordBufferIndex >= MAX_WORD_SIZE) {
      int safeLen = utf8SafeTruncateBuffer(self->partWordBuffer, self->partWordBufferIndex);

      if (safeLen < self->partWordBufferIndex && safeLen > 0) {
        // Incomplete UTF-8 sequence at the end — save it before flushing
        int overflow = self->partWordBufferIndex - safeLen;
        char saved[4];
        for (int j = 0; j < overflow; j++) {
          saved[j] = self->partWordBuffer[safeLen + j];
        }
        self->partWordBufferIndex = safeLen;
        if (!self->flushPartWordBuffer()) return;
        for (int j = 0; j < overflow; j++) {
          self->partWordBuffer[j] = saved[j];
        }
        self->partWordBufferIndex = overflow;
      } else {
        if (!self->flushPartWordBuffer()) return;
      }
    }

    self->partWordBuffer[self->partWordBufferIndex++] = s[i];
  }
}

void ChapterHtmlSlimParser::defaultHandlerExpand(void* userData, const char* s, const int len) {
  // Check if this looks like an entity reference (&...;)
  if (len >= 3 && s[0] == '&' && s[len - 1] == ';') {
    const char* utf8Value = lookupHtmlEntity(s, static_cast<size_t>(len));
    if (utf8Value != nullptr) {
      // Known entity: expand to its UTF-8 value
      characterData(userData, utf8Value, strlen(utf8Value));
      return;
    }
    // Unknown entity: preserve original &...; sequence
    characterData(userData, s, len);
    return;
  }
  // Not an entity we recognize - skip it
}

void ChapterHtmlSlimParser::endElement(void* userData, const char* name) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);

  if (self->streamFailed) {
    return;
  }

  // Check if any style state will change after we decrement depth
  // If so, we MUST flush the partWordBuffer with the CURRENT style first
  // Note: depth hasn't been decremented yet, so we check against (depth - 1)
  const bool willPopStyleStack =
      !self->inlineStyleStack.empty() && self->inlineStyleStack.back().depth == self->depth - 1;
  const bool willClearBold = self->boldUntilDepth == self->depth - 1;
  const bool willClearItalic = self->italicUntilDepth == self->depth - 1;
  const bool willClearUnderline = self->underlineUntilDepth == self->depth - 1;
  const bool willClearStrikethrough = self->strikethroughUntilDepth == self->depth - 1;

  const bool styleWillChange =
      willPopStyleStack || willClearBold || willClearItalic || willClearUnderline || willClearStrikethrough;
  const bool headerOrBlockTag = isHeaderOrBlock(name);
  const bool tableStructuralTag = isTableStructuralTag(name);

  if (self->currentTable && self->currentTable->depth > 1 && strcmp(name, "table") == 0) {
    self->partWordBufferIndex = 0;
    self->currentTable->depth -= 1;
    self->depth -= 1;
    LOG_DBG("EHP", "nested table end, depth now %d", self->currentTable->depth);
    return;
  }

  // Flush buffer with current style BEFORE any style changes
  if (self->partWordBufferIndex > 0) {
    // Flush if style will change OR if we're closing a block/structural element
    const bool isInlineTag =
        !headerOrBlockTag && !tableStructuralTag && !matches(name, IMAGE_TAGS, NUM_IMAGE_TAGS) && self->depth != 1;
    const bool shouldFlush = styleWillChange || headerOrBlockTag || matches(name, BOLD_TAGS, NUM_BOLD_TAGS) ||
                             matches(name, ITALIC_TAGS, NUM_ITALIC_TAGS) ||
                             matches(name, UNDERLINE_TAGS, NUM_UNDERLINE_TAGS) ||
                             matches(name, STRIKETHROUGH_TAGS, NUM_STRIKETHROUGH_TAGS) || tableStructuralTag ||
                             matches(name, IMAGE_TAGS, NUM_IMAGE_TAGS) || self->depth == 1;

    if (shouldFlush) {
      const bool endsAtDashBreak = bufferEndsWithBreakableDash(self->partWordBuffer, self->partWordBufferIndex);
      if (!self->flushPartWordBuffer()) return;
      // If closing an inline element, the next word fragment continues the same visual word —
      // unless the buffered text ended at a dash that should allow a line break (em/en dash, etc.).
      if (isInlineTag && !endsAtDashBreak) {
        self->nextWordContinues = true;
      }
    }
  }

  self->depth -= 1;

  // Decrement float depth when the floated element's scope closes.
  while (self->floatDepth_ > 0 && self->floatOpenDepths_[self->floatDepth_ - 1] >= self->depth) {
    self->floatDepth_--;
  }

  if (strcmp(name, "svg") == 0 && self->svgDepth > 0) {
    self->svgDepth -= 1;
  }

  // Pop list entries whose ul/ol is now out of scope
  while (!self->listStack.empty() && self->listStack.back().depth >= self->depth) {
    self->listStack.pop_back();
  }

  // Closing a footnote link — create entry from collected text and href
  if (self->insideFootnoteLink && self->depth == self->footnoteLinkDepth) {
    if (self->currentFootnote.number[0] != '\0' && self->currentFootnote.href[0] != '\0') {
      FootnoteEntry entry = self->currentFootnote;
      int wordIndex =
          self->wordsExtractedInBlock + (self->currentTextBlock ? static_cast<int>(self->currentTextBlock->size()) : 0);
      self->pendingFootnotes.push_back({wordIndex, entry});
      // Stage-1: anchor the footnote to the current block at the same word position.
      if (auto* sink = self->effectiveSink()) sink->onFootnote(wordIndex, entry);
    }
    if (self->inlineFootnotePreviews && self->currentFootnote.href[0] != '\0') {
      // Membership in the book-level preview cache is the gate: the gatherer only
      // stored targets of footnote-shaped links, so any resolving href — same-file or
      // cross-file ("../Text/notes.xhtml#n3", Calibre filepos anchors) — is a real note.
      std::string preview;
      if (self->inlineFootnotePreviews->find(self->currentFootnote.href, preview)) {
        self->pendingInlineFootnotePreview = self->abbreviateInlineFootnote(preview.c_str());
        if (!self->pendingInlineFootnotePreview.empty()) {
          LOG_DBG("EHP", "Expanded inline footnote: href=%s previewBytes=%u", self->currentFootnote.href,
                  static_cast<uint32_t>(self->pendingInlineFootnotePreview.size()));
        }
      }
    }
    self->insideFootnoteLink = false;
  }

  // Leaving skip
  if (self->skipUntilDepth == self->depth) {
    self->skipUntilDepth = INT_MAX;
  }

  // Leaving zero-height spacer paragraph text-skip scope
  if (self->skipTextUntilDepth == self->depth) {
    self->skipTextUntilDepth = INT_MAX;
  }

  if (self->currentTable && self->currentTable->depth == 1 && (strcmp(name, "td") == 0 || strcmp(name, "th") == 0)) {
    if (self->partWordBufferIndex > 0) {
      if (!self->flushPartWordBuffer()) return;
    }
    // Determine if the whole row consists of header cells
    if (!self->currentTable->rows.empty()) {
      auto& row = self->currentTable->rows.back();
      bool allHeaders = !row.cells.empty();
      for (const auto& c : row.cells) {
        if (!c.isHeader) {
          allHeaders = false;
          break;
        }
      }
      row.isHeaderRow = allHeaders;
    }
    self->currentTableCell = nullptr;
    self->nextWordContinues = false;
  }

  if (self->currentTable && self->currentTable->depth == 1 && strcmp(name, "tr") == 0) {
    self->nextWordContinues = false;
  }

  if (self->currentTable && self->currentTable->depth == 1 && strcmp(name, "table") == 0) {
    if (self->partWordBufferIndex > 0) {
      if (!self->flushPartWordBuffer()) return;
    }
    self->currentTableCell = nullptr;
    self->emitBufferedTable();
    self->currentTable.reset();
    self->nextWordContinues = false;
  }

  // Leaving bold tag
  if (self->boldUntilDepth == self->depth) {
    self->boldUntilDepth = INT_MAX;
  }

  // Leaving italic tag
  if (self->italicUntilDepth == self->depth) {
    self->italicUntilDepth = INT_MAX;
  }

  // Leaving underline tag
  if (self->underlineUntilDepth == self->depth) {
    self->underlineUntilDepth = INT_MAX;
  }

  // Leaving strikethrough tag
  if (self->strikethroughUntilDepth == self->depth) {
    self->strikethroughUntilDepth = INT_MAX;
  }

  // Leaving pre tag
  if (self->preUntilDepth == self->depth) {
    self->preUntilDepth = INT_MAX;
  }

  // Pop from inline style stack if we pushed an entry at this depth
  // This handles all inline elements: b, i, u, span, etc.
  if (!self->inlineStyleStack.empty() && self->inlineStyleStack.back().depth == self->depth) {
    self->inlineStyleStack.pop_back();
    self->updateEffectiveInlineStyle();
  }

  if (!self->pendingInlineFootnotePreview.empty()) {
    std::string preview = " (";
    preview += self->pendingInlineFootnotePreview;
    preview += ")";
    self->pendingInlineFootnotePreview.clear();

    const bool surroundingItalic = self->effectiveItalic;
    self->effectiveItalic = true;
    characterData(self, preview.c_str(), static_cast<int>(preview.size()));
    if (self->partWordBufferIndex > 0 && !self->flushPartWordBuffer()) {
      self->effectiveItalic = surroundingItalic;
      return;
    }
    self->effectiveItalic = surroundingItalic;
  }

  // Clear block style when leaving header or block elements
  if (headerOrBlockTag) {
    self->currentCssStyle.reset();
    self->updateEffectiveInlineStyle();

    // Reset alignment on empty text blocks to prevent stale alignment from bleeding
    // into the next sibling element. This fixes issue #1026 where an empty <h1> (default
    // Center) followed by an image-only <p> causes Center to persist through the chain
    // of empty block reuse into subsequent text paragraphs.
    // Margins/padding are preserved so parent element spacing still accumulates correctly.
    if (self->currentTextBlock && self->currentTextBlock->isEmpty()) {
      auto style = self->currentTextBlock->getBlockStyle();
      // Keep alignment only when closing the <br> separator itself so subsequent text
      // within the same block container stays aligned. Reset alignment when closing
      // other block tags (e.g. div/p) to avoid leaking centered/right alignment globally.
      const bool preserveForBrClose = style.fromBrElement && strcmp(name, "br") == 0;
      if (!preserveForBrClose) {
        style.textAlignDefined = false;
        style.alignment = (self->paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None))
                              ? CssTextAlign::Justify
                              : static_cast<CssTextAlign>(self->paragraphAlignment);
        self->currentTextBlock->setBlockStyle(style);
      }
    }
  }
}

ChapterHtmlSlimParser::~ChapterHtmlSlimParser() = default;

compiled::BlockSink* ChapterHtmlSlimParser::effectiveSink() const {
  // A ContentSink compile (external stage1Sink_) is content-only and does not need pages, so it
  // REPLACES the internal layout sink for that build; otherwise the internal layout sink drives the
  // section-cache/device path. Never both — one sink at a time, no fan-out.
  return stage1Sink_ ? stage1Sink_ : static_cast<compiled::BlockSink*>(layoutSink_.get());
}

// Getter proxies (step 6 unify). When the internal LayoutSink drove output, return its tables; the
// fused anchorData/pageBreakLabels/paragraphLutPerPage are then unread scratch. When an external
// ContentSink compiled (no internal sink), fall back to the fused tables.
const std::vector<std::pair<std::string, uint16_t>>& ChapterHtmlSlimParser::getAnchors() const {
  return layoutSink_ ? layoutSink_->anchors() : anchorData;
}
const std::vector<std::pair<uint16_t, std::string>>& ChapterHtmlSlimParser::getPageBreakLabels() const {
  return layoutSink_ ? layoutSink_->pageBreakLabels() : pageBreakLabels;
}
const std::vector<ChapterHtmlSlimParser::ParagraphLutEntry>& ChapterHtmlSlimParser::getParagraphLutPerPage() const {
  if (!layoutSink_) return paragraphLutPerPage;
  // Adapt the sink's field-identical LayoutLutEntry vector to the parser's ParagraphLutEntry type.
  lutAdapter_.clear();
  lutAdapter_.reserve(layoutSink_->paragraphLutPerPage().size());
  for (const auto& e : layoutSink_->paragraphLutPerPage()) {
    lutAdapter_.push_back({e.xhtmlByteOffset, e.paragraphIndex, e.listItemIndex});
  }
  return lutAdapter_;
}

bool ChapterHtmlSlimParser::setup(const size_t totalInflatedSize) {
  // Construct the internal layout sink from the parser's settings members BEFORE the first
  // startNewTextBlock (which drives the producer). Skipped when an external ContentSink is attached
  // — that build is content-only and needs no pages. (Step 6 unify; see effectiveSink().)
  if (!stage1Sink_) {
    compiled::LayoutParams lp;
    lp.fontId = fontId;
    lp.lineCompression = lineCompression;
    lp.extraParagraphSpacing = extraParagraphSpacing;
    lp.paragraphAlignment = paragraphAlignment;
    lp.viewportWidth = viewportWidth;
    lp.viewportHeight = viewportHeight;
    lp.hyphenationEnabled = hyphenationEnabled;
    lp.bionicReadingEnabled = bionicReadingEnabled;
    lp.embeddedStyle = embeddedStyle;
    lp.fontSizeLadder = fontSizeLadder_;  // set by Section before setup()
    lp.imageBasePath = imageBasePath;
    lp.epubFilePath = epub ? epub->getPath() : std::string();
    layoutSink_ = std::make_unique<compiled::LayoutSink>(renderer, std::move(lp), completePageFn);
  }

  auto paragraphAlignmentBlockStyle = BlockStyle();
  paragraphAlignmentBlockStyle.textAlignDefined = true;
  // Resolve None sentinel to Justify for initial block (no CSS context yet)
  const auto align = (this->paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None))
                         ? CssTextAlign::Justify
                         : static_cast<CssTextAlign>(this->paragraphAlignment);
  paragraphAlignmentBlockStyle.alignment = align;
  startNewTextBlock(paragraphAlignmentBlockStyle);

  // Handle HTML entities (like &nbsp;) that aren't in XML spec or DTD.
  // Using DefaultHandlerExpand preserves normal entity expansion from DOCTYPE.
  // Chapter XHTML is HTML-flavored: enable bare-void-tag repair (<br>, <img>, ...).
  if (!saxParser_.init(this, startElement, endElement, characterData, defaultHandlerExpand,
                       /*htmlVoidTagRepair=*/true)) {
    LOG_ERR("EHP", "Couldn't allocate memory for parser");
    return false;
  }

  totalStreamSize = totalInflatedSize;
  bytesStreamed = 0;
  lastReportedProgress = -1;
  streamFailed = false;
  layoutFailed = false;
  streamStartTimeMs = millis();

  // Choose progress granularity by chapter size. Each callback drives a full-screen
  // e-ink refresh (~640ms), so smaller chapters skip mid-parse ticks entirely.
  // progressStepPercent == 0 means "popup only, no mid-parse updates".
  progressStepPercent = 0;
  if (totalStreamSize >= SIZE_FOR_PROGRESS_FINE) {
    progressStepPercent = 25;
  } else if (totalStreamSize >= SIZE_FOR_PROGRESS_HEARTBEAT) {
    progressStepPercent = 50;
  }

  const uint32_t popupFreeHeap = ESP.getFreeHeap();
  const uint32_t popupContigHeap = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_DEFAULT);
  progressUiEnabled =
      popupFreeHeap >= MIN_FREE_HEAP_FOR_INDEXING_POPUP && popupContigHeap >= MIN_CONTIG_HEAP_FOR_INDEXING_POPUP;
  if (!progressUiEnabled) {
    LOG_DBG("EHP", "Skipping indexing popup due to low heap (free=%u contig=%u)", popupFreeHeap, popupContigHeap);
    // When popup is disabled, also disable mid-parse ticks.
    progressStepPercent = 0;
  }

  // Show initial progress popup for files above threshold.
  if (progressFn && progressUiEnabled && totalStreamSize >= MIN_SIZE_FOR_POPUP) {
    progressFn(0);
  }
  return true;
}

size_t ChapterHtmlSlimParser::write(const uint8_t data) { return write(&data, 1); }

size_t ChapterHtmlSlimParser::write(const uint8_t* buffer, const size_t size) {
  if (size == 0) return 0;
  if (!saxParser_.isActive() || streamFailed) return 0;

  bytesStreamed += size;
  // The streaming source doesn't know "this was the last chunk" — pass isFinal=false
  // here and let finalize() emit the terminating empty parse with isFinal=true.
  if (!saxParser_.feed(buffer, size)) {
    LOG_ERR("EHP", "Parse error at line %d:\n%s", saxParser_.errorLine(), saxParser_.errorString());
    streamFailed = true;
    return 0;
  }

  // Report progress at the granularity chosen up-front (see progressStepPercent).
  // Skip the 100% callback — the page render that follows immediately replaces the popup,
  // so the final tick is wasted work.
  if (progressFn && progressUiEnabled && progressStepPercent > 0 && totalStreamSize > 0) {
    const int progress = static_cast<int>(bytesStreamed * 100 / totalStreamSize);
    if (progress < 100 && progress / progressStepPercent > lastReportedProgress / progressStepPercent) {
      lastReportedProgress = progress;
      progressFn(progress);
    }
  }

  return size;
}

bool ChapterHtmlSlimParser::finalize() {
  bool success = !streamFailed;
  if (saxParser_.isActive()) {
    // Emit terminating empty parse so the parser finalizes any pending tokens.
    if (success && !saxParser_.finalize()) {
      LOG_ERR("EHP", "Parse error at line %d (finalize):\n%s", saxParser_.errorLine(), saxParser_.errorString());
      success = false;
      streamFailed = true;
    }
  }

  const uint32_t totalTimeMs = millis() - streamStartTimeMs;
  LOG_DBG("EHP", "Time to parse and build pages: %lu ms", totalTimeMs);

  // The yxml SaxParser backend uses fixed-capacity buffers sized from measured
  // real-world maxima; if this chapter exceeded any of them the parser silently
  // truncated (dropped) the excess. Surface it so out-of-bounds documents are
  // diagnosable rather than failing invisibly (e.g. XPath/anchor drift). Also
  // surfaces voidTag: the source had an HTML-style unclosed void element
  // (<br>, <hr>, ...) that the parser auto-closed rather than failing on.
  if (const uint32_t trunc = saxParser_.truncationFlags()) {
    LOG_DBG("EHP",
            "SaxParser hit fixed-capacity limits (flags=0x%lx): elemName=%d attrName=%d attrVal=%d maxAttrs=%d "
            "maxDepth=%d voidTag=%d",
            static_cast<unsigned long>(trunc), (trunc & SaxParser::kTruncElemName) != 0,
            (trunc & SaxParser::kTruncAttrName) != 0, (trunc & SaxParser::kTruncAttrValue) != 0,
            (trunc & SaxParser::kTruncMaxAttrs) != 0, (trunc & SaxParser::kTruncMaxDepth) != 0,
            (trunc & SaxParser::kVoidTagRepaired) != 0);
  }

  // Stage-1: hand off the final accumulated block (no subsequent stage1OpenBlock will)
  // and signal end-of-spine so the sink can flush any trailing state.
  stage1FlushBlock();
  if (auto* sink = effectiveSink()) sink->onSpineEnd();

  // Process last page if there is still text. Done unconditionally so that a partial
  // success scenario still flushes whatever pages were produced.
  if (currentTextBlock) {
    makePages();
    if (!layoutFailed) {
      const bool hasFinalPageContent = currentPage && !currentPage->elements.empty();
      if (!pendingAnchorId.empty()) {
        uint16_t anchorPage = static_cast<uint16_t>(completedPageCount);
        // Avoid mapping trailing anchors to a non-existent blank page when the
        // chapter ended exactly on a page boundary.
        if (!hasFinalPageContent && completedPageCount > 0) {
          anchorPage = static_cast<uint16_t>(completedPageCount - 1);
        }
        anchorData.push_back({std::move(pendingAnchorId), anchorPage});
        pendingAnchorId.clear();
      }
      if (hasFinalPageContent) {
        emitPage(0u);  // post-parse: no byte offset available
      }
    }
    currentPage.reset();
    currentTextBlock.reset();
  }

  return success;
}

void ChapterHtmlSlimParser::resolveBlockFont(BlockStyle& bs) {
  if (bs.fontResolved) return;
  bs.fontResolved = true;
  if (bs.headingFontId != 0 || bs.fontSizeMultiplier == 1.0f) return;
  const FontSizeLadder::Resolved r = fontSizeLadder_.resolve(bs.fontSizeMultiplier * 100.0f);
  if (r.fontId == 0) {
    // Nearest rung is the body font (or the ladder is empty, e.g. SD fonts):
    // keep the pure-scale path — identical to the legacy behavior.
    bs.fontSizeMultiplier = r.residual;
    return;
  }
  if (auxFontId_ == 0) auxFontId_ = r.fontId;
  if (r.fontId != auxFontId_) return;  // aux budget already claimed by another size — scale fallback
  bs.headingFontId = r.fontId;
  bs.fontSizeMultiplier = r.residual;
}

int ChapterHtmlSlimParser::effectiveLineHeight(const BlockStyle& bs) const {
  return static_cast<int>(renderer.getLineHeight(effectiveFontId(bs)) * lineCompression * bs.fontSizeMultiplier + 0.5f);
}

ParsedText::LineProcessResult ChapterHtmlSlimParser::addLineToPage(std::shared_ptr<TextBlock> line,
                                                                   const bool lineEndsWithHyphenatedWord,
                                                                   const bool suppressHyphenationRetry) {
  // Lines carrying inline-sized words advance by the tallest word on the line
  // (microreader semantics); uniform lines keep the block line height exactly.
  int lineHeight = effectiveLineHeight(line->getBlockStyle());
  const uint8_t maxPct = line->maxSizePct();
  if (maxPct != 100) {
    lineHeight = lineHeight * maxPct / 100;
  }

  if (!currentPage) {
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  if (currentPageNextY + lineHeight > viewportHeight) {
    emitPage(lastBodyChildByteOffset);
  }

  const bool noRoomForAnotherLine =
      currentPageNextY + lineHeight <= viewportHeight && currentPageNextY + (lineHeight * 2) > viewportHeight;
  if (lineEndsWithHyphenatedWord && !suppressHyphenationRetry && noRoomForAnotherLine) {
    const std::string linePreview = buildTextBlockPreview(line);
    LOG_DBG("EHP", "Requesting line rerender without hyphenation to avoid page-break split word: %s",
            linePreview.c_str());
    return ParsedText::LineProcessResult::RetryWithoutHyphenation;
  }

  // Capture first-line flag before incrementing wordsExtractedInBlock.
  const bool isFirstLineOfBlock = (wordsExtractedInBlock == 0);

  // Track cumulative words to assign footnotes to the page containing their anchor
  wordsExtractedInBlock += line->wordCount();
  auto footnoteIt = pendingFootnotes.begin();
  while (footnoteIt != pendingFootnotes.end() && footnoteIt->first <= wordsExtractedInBlock) {
    currentPage->addFootnote(footnoteIt->second.number, footnoteIt->second.href);
    ++footnoteIt;
  }
  pendingFootnotes.erase(pendingFootnotes.begin(), footnoteIt);

  // Apply horizontal left inset (margin + padding) as x position offset.
  // For lines that overlap an active left float zone, also shift right by the zone
  // width so text starts after the image rather than overlapping it.
  // Right-floated zones narrow the line width (handled in widthForLine) but don't shift text left.
  int16_t xOffset = line->getBlockStyle().leftInset();
  {
    const auto& bs = line->getBlockStyle();
    for (int zi = 0; zi < bs.floatZoneCount; ++zi) {
      const auto& z = bs.floatZones[zi];
      if (!z.isRight && currentPageNextY < z.bottom && currentPageNextY + lineHeight > z.top) {
        xOffset = static_cast<int16_t>(xOffset + z.width);
      }
    }
  }
  currentPage->elements.push_back(std::make_shared<PageLine>(line, xOffset, currentPageNextY));

  // On the first line of a block with a deferred inline image, fix the image's
  // yPos so its top aligns with the glyph top of the first text line.
  // PageLine y and image y both use the same coordinate: the line's top edge.
  // Float zones were already pre-corrected in makePages() to the same value.
  if (isFirstLineOfBlock && deferredPageImage_) {
    deferredPageImage_->yPos = static_cast<int16_t>(currentPageNextY);
    deferredPageImage_.reset();
  }

  currentPageNextY += lineHeight;
  return ParsedText::LineProcessResult::Accepted;
}

void ChapterHtmlSlimParser::makePages() {
  if (layoutFailed) {
    currentTextBlock.reset();
    return;
  }

  if (!currentTextBlock) {
    LOG_ERR("EHP", "!! No text block to make pages for !!");
    return;
  }

  if (!currentPage) {
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  // Snap the block to the size ladder before any metric below is computed. Uniform
  // per-word sizes (a span wrapping the whole paragraph) fold into the block multiplier
  // first so they benefit too; continuations skip the fold — their first chunk already
  // laid out with the resolved style, and resolveBlockFont is a no-op on them anyway.
  if (!currentTextBlock->isContinuation()) {
    currentTextBlock->foldUniformWordSizes();
  }
  resolveBlockFont(currentTextBlock->getBlockStyle());

  const BlockStyle& blockStyle = currentTextBlock->getBlockStyle();
  const int lineHeight = effectiveLineHeight(blockStyle);

  // Apply top spacing before the paragraph — skip for continuation fragments
  // (words left over after an intermediate flush): the top margin was already
  // applied before the first set of lines from this logical paragraph.
  if (!currentTextBlock->isContinuation()) {
    if (blockStyle.marginTop > 0) {
      // CSS margin collapsing: gap between adjacent blocks = max(prevMarginBottom, thisMarginTop).
      // lastBlockMarginBottom was already added after the previous block; subtract the overlap.
      const int16_t collapse = std::min(lastBlockMarginBottom, blockStyle.marginTop);
      currentPageNextY += static_cast<int16_t>(blockStyle.marginTop - collapse);
    }
    if (blockStyle.paddingTop > 0) {
      currentPageNextY += blockStyle.paddingTop;
    }
  }
  lastBlockMarginBottom = 0;

  // Calculate effective width accounting for horizontal margins/padding
  const int horizontalInset = blockStyle.totalHorizontalInset();
  const uint16_t effectiveWidth =
      (horizontalInset < viewportWidth) ? static_cast<uint16_t>(viewportWidth - horizontalInset) : viewportWidth;

  if (!ensureHeapForTextLayout("paragraph layout")) {
    layoutFailed = true;
    currentTextBlock.reset();
    return;
  }

  // Active-float propagation. A tall floated image spans several text blocks (its
  // caption plus the following paragraphs). The image is attached to the first of
  // those blocks; here we re-inject the same zone into every later block that still
  // overlaps the image vertically, so they all wrap beside it — then drop it once
  // layout has passed the image bottom.
  const bool isOriginatingBlock = static_cast<bool>(deferredPageImage_);
  if (activeFloatBottom_ > 0 && currentPageNextY >= activeFloatBottom_) {
    activeFloatBottom_ = 0;  // layout has moved past the image; float no longer applies
  }
  if (!isOriginatingBlock && activeFloatBottom_ > 0 && currentPageNextY < activeFloatBottom_ &&
      currentTextBlock->getBlockStyle().floatZoneCount == 0) {
    BlockStyle& mbs = currentTextBlock->getBlockStyle();
    auto& z = mbs.floatZones[mbs.floatZoneCount++];
    z.top = activeFloatTop_;  // absolute (already-anchored) image coordinates
    z.bottom = activeFloatBottom_;
    z.width = activeFloatWidth_;
    z.isRight = activeFloatIsRight_;
  }

  // Pre-correct float zone coordinates before line-breaking so widthForLine
  // and the xOffset check in addLineToPage use the same y values. Only the
  // originating block re-anchors (its zone, and the image, snap to the first
  // line top); injected zones already carry absolute image coordinates.
  const int lineHeightForFloat = (blockStyle.floatZoneCount > 0) ? effectiveLineHeight(blockStyle) : 0;
  if (isOriginatingBlock && blockStyle.floatZoneCount > 0) {
    auto& mbs = currentTextBlock->getBlockStyle();
    for (int zi = 0; zi < mbs.floatZoneCount; ++zi) {
      const int imgH = mbs.floatZones[zi].bottom - mbs.floatZones[zi].top;
      mbs.floatZones[zi].top = static_cast<int16_t>(currentPageNextY);
      mbs.floatZones[zi].bottom = static_cast<int16_t>(currentPageNextY + imgH);
    }
    // Finalise the active-float extent so following blocks reference the image's
    // real on-page position (after this block's top margin).
    activeFloatTop_ = static_cast<int16_t>(currentPageNextY);
    activeFloatBottom_ = static_cast<int16_t>(currentPageNextY + (mbs.floatZones[0].bottom - mbs.floatZones[0].top));
  }
  currentTextBlock->layoutAndExtractLines(
      renderer, fontId, effectiveWidth,
      [this](const std::shared_ptr<TextBlock>& textBlock, const bool lineEndsWithHyphenatedWord,
             const bool suppressHyphenationRetry) {
        return addLineToPage(textBlock, lineEndsWithHyphenatedWord, suppressHyphenationRetry);
      },
      /*includeLastLine=*/true, static_cast<int16_t>(currentPageNextY), lineHeightForFloat);

  // Fallback: transfer any remaining pending footnotes to current page.
  // Normally addLineToPage handles this via word-index tracking, but this catches
  // edge cases where a footnote's word index equals the exact block size.
  if (!pendingFootnotes.empty() && currentPage) {
    for (const auto& [idx, fn] : pendingFootnotes) {
      currentPage->addFootnote(fn.number, fn.href);
    }
    pendingFootnotes.clear();
  }

  // Apply bottom spacing after the paragraph (stored in pixels)
  if (blockStyle.marginBottom > 0) {
    currentPageNextY += blockStyle.marginBottom;
    lastBlockMarginBottom = blockStyle.marginBottom;
  } else {
    lastBlockMarginBottom = 0;
  }
  if (blockStyle.paddingBottom > 0) {
    currentPageNextY += blockStyle.paddingBottom;
  }

  // Extra paragraph spacing if enabled (default behavior).
  // Suppressed between lines within a <pre> block so code/preformatted text is not
  // double-spaced; the last line of the block is flushed after </pre> is closed and
  // preUntilDepth has already been reset, so it still receives normal paragraph spacing.
  if (extraParagraphSpacing && preUntilDepth == INT_MAX) {
    currentPageNextY += lineHeight / 2;
  }
}

// Guard: minimum free heap before attempting table layout (cell wrapping allocates TextBlock vectors)
static constexpr size_t MIN_FREE_HEAP_FOR_TABLE = 20 * 1024;

void ChapterHtmlSlimParser::emitBufferedTable() {
  if (!currentTable) return;
  // Emit the settings-independent Table block. LayoutSink::placeTable reproduces the grid-vs-
  // paragraph decision (font-dependent) and PageTableFragment / fallback placement.
  stage1EmitTableBlock(*currentTable);
}
