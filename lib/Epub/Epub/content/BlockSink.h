#pragma once
// Coarse push seam between the settings-independent XHTML walk and its consumers
// (Phase 3 step 2c; design in docs/stage1-extraction-design.md). One handoff per
// SEMANTIC block — a fully-built compiled::Block — mirroring microreader's
// whole-Paragraph ParagraphSink. Two implementations:
//   - LayoutSink   (unflagged): onBlock() runs resolveBlockFont + makePages, i.e.
//                  today's measure+paginate, byte-identical to the fused path.
//   - ContentSink  (behind EPUB_STAGE1): onBlock() interns the style and streams the
//                  block (split-at-write) into content.bin.
// The walk core NEVER touches GfxRenderer; all pixel/position resolution is the
// sink's job. Block boundaries here are semantic (paragraph), not page-fit: the
// >96-word mid-flush split and the 8 KB serialization split live inside the
// respective sinks, not in the seam.
//
// This is the push side. The pull side (IBlockSource, random-access with a sliding-
// window cache over content.bin) lands with the later Stage-2-reads-content.bin
// step; see docs/stage1-extraction-design.md "Pull seam".

#include <cstdint>
#include <string>

#include "CompiledContent.h"  // compiled::Block

struct FootnoteEntry;  // ../FootnoteEntry.h — reference-only here

namespace compiled {

// A block-at-a-time consumer of the walk. Tables and lists are synthesized inside
// the walk core and replayed as ordinary onBlock() calls, so a sink only ever sees
// resolved blocks, never <table>/<li>.
struct BlockSink {
  virtual ~BlockSink() = default;

  // One complete block (text runs + interned-later style, or image ref). Ownership
  // is moved to the sink.
  virtual void onBlock(Block&& block) = 0;

  // An element id at the current block/char position. LayoutSink resolves it to a
  // page (anchorData); ContentSink records id -> (blockIndex, charOffsetInBlock).
  virtual void onAnchor(const std::string& id) = 0;

  // A heading / chapter boundary (level 1..6; 0 = non-heading boundary).
  virtual void onChapter(uint8_t level, const std::string& title) = 0;

  // A printed-page label (EPUB pagebreak marker / NCX pageList) at the current position.
  virtual void onPageBreakLabel(const std::string& label) = 0;

  // A footnote link anchored to a word in the block currently being built.
  virtual void onFootnote(int wordIndex, const FootnoteEntry& entry) = 0;

  // End of the spine document: flush any block still being accumulated.
  virtual void onSpineEnd() = 0;
};

}  // namespace compiled
