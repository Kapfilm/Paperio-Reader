# Step 5 design — LayoutSink: route measure+paginate through BlockSink

The golden-risk step of the Stage-1 extraction (design in `stage1-extraction-design.md`, master
plan `compiled-book-pipeline-plan.md` Phase 3). Steps 1–4 are **done and host-verified** (see
`parser-stage1-step4-handover.md`): the producer emits a materialized `compiled::Block` stream
through `BlockSink`, with the fused layout untouched, and `ContentSink` consumes it into
`content.bin`. Step 5 builds the **other** consumer — `LayoutSink` — that reproduces today's
measure+paginate byte-for-byte from the same `onBlock(block, style)` stream.

Branch: `feat-stage1-extraction`. This doc is the plan to sign off **before** touching the parser.

## The problem the map surfaced (why the naive order is unsafe)

The agent map of `ChapterHtmlSlimParser.cpp` (3,085 lines) confirmed design finding #3, concretely:
layout and walk are **interleaved inside the same methods**, not cleanly separable by moving whole
functions:

- **`startNewTextBlock` [cpp:741-824]** is the crux. In one method it does walk work
  (`nextWordContinues`, `currentCssStyle`, the `stage1*` tap, `pendingAnchorId` bookkeeping) AND
  layout work (`makePages` [cpp:798], `emitPage` [cpp:775,805], `renderer.getLineHeight` for the
  `<br>` gap [cpp:762], `attachPendingFloatImage`, `currentTextBlock.reset(new ParsedText(...))`).
  It also **records `anchorData` as a page number** inline [cpp:779,811] — the exact anchor→page /
  anchor→block-position coupling finding #3 flagged.
- **The >96-word split** lives inside `flushPartWordBuffer` [cpp:407-459] — pure page-fitting layout
  (`resolveBlockFont`, float-zone injection, `layoutAndExtractLines`, `addLineToPage`) sitting inside
  a pure-walk method.
- **Five walk-side `renderer` dereferences** [cpp:762, 1200, 1562, 1694, 2896] do em→px sizing during
  the walk. `BlockSink.h` says "the walk core NEVER touches GfxRenderer" — today it does, in five
  places. These are step-6's problem to fully sever, but step 5 must not depend on them being gone.

**Consequence for step 5's scope.** We do NOT unify yet (that is step 6). Step 5 builds `LayoutSink`
as a **second, parallel consumer** that reproduces layout output, with the fused path **left intact
and shipping**. This isolates blast radius: if `LayoutSink` diverges, the diff is caught by the
equivalence test, and nothing on the device path has changed. The unify (delete the fused inline
layout, make the parser drive *only* sinks, extract `HtmlWalkCore`) is step 6, gated by this step's
equivalence proof.

## What LayoutSink is

`lib/Epub/Epub/content/LayoutSink.{h,cpp}` — a `compiled::BlockSink` that, given the walk's
`onBlock(Block&&, const CssStyle&)` stream, produces the identical sequence of `Page` objects the
fused path produces via `completePageFn`.

```cpp
class LayoutSink : public compiled::BlockSink {
 public:
  LayoutSink(GfxRenderer& renderer, const LayoutParams& params,
             const std::function<void(std::unique_ptr<Page>)>& completePageFn);

  void onBlock(compiled::Block&& block, const CssStyle& style) override;
  void onAnchor(const std::string& id) override;          // -> anchorData (id, page)
  void onChapter(uint8_t level, const std::string& title) override;  // -> TOC page break
  void onPageBreakLabel(const std::string& label) override;          // -> pageBreakLabels
  void onFootnote(int wordIndex, const FootnoteEntry& entry) override;
  void onSpineEnd() override;                             // flush last block + final emitPage

  // Side outputs the caller pulls today via the parser getters:
  const std::vector<std::pair<std::string,uint16_t>>& anchors() const;
  const std::vector<std::pair<uint16_t,std::string>>& pageBreakLabels() const;
  const std::vector<ParagraphLutEntry>& paragraphLutPerPage() const;
};
```

`LayoutParams` bundles the settings the fused parser holds as ctor args: `fontId`, `lineCompression`,
`extraParagraphSpacing`, `paragraphAlignment`, `viewportWidth`, `viewportHeight`, `hyphenationEnabled`,
`bionicReadingEnabled`, plus `fontSizeLadder_`. `renderer` is held by reference. This is exactly the
settings-dependent state the map lists (§3 of the map) — no walk state crosses into the sink.

## State that lives in LayoutSink (from the map, §3)

Moved (copied for step 5) into the sink, verbatim types:
`currentTextBlock` (`unique_ptr<ParsedText>`), `currentPage` (`unique_ptr<Page>`), `currentPageNextY`,
`lastBlockMarginBottom`, `pendingInlineImage_`, `deferredPageImage_`, the four `activeFloat*`,
`auxFontId_`, `completedPageCount`, `anchorData`, `pendingAnchorId`, `pageBreakLabels`,
`paragraphLutPerPage`, `wordsExtractedInBlock`, `pendingFootnotes`, `layoutFailed`, and the
`renderer` reference. The layout methods move with them: `resolveBlockFont`, `effectiveFontId`,
`effectiveLineHeight`, `makePages`, `addLineToPage`, `emitPage`, `attachPendingFloatImage`,
`ensureHeapForTextLayout`, `placeImageBlockAsBlock`, and the image/table cell builders they call.

## The hard part: reconstructing block boundaries from the stream

The fused path's `startNewTextBlock` decides block boundaries AS IT WALKS. `LayoutSink` receives
**already-materialized blocks** — so it must reconstruct the same boundary decisions from the block
stream. The producer already encodes what the sink needs:

1. **Empty wrapper / `<br>` blocks.** The producer emits them as a 1:1 transcript (per `stage1FlushBlock`
   comment and `BlockFlags`). `LayoutSink::onBlock` replays the same empty-block merge (`getCombinedBlockStyle`,
   the `<br>` `marginTop += lineHeight` injection) the fused `startNewTextBlock` does [cpp:752-796].
   The `<br>`-gap `renderer.getLineHeight(fontId)*lineCompression` [cpp:762] happens **inside the sink**
   now — legitimately, since the sink owns `renderer`.
2. **The block's px BlockStyle.** `onBlock` carries the pre-px `CssStyle`. The sink builds a `BlockStyle`
   from it the same way the walk does before `startNewTextBlock` (the em→px step). This is the one place
   we must mirror the walk's `currentCssStyle` → `BlockStyle` construction exactly; it is the highest-risk
   line-up point and gets its own focused test.
3. **Anchors → page.** `onAnchor` stashes the id; the next `onBlock` records `(id, completedPageCount)`
   into `anchorData` — reproducing [cpp:779,811], including the TOC-boundary forced page break
   [cpp:772-777, 802-807] driven by `onChapter`/`tocAnchors`.
4. **The >96-word split** [cpp:407-459] moves wholesale into `LayoutSink`. But note: the sink receives
   the WHOLE block at once (the producer accumulates all words), so the sink does its own measure and
   mid-block split at `makePages` time — it does NOT need the walk's incremental 96-word trigger. The
   split logic (float-zone re-anchoring, `includeLastLine=false`) becomes an internal `makePages`
   concern. **This is a behavioral risk:** the fused path splits at word 97 mid-accumulation; the sink
   splits when it lays out the full block. The *output pages* must still be identical because
   `layoutAndExtractLines` is deterministic on the same words+width+height — but this is the #1 thing
   the equivalence test must prove, per-book, across the settings matrix.

## Verification — the gate

The fused page dump already exists: `PipelineRunner::runAndDump` builds via `createSectionFile` →
`loadPageFromSectionFile` and prints canonical `PAGE/LINE/IMG/TABLE/HR/FN` records. Step 5 adds a
**parallel driver** that runs the parser with a `LayoutSink` attached (instead of, or alongside, the
fused layout) and dumps its `Page` stream in the SAME format. The test asserts the two dumps are
**byte-identical**, per book, across the settings matrix (the same `Profile` set the goldens use).

Two sub-checks, most-specific first:
- **`LayoutSink.BlockStyleMatchesFused`** — a focused unit test that feeds one hand-built block+CssStyle
  and asserts the sink's `BlockStyle` (px) equals what the walk produces. Catches the em→px line-up
  (risk #2) in isolation before the full-page diff muddies it.
- **`LayoutSink.PageDumpMatchesFused`** — parametrized over the synthetic corpus × settings matrix;
  the full byte-identical gate.

Driver wiring: add `layoutViaSink(...)` to `PipelineRunner` mirroring `compileContent(...)`, attaching
a `LayoutSink` whose `completePageFn` collects pages into a vector the harness dumps. Gated by a new
`EPUB_STAGE1_LAYOUT` (or reuse `EPUB_STAGE1`) on the test target only — the sink compiles unconditionally
but nothing on the device constructs it, exactly like `ContentSink`.

## Commit sequence (each host-green)

1. **LayoutSink skeleton + LayoutParams** — the `BlockSink` subclass with the moved state members and
   method stubs; `resolveBlockFont`/`effectiveFontId`/`effectiveLineHeight` copied (they're small and
   `renderer`-local). Compiles, does nothing yet. + `BlockStyleMatchesFused` unit test.
2. **onBlock text path** — empty-block merge, `<br>` gap, `makePages`, `addLineToPage`, `emitPage`,
   the 96-word/mid-block split. Text-only corpus books byte-identical.
3. **Images + floats** — `attachPendingFloatImage`, `deferredPageImage_`, active-float propagation,
   `placeImageBlockAsBlock`. Image/float corpus books byte-identical.
4. **Tables** — cell image builders, `emitTableAsFragments`/`Paragraphs`. Table corpus byte-identical.
5. **Anchors / chapters / labels / footnotes** — the side-output tables; assert getter equality vs the
   fused parser (anchor set, label set, LUT).
6. **Full matrix gate green** — `PageDumpMatchesFused` across corpus × settings; wire the doc/handover.

Steps 1–6 here are all ADDITIVE — the fused path is untouched and keeps shipping. Only **step 6 of the
outer sequence** (the next doc) removes the fused inline layout and extracts `HtmlWalkCore`, gated by
this equivalence proof.

## Explicitly NOT in step 5

- No removal of the fused layout, no `HtmlWalkCore` extraction (that's outer-step 6).
- No change to the device path, `Section`, WBC1, or goldens. `LayoutSink` is inert on device.
- The five walk-side `renderer` dereferences [cpp:1200,1562,1694,2896 + the 762 one moves into the sink]
  are NOT severed here; they stay in the walk. Severing the em→px walk uses is outer-step 6's work.
- `content.bin` is not read back for layout yet (the pull `IBlockSource` is a later Stage-2-flip step).

## Open risks to watch (ranked)

1. **96-word split vs whole-block split** producing a different page break in some edge case — the #1
   equivalence risk. Mitigation: the corpus already has `test_kerning_ligature` and long paragraphs;
   add a synthetic >200-word multi-page paragraph book if the corpus doesn't force a mid-block break.
2. **em→px BlockStyle construction** lining up (risk #2). Mitigation: the focused unit test.
3. **`auxFontId_` one-per-section budget** ordering — the fused path claims the aux slot in walk order;
   the sink must claim it in the same block order. Since `onBlock` order == walk order, this should
   hold, but assert it (a book with two distinct off-body fonts).
4. **Float zone re-anchoring** [cpp:2683-2699] depends on `currentPageNextY` at layout time; if the
   sink's page-Y evolves differently the image lands one line off. Covered by the image/float dump diff.
