#pragma once
// LaidOutBlock — the pull core's per-block cache slot (P1, text-only), microreader's LaidOutParagraph
// analog (docs/pull-core-plan-microreader-guided-2026-07-27.md §1). A logical Text block is laid out
// ONCE into its page-INDEPENDENT line set (via ParsedText::layoutAndExtractLines, the same call
// LayoutSink makes), plus the settings-resolved BlockStyle and the vertical-spacing scalars the
// forward collect loop needs to reproduce LayoutSink's Y math (makePages/addLineToPage) exactly. A
// page that ends on this block AND the page that starts on it both reuse this slot — the property
// that makes live one-page-from-cursor layout cheap.
//
// P1 is TEXT-ONLY: a spine with any Image/Hr/Table block falls back to the scaffold LayoutSink path,
// so LaidOutBlock models only Text (empty <br>/wrapper blocks included, which contribute spacing but
// no lines). Images/Hr/tables are P2-P4.

#include <cstdint>
#include <memory>
#include <vector>

#include "Epub/blocks/BlockStyle.h"

class TextBlock;
class ParsedText;

namespace compiled {

// One text logical block, laid out to its page-independent lines. `lines` are the exact
// std::shared_ptr<TextBlock> objects ParsedText emits (per-word xpos already baked), so a placed
// PageLine is byte-identical to LayoutSink's. Empty (<br>/wrapper) blocks have no lines but still
// carry a merged style whose margins/padding fold into the following block — reproduced by the
// preprocessor that fills these slots, so the collect loop only ever sees final styles.
struct LaidOutBlock {
  // The resolved, post-merge block style actually laid out (headings folded, empty-block merge
  // already applied by the preprocessor). resolveBlockFont has NOT been run yet — the collect loop
  // runs it (seeded from the carried auxFontId) exactly where makePages does.
  BlockStyle style;
  std::vector<std::shared_ptr<TextBlock>> lines;

  bool isEmptyBlock = false;      // <br>/empty wrapper: no lines, spacing-only (folded into next)
  bool isContinuation = false;    // kContinuation split record: makePages skips top-margin/fold
  bool preformatted = false;      // inside <pre>: suppress extra-paragraph spacing
  bool pageBreakBefore = false;   // kPageBreakBefore: forces a fresh page when the page has content
  bool flushedMidBlock = false;   // >96-word mid-block flush fired: LayoutSink drops this block's top
                                  // spacing (flush bypasses makePages' margin; tail is a continuation)

  // Word count per emitted line (prefix-summable), for reproducing addLineToPage's footnote
  // assignment (a footnote lands on the page whose lines have covered its anchor word index).
  std::vector<uint16_t> lineWordCounts;
};

}  // namespace compiled
