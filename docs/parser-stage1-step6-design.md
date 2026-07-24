# Step 6 design — unify: parser drives only sinks, extract HtmlWalkCore

The final step of the Stage-1 extraction (design `stage1-extraction-design.md`, master plan
`compiled-book-pipeline-plan.md` Phase 3). Step 5 is **done**: `LayoutSink` reproduces the fused
measure+paginate byte-for-byte across the full corpus × 7-profile matrix. Step 6 removes the fused
inline layout so the parser drives **only** the sink, then splits the state machine into
`HtmlWalkCore` (settings-independent walk) + the `LayoutSink` glue. The parser's public Stage-2
contract to `Section` (setup/write/finalize/completePageFn/getAnchors/getPageBreakLabels/
getParagraphLutPerPage) stays byte-identical — only the internals change.

Branch `feat-stage1-extraction`. This is the plan to sign off before touching the parser.

## The current structure (from the step-6 map)

Today the walk drives the inline layout AND the `stage1Sink_` producer **in lockstep on one
path** — every dual-drive site runs both halves back-to-back. On the shipping device `stage1Sink_`
is null (producer inert); the inline layout does all the work. The step-5 equivalence test attaches
a `LayoutSink` as an *additional* `stage1Sink_` and diffs its pages vs the fused goldens.

Step 6 flips this: the parser **always** owns a `LayoutSink` internally, wired to its
`completePageFn`; the fused inline-layout half is deleted; the producer half becomes the parser's
only output driver. `Section` sees no change.

## Prerequisite: prove the SINK'S GETTERS match (not just the page dump)

The step-5 gate diffs the page *dump*, which does NOT include the three side-output tables
(`anchorData`, `pageBreakLabels`, `paragraphLutPerPage`). The map found a real gap: the sink's
**XPath LUT is never fed** — `xpathParagraphIndex_`/`xpathListItemIndex_`/`lastBodyChildByteOffset_`
are declared but unwired, and `LayoutLutEntry` differs in type from the parser's `ParagraphLutEntry`.
Section's serialization has a hard `paragraphLut.size() == pageCount` check.

**So step 6a (first commit) is a verification + plumbing step, BEFORE any deletion:**
- Extend the equivalence harness to also compare the parser's getters against the LayoutSink's
  getters (anchor set, label set, LUT entries) — a new assertion in `layoutViaSink`/the test.
- Wire the XPath indices into `LayoutSink`: the walk increments them (parser cpp:1424-1436), the
  sink's `emitPage` consumes them. Route them through the sink — either a new `BlockSink` hook
  (`onXPathAdvance`/carried on `onBlock`) or by having the producer stamp per-block xpath counts.
  Decide the exact mechanism during 6a (a block-carried triple is least invasive).
- Adapt `LayoutLutEntry` ↔ `ParagraphLutEntry` (field-compatible; a proxy or a shared type).
- Land 6a with the sink STILL a parallel consumer — getters proven equal, nothing deleted.

## Incremental sequence (each equivalence-green, fused path intact until the flip)

**6a — getter equivalence + XPath LUT plumbing.** Above. The sink now reproduces pages AND all
three getter tables. Fused path untouched. Gate: page dump + getter equality across the matrix.

**6b — parser owns an internal LayoutSink; route output through it (fused layout STILL runs,
but its output is discarded / asserted-equal).** Construct a `LayoutSink` inside the parser from
its own members (all the LayoutParams are already parser fields + ctor args). Point the internal
sink's `completePageFn` at the parser's `completePageFn`; make the getters proxy the sink. The
fused inline layout still runs but its `completePageFn`/getters are bypassed. Verify the SHIPPING
goldens (device path, `stage1Sink_` null) still pass — now served by the internal sink, not the
inline layout. This is the moment the device path switches to the sink; it must be byte-identical.

**6c — delete the fused inline-layout half.** With the internal sink proven as the output driver,
delete the inline-layout lines at every dual-drive site (the map lists them exactly): `makePages`,
`addLineToPage`, `emitPage`, `resolveBlockFont`, `effectiveLineHeight`, `startNewTextBlock`'s
layout half, `attachPendingFloatImage`, `placeImageBlockAsBlock`, `buildCellImage`, the
`emitTableAs*` family, the >96-word split, the `<img>`/`<hr>` placement, and the layout state
members (currentTextBlock, currentPage, float state, anchorData, LUT, pendingFootnotes, etc.).
Keep the producer half. The parser is now walk + producer + owned LayoutSink.

**6d — sever the walk-side renderer em→px uses.** With no inline layout consuming px BlockStyle,
the walk no longer needs to resolve em→px: it produces `CssStyle` (pre-px) for the producer, and
the sink does em→px. Sever cpp:786 (`<br>` gap), 1234 (`<img>` emSize), 1528 (`BlockStyle::
fromCssStyle` emSize). The `<hr>`/`effectiveLineHeight`/table renderer uses left with the deleted
layout in 6c. **Residual:** the footnote-preview `abbreviateInlineFootnote` (cpp:164,174) still
measures text — a walk-side renderer use that is NOT layout. Either move preview abbreviation to
Stage-2 or keep a renderer solely for it; flag and decide in 6d (likely keep it for now, noted as
the one remaining walk renderer dependency — a fully renderer-free walk is a follow-up).

**6e — split into HtmlWalkCore + glue; rename.** Extract the settings-independent walk state +
methods into `HtmlWalkCore`; `ChapterHtmlSlimParser` becomes the thin Stage-2 entry that owns an
`HtmlWalkCore` + a `LayoutSink`. Public contract unchanged. This is the cosmetic/structural close;
if it proves large, it can be its own follow-up after 6c/6d deliver the functional unify.

## The gate

Every commit stays green on: the full page-dump equivalence matrix (step-5 gate) PLUS the new
getter-equality assertions (6a). 6b additionally re-verifies the **shipping goldens** (device path)
since that path switches from inline layout to the internal sink. Full real suite (426 tests) green
throughout; `content.bin`/ContentSink unaffected (producer half is untouched by the deletion).

## Risks (ranked)

1. **XPath LUT plumbing** (6a) — the one real gap; the LUT drives KOReader XPath→page and has a
   hard size check in Section. Must be wired and getter-verified before the flip.
2. **6b device-path switch** — the shipping path moves from inline layout to the sink. The step-5
   matrix proves the SINK equals the fused layout, so this should be safe, but 6b is where a
   divergence would hit real books; keep the shipping goldens as the gate.
3. **Deletion scope** (6c) — the dual-drive sites interleave halves; deleting the wrong line breaks
   the walk. The map lists exact line ranges per site; delete conservatively, one site per commit.
4. **Footnote-preview renderer residue** (6d) — may prevent a fully renderer-free walk; acceptable
   to leave as a documented follow-up rather than block step 6.

## Not in step 6

Stage-2-reads-content.bin (the pull `IBlockSource`), footnote/image-manifest folding, `html_<spine>
.bin` retirement — the back half of Phase 3, after the unify.
