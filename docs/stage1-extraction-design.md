# Stage-1 extraction design — shared yxml-walk core, coarse block seam

Phase 3 step 2c of `compiled-book-pipeline-plan.md`. Companion to
`parser-stage1-handover.md` (mission) and `compiled-content-format.md` (WBC1 output).
This is the **contract** for the chosen approach — *extract a shared walk core* — written
before any parser surgery, because the gate is **byte-identical goldens** and the state
machine is 2,862 lines.

Seam shape validated against **microreader** (`CidVonHighwind/microreader`,
`content/{ContentModel,EpubParser,IParagraphSource,TextLayout}.h`, `content/mrb/`), the only
studied reader with a real settings-independent compile. Its lessons, adopted below:

- The seam unit is a **whole materialized paragraph/block**, not per-word/per-tag events.
  microreader's `ParagraphSink` emits a complete `Paragraph` at a time; we already have that
  unit as `compiled::Block`. → **coarse `BlockSink`, one handoff per block.**
- Producer and consumer are a **symmetric push/pull pair** with the serialized file between:
  push `ParagraphSink` on the write side, pull `IParagraphSource` (random access) on the
  layout side. The on-disk source (`MrbChapterSource`) loads on demand with a **32-item
  sliding-window cache**, so relayout never holds a whole chapter in RAM.
- **Split-at-write** (microreader `kMaxSerializedBody = 8192`) bounds read-time allocs — the
  same 8 KB record cap our format spec already mandates. It lives in the ContentSink, not the
  seam.

## Decision

`ChapterHtmlSlimParser` fuses a settings-independent **walk** with settings-dependent
**layout**. Split it so the walk emits a **materialized `compiled::Block`** through a coarse
push sink; today's layout and the new content writer are two implementations of that sink:

```
                       ┌───────────────────────────┐
  XHTML ─▶ yxml ─▶     │  HtmlWalkCore             │ ─▶ BlockSink::onBlock(compiled::Block&&)
                       │  depth/skip, inline-style │        ├─ LayoutSink  → measure+paginate → pages (Stage 2, today)
                       │  stack, CSS resolve, tag  │        └─ ContentSink → split-at-write   → content.bin (Stage 1)
                       │  dispatch, word segment,  │
                       │  tables, lists, anchors,  │   ── later: Stage-2 pulls blocks back via
                       │  chapters, charOffset      │       IBlockSource (sliding-window cache)
                       └───────────────────────────┘
```

The walk core builds each block fully (words + interned `CssStyle`, em/%, logical align —
*exactly* Stage-1's block style), then hands it off in one `onBlock` call. It never touches
`GfxRenderer`. One handoff per block, not a per-word event stream — lower golden-risk and
`compiled::Block` is reused verbatim as the seam type.

## The seam: what stays in the core vs. moves to the sink

**Walk core (settings-independent — the shared part).** Members from the header that are
pure walk state: `depth`, `skipUntilDepth`, `skipTextUntilDepth`, the `*UntilDepth` inline
flags, `svgDepth`, `partWordBuffer`/`partWordBufferIndex`/`nextWordContinues`,
`inlineStyleStack` + all `effective*` inline fields, `currentCssStyle`, `currentTable`/
`currentTableCell` (table buffering is semantic), `listStack`, `pendingAnchorId`/
`tocAnchors`, xpath indices (`xpathParagraphIndex`, `xpathListItemIndex`, `xpathBodyDepth`,
`lastBodyChildByteOffset`), footnote-link tracking, `cssStyleCache_`/`inlineStyleCache_`,
`saxParser_`, the streaming fields, and `contentBase`/`imageBasePath`/`imageCounter`
(image *identity*, not sizing).

**Layout sink (settings-dependent — Stage 2 only).** `currentTextBlock` (`ParsedText`),
`currentPage`, `currentPageNextY`, `lastBlockMarginBottom`, all `activeFloat*` +
`deferredPageImage_` zone state, `resolveBlockFont`/`effectiveFontId`/`effectiveLineHeight`
(em→px), `makePages`, `addLineToPage`, `emitPage`, `paragraphLutPerPage`,
`completedPageCount`, `anchorData` (id→**page**), `pageBreakLabels`, and the `renderer`
reference. These reproduce today's behavior byte-for-byte in `LayoutSink`.

Load-bearing consequence: the >96-word mid-flush split (`flushPartWordBuffer`
[cpp:407-457](../lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp#L407)) is **layout**
(page-fitting) and lives entirely in `LayoutSink`. The walk core accumulates a full
`compiled::Block` (all its words) and hands it off once; `LayoutSink` does its own measure /
mid-block split / paginate, and `ContentSink` does the *different* settings-independent 8 KB
serialization split at write time. Block boundaries in the seam are semantic (paragraph),
not page-fit — matching microreader's whole-`Paragraph` handoff.

## Push seam (`BlockSink`) — producer side

One coarse handoff per semantic block. The walk core fills a `compiled::Block` (interned
style + words, or image ref), then emits it. Mirrors microreader's `ParagraphSink`.

```cpp
struct BlockSink {
  virtual ~BlockSink() = default;
  // Block + its resolved CssStyle (pre-px); sink resolves styleId (intern) or BlockStyle.
  virtual void onBlock(compiled::Block&& block, const CssStyle& style) = 0;
  virtual void onAnchor(const std::string& id) = 0;    // id at the current block/char position
  virtual void onChapter(uint8_t level, const std::string& title) = 0;
  virtual void onPageBreakLabel(const std::string& label) = 0;
  virtual void onFootnote(int wordIndex, const FootnoteEntry&) = 0;
  virtual void onSpineEnd() = 0;                        // flush trailing state
};
```

**Findings that shaped this interface (from reading the parser + model):**
1. **Style is passed alongside, not pre-interned.** `compiled::Block::styleId` indexes a pool
   the *sink* owns, and `LayoutSink` needs the `CssStyle` itself (to build a px `BlockStyle`),
   not an index. So `onBlock` carries the block's resolved `CssStyle` — the em/% value the walk
   holds at block START, *before* the `getFontAscenderSize` em→px step
   ([cpp:1008,1356](../lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp#L1008)). The finished
   `ParsedText` is no good here: its `BlockStyle` is already px (settings-dependent).
2. **Word-attach needs a bit.** `ParsedText` tracks `wordContinues` (`<b>foo</b>bar` = no space),
   but `compiled::Word` didn't. Added as `WordStyleSpan::kSpanAttachPrev` (`styleSpan` bit 6 —
   free, serialized wholesale). Without it the producer can't reproduce spacing.
3. **Producer-first, not LayoutSink-first.** `startNewTextBlock` records `anchorData` as a
   *page number* ([cpp:597,622](../lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp#L597)) and the
   >96-word split lays out mid-block — so "route today's layout through a sink" tangles
   anchor→page with anchor→block-position and the mid-block split. Instead the producer is built
   **additively first** (below): emit blocks with layout untouched, so goldens are byte-identical
   by construction, and the block-emission is validated before the consumer-side unification.

`compiled::Block` otherwise carries everything Stage 1 needs (`type`, `flags`, `charOffset`,
`words{textOff,styleSpan,sizePct,bidiLevel}`+`text`, or `entryPath`/`width`/`height`/`floatSide`/
`alt`). The per-word inline style is built exactly as `flushPartWordBuffer` computes
`fontStyle`/`effectiveSizePct`/`nextWordContinues` today
([cpp:365-405](../lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp#L365)).

Tables and lists are **synthesized inside the core** (it already buffers `currentTable` and
`listStack`) and replayed as ordinary `onBlock` calls — the sink never sees `<table>`/`<li>`,
only the resulting blocks, preserving the exact block sequence the goldens encode.

`LayoutSink::onBlock` runs `resolveBlockFont` + `makePages` (today's path, byte-identical).
`ContentSink::onBlock` runs `internStyle()` + split-at-write into `content.bin`. `onAnchor`:
`LayoutSink` records id→**page** (`anchorData`), `ContentSink` records id→(blockIndex,
charOffsetInBlock) — both fed the same call.

## Pull seam (`IBlockSource`) — later, Stage-2-from-content.bin

When Stage 2 stops re-parsing and reads `content.bin` (a later step, not 2c), it consumes a
random-access source, mirroring microreader's `IParagraphSource` + `MrbChapterSource`:

```cpp
struct IBlockSource {
  virtual size_t blockCount() const = 0;
  virtual const compiled::Block& block(size_t index) const = 0;  // sliding-window cached
};
```

The on-disk impl keeps only a **~32-block window** resident (scan the spine's block offsets
on open, load on demand, evict outside the window) so relayout of a long chapter never loads
it whole. Noted here so the `content.bin` layout (offset table per spine) is designed for it
now; implemented at the Stage-2 flip.

## Incremental commit sequence (each golden-green)

**Producer-first.** The naive "route today's layout through the sink first" order is unsafe
(finding 3: anchor→page coupling + mid-block split). Instead, build and validate the *producer*
additively — with the fused layout untouched, so goldens are byte-identical by construction —
then build the consumer (`LayoutSink`), then unify. `EPUB_STAGE1` (default 0) gates the new
ContentSink/producer wiring so the shipping path is untouched until the unify step.

1. **Flag + interface** (done). `EPUB_STAGE1` (`Stage1Config.h`) + `BlockSink.h`. Additive.
2. **Additive producer, text blocks.** Add `BlockSink* stage1Sink_` (+ setter) to the parser.
   Where it builds words (`flushPartWordBuffer`) and starts blocks (`startNewTextBlock`), also
   accumulate a `compiled::Block` (words{styleSpan incl. attach, sizePct}, text) and capture the
   block's `CssStyle`; at the block boundary emit `onBlock(block, style)` **when `stage1Sink_`
   is set**. Layout is untouched → goldens byte-identical. Host test: a capturing sink over
   `test_headings.epub` asserts the block/word sequence.
3. **Producer, remaining block kinds + tables/lists/images + anchors/chapters/footnotes.** One
   family per commit; each extends the capturing-sink test. Still additive; goldens untouched.
4. **`ContentSink` + `content.bin`** (behind `EPUB_STAGE1`): a `BlockSink` that `internStyle()`s
   each `onBlock` and streams it (split-at-write) with the anchor/chapter/charOffset tables. New
   `content_stage1_dump` target in `test/epub_pipeline/`; determinism check (two runs identical).
5. **`LayoutSink`** implementing `BlockSink`: consumes `onBlock(block, style)` and reproduces
   `resolveBlockFont`+`makePages`+float layout byte-for-byte (the >96-word split lives here).
   Verified by running the parser driving `LayoutSink` and diffing section dumps vs. the fused
   path across the settings matrix.
6. **Unify + extract `HtmlWalkCore`.** Flip the parser to drive *only* sinks (remove the inline
   fused layout, now provided by `LayoutSink`); split the state machine into `HtmlWalkCore` +
   the `LayoutSink` glue. `ChapterHtmlSlimParser`'s Stage-2 entry point + output stay unchanged.
   Full equivalence gate: settings matrix byte-identical, footnote/anchor set equality.

Steps 2–4 are additive (producer only) and behind `EPUB_STAGE1`; 5–6 carry the golden risk and
land last, once the producer they consume is validated. Only after 6 do later steps (Stage-2
reads `content.bin` in the real build via the pull `IBlockSource`, footnote/image-manifest
folding, `html_<spine>.bin` retirement) proceed — the back half of Phase 3 in the master plan.

## Non-goals for step 2c

No change to layout output, fonts, or the display path (goldens are the proof). No PC-side
compile. Stage-2 still reads XHTML+CSS in the shipping build until a later step flips it to
`content.bin`; step 2c only makes the *producer* exist and proves it equivalent.
