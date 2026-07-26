#pragma once
// Coarse push seam between the settings-independent XHTML walk and its consumers
// (design in docs/stage1-extraction-design.md). One handoff per SEMANTIC block — a
// fully-built compiled::Block. Two implementations:
//   - LayoutSink  — Stage-2: onBlock() measures + paginates into Pages.
//   - ContentSink — Stage-1 persist: interns the style and streams the block
//                   (split-at-write) into content.bin.
// The walk NEVER touches GfxRenderer; all pixel/position resolution is the sink's
// job. Block boundaries here are semantic (paragraph), not page-fit: the >96-word
// mid-flush split and the 8 KB serialization split live inside the respective
// sinks, not in the seam.
//
// This is the PUSH side (live walk → sink). A PULL side (IBlockSource: random-access
// over a persisted content.bin, so Stage-2 reads records without the live walk and
// without holding the whole book in RAM) is DESIGNED but NOT YET IMPLEMENTED — see
// docs/stage1-reassessment-2026-07-26.md, which makes it a prerequisite for the
// device wiring.

#include <cstdint>
#include <string>

#include "CompiledContent.h"  // compiled::Block
#include "Epub/css/CssStyle.h"

struct FootnoteEntry;  // ../FootnoteEntry.h — reference-only here

namespace compiled {

// A block-at-a-time consumer of the walk. Tables and lists are synthesized inside
// the walk core and replayed as ordinary onBlock() calls, so a sink only ever sees
// resolved blocks, never <table>/<li>.
struct BlockSink {
  virtual ~BlockSink() = default;

  // One complete block (text runs, or image ref) plus the block's resolved CssStyle
  // (em/%, logical align — pre-px, settings-independent, captured at block START before
  // em->px resolution; NOT the finished ParsedText's px BlockStyle). Block::styleId is
  // left unset and is the sink's to resolve: ContentSink interns `style` into its pool
  // and stamps styleId; LayoutSink ignores styleId and builds a px BlockStyle from
  // `style` directly. Ownership of `block` moves to the sink.
  virtual void onBlock(Block&& block, const CssStyle& style) = 0;

  // An element id at the current block/char position. LayoutSink resolves it to a
  // page (anchorData); ContentSink records id -> (blockIndex, charOffsetInBlock).
  virtual void onAnchor(const std::string& id) = 0;

  // A heading / chapter boundary (level 1..6; 0 = non-heading boundary).
  virtual void onChapter(uint8_t level, const std::string& title) = 0;

  // A printed-page label (EPUB pagebreak marker / NCX pageList) at the current position.
  virtual void onPageBreakLabel(const std::string& label) = 0;

  // A footnote link anchored to a word in the block currently being built.
  virtual void onFootnote(int wordIndex, const FootnoteEntry& entry) = 0;

  // The walk's current XPath counters (1-based <p> sibling index, running <li> count, and the
  // byte offset of the most recent body-child element start), as of the block about to be emitted.
  // LayoutSink records these into the per-page LUT its emitPage builds; ContentSink ignores them.
  // Default no-op so non-layout sinks need not implement it. Settings-independent (pure XML), but
  // the LUT that maps them to PAGES is settings-dependent, so it stays a sink concern.
  virtual void onXPathAdvance(uint16_t /*paragraphIndex*/, uint16_t /*listItemIndex*/,
                              uint32_t /*bodyChildByteOffset*/) {}

  // End of the spine document: flush any block still being accumulated.
  virtual void onSpineEnd() = 0;
};

}  // namespace compiled
