#pragma once
// Compiled content format (content.bin, magic "WBC1") — Phase 3 of
// docs/compiled-book-pipeline-plan.md; layout defined in docs/compiled-content-format.md.
//
// This is the SETTINGS-INDEPENDENT product of a one-time Stage-1 compile: parsed,
// CSS-resolved blocks with text runs and image refs, keyed only by the book's ZIP
// content fingerprint. Stage-2 pagination (word measurement + line/page breaking)
// reads this and produces the per-settings section caches, so a font/margin change
// never re-runs ZIP/XML/CSS.
//
// This header + CompiledContent.cpp are sub-step 2a: the in-memory model and the
// content.bin writer/reader with a host round-trip test. The rendererless Stage-1
// pass that *fills* this model (from ChapterHtmlSlimParser) and the anchor/chapter/
// char-offset tables land in later sub-steps; header flags are reserved for them.
// Nothing here is wired into a build yet (guarded by EPUB_STAGE1 at the call sites).

#include <HalStorage.h>  // FsFile

#include <cstdint>
#include <string>
#include <vector>

#include "Epub/css/CssStyle.h"

namespace compiled {

inline constexpr char kMagic[4] = {'W', 'B', 'C', '1'};
// v2 added Block::footnotePreviews (inline footnote preview runs; abbreviation deferred to Stage-2).
inline constexpr uint8_t kVersion = 2;

// Word::styleSpan bits. Bits 0-6 = inline font style (the EpdFontFamily::Style set,
// remapped to a stable on-disk layout); bit 7 = word attaches to the previous word
// with no space before it (inline-element boundary, e.g. <b>foo</b>bar). All
// layout-independent, so they live in Stage-1.
enum WordStyleSpan : uint8_t {
  kSpanBold = 1 << 0,
  kSpanItalic = 1 << 1,
  kSpanUnderline = 1 << 2,
  kSpanStrikethrough = 1 << 3,
  kSpanSuper = 1 << 4,
  kSpanSub = 1 << 5,
  kSpanSmallCaps = 1 << 6,
  kSpanAttachPrev = 1 << 7,  // no leading space (ParsedText wordContinues)
};

// Per-word settings-independent data — the slice of today's TextBlock that survives
// a relayout. No xpos: Stage-2 measurement computes it. bidiLevel is 0 for LTR books
// and reserved for RTL (see docs/compiled-content-format.md "RTL / BiDi").
struct Word {
  uint32_t textOff = 0;   // byte offset of the word's text within Block::text
  uint8_t styleSpan = 0;  // WordStyleSpan bitmask (style + attach-to-previous)
  uint8_t sizePct = 100;  // per-word font-size percent; 100 = inherit block size
  uint8_t bidiLevel = 0;  // Unicode embedding level; 0 = LTR
};

enum class BlockType : uint8_t { Text = 0, Image = 1, Table = 2, Hr = 3 };

// A run of words inside a Text block that make up an inline footnote preview ("(note text)").
// The full preview text is stored settings-independently; Stage-2 abbreviates the run to the
// viewport width at layout time (the abbreviation is font/viewport-dependent, so it must NOT be
// baked into content.bin). startWord indexes into Block::words; count words follow.
struct PreviewRun {
  uint32_t startWord = 0;
  uint32_t count = 0;
};

// One table cell: text runs (a mini text block) and/or a single cell image. Settings-
// independent — Stage-2 reproduces today's grid-or-paragraph decision (which is
// font-dependent) from this structure, so grid tables survive relayout unchanged.
struct TableCell {
  std::vector<Word> words;
  std::string text;            // words back-to-back, each NUL-terminated
  std::string imageEntryPath;  // optional cell image (empty = none)
  int16_t imageWidth = 0;      // intrinsic dims, pre-probed at compile
  int16_t imageHeight = 0;
  std::string imageAlt;
  bool isHeader = false;  // <th> cell
  uint8_t colSpan = 1;
};

struct TableRow {
  std::vector<TableCell> cells;
  bool isHeaderRow = false;  // all cells are <th>
};

// Block::flags bits (docs/compiled-content-format.md).
enum BlockFlags : uint8_t {
  kStartsChapter = 1 << 0,
  kPageBreakBefore = 1 << 1,
  kPageBreakAfter = 1 << 2,
  // bits 3-4: base direction (0 auto, 1 LTR, 2 RTL) — reserved for RTL.
  kDirectionShift = 3,
  kDirectionMask = 0b11 << 3,
  // This (empty) block came from a <br> section separator: Stage-2 injects a blank
  // line's worth of top margin when merging it into the following paragraph, exactly
  // as the fused parser's empty-block-reuse path does.
  kFromBrElement = 1 << 5,
  // Continuation of the previous TEXT block, produced by the writer's 8 KB
  // split-at-write; Stage-2 treats the run as one logical paragraph.
  kContinuation = 1 << 6,
  // Block is inside a <pre> element: Stage-2 suppresses the extra inter-paragraph spacing
  // (the fused path gates it on preUntilDepth), so preformatted lines are single-spaced.
  kPreformatted = 1 << 7,
};

// One content block. Text fields are used when type==Text, image fields when
// type==Image; the unused set stays empty/zero.
struct Block {
  BlockType type = BlockType::Text;
  uint16_t styleId = 0;  // index into CompiledContent::stylePool
  uint8_t flags = 0;
  uint32_t charOffset = 0;  // absolute char offset of this block's first char (reading progress)

  // Text block:
  std::vector<Word> words;
  std::string text;  // words back-to-back, each NUL-terminated
  // Inline footnote preview runs within this block (empty for the vast majority of blocks).
  // The words themselves live in `words`/`text`; these ranges tell Stage-2 which runs to
  // abbreviate to the viewport at layout time. See PreviewRun.
  std::vector<PreviewRun> footnotePreviews;
  // Optional inline (float) image rendered beside this paragraph's text. Stage-2 places
  // it; Stage-1 just records the ref (empty entryPath = none). intrinsic dims, pre-probed.
  std::string inlineImageEntryPath;
  int16_t inlineImageWidth = 0;
  int16_t inlineImageHeight = 0;
  uint8_t inlineImageSide = 0;  // 1 left / 2 right (0 = none)
  std::string inlineImageAlt;

  // Image block:
  std::string entryPath;  // EPUB-internal path (e.g. OEBPS/images/x.jpg)
  int16_t width = 0;      // intrinsic dimensions, pre-probed at compile
  int16_t height = 0;
  uint8_t floatSide = 0;  // 0 none / 1 left / 2 right
  std::string alt;

  // Table block:
  std::vector<TableRow> rows;
  bool hasBorder = true;  // border="0" clears it (affects Stage-2 grid rendering)
};

// Named position for anchor navigation (TOC targets, in-book links). Resolves to a
// (block, char-offset) pair; Stage-2 maps that to a page. Id stored as a string
// (not a hash) so a lookup can never resolve the wrong target.
struct Anchor {
  std::string id;           // element id / fragment
  uint32_t blockIndex = 0;  // block within this spine
  uint32_t charOffsetInBlock = 0;
};

// Book-level chapter/heading entry (drives the TOC and heading navigation).
struct Chapter {
  uint16_t spineIndex = 0;
  uint32_t blockIndex = 0;
  uint8_t level = 0;  // heading level 1..6; 0 = non-heading chapter boundary
  std::string title;
};

// Per-spine content, in document order.
struct SpineContent {
  std::vector<Block> blocks;
  std::vector<Anchor> anchors;
  uint32_t firstCharOffset = 0;  // absolute char offset of the spine's first char (progress)
};

// A whole book's compiled content.
struct CompiledContent {
  std::vector<CssStyle> stylePool;  // deduped block styles; blocks reference by index
  std::vector<SpineContent> spines;
  std::vector<Chapter> chapters;
};

// Whether two block styles are identical for pooling purposes (all rendering-relevant
// fields + the explicit-set flags). Two blocks that resolve to equal styles share a pool id.
bool styleEquals(const CssStyle& a, const CssStyle& b);

// Return the pool id for `style`, appending it to `content.stylePool` if not already
// present (dedup by value). Linear scan — the distinct-style set per book is small
// (tens), and blocks vastly outnumber styles.
uint16_t internStyle(CompiledContent& content, const CssStyle& style);

// Serialize/deserialize the WBC1 container. Return false on I/O error or a
// version/magic mismatch (caller treats a mismatch like a stale cache: recompile).
bool writeContentBin(FsFile& out, const CompiledContent& content);
bool readContentBin(FsFile& in, CompiledContent& content);

}  // namespace compiled
