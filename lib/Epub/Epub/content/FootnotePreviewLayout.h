#pragma once
// Inline footnote-preview abbreviation — the Stage-2 (settings-dependent) step that trims each inline
// footnote preview run in a Block to the viewport width. Stage-1 stores the FULL preview text
// (settings-independent); the width-based abbreviation (viewport*2 px budget, trailing ellipsis) is
// font/viewport-dependent, so it happens at layout time. Shared by the streaming LayoutSink and the
// pull core (PageLayout) so both produce identical previews — do not duplicate the paren/ellipsis
// logic. See [[stage1-footnote-preview-settings-split]].

#include <cstdint>

class GfxRenderer;

namespace compiled {

struct Block;

// Abbreviate each inline footnote preview run in `block` to the viewport width, IN PLACE. For each
// run, keep whole note words within a viewport*2 px budget and append an ellipsis when some were
// dropped. Measures BARE note words (parens stripped) so the budget is computed on the note text
// itself, then re-fuses the parens onto the kept boundary words. No-op when the block has no preview
// runs. Clears block.footnotePreviews afterward (runs no longer need marking once abbreviated).
void abbreviateFootnotePreviews(Block& block, GfxRenderer& renderer, int fontId, uint16_t viewportWidth);

}  // namespace compiled
