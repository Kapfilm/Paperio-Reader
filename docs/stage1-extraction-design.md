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
  virtual void onBlock(compiled::Block&& block) = 0;   // one complete block
  virtual void onAnchor(const std::string& id) = 0;    // id at the current block/char position
  virtual void onChapter(uint8_t level, const std::string& title) = 0;
  virtual void onPageBreakLabel(const std::string& label) = 0;
  virtual void onFootnote(int wordIndex, const FootnoteEntry&) = 0;
  virtual void onSpineEnd() = 0;                        // flush trailing state
};
```

`compiled::Block` already carries everything Stage 1 needs (`type`, `styleId` after intern,
`flags`, `charOffset`, `words{textOff,styleSpan,sizePct,bidiLevel}`+`text`, or `entryPath`/
`width`/`height`/`floatSide`/`alt`). The per-word inline style (bold/italic/…/sizePct) is
built exactly as `flushPartWordBuffer` computes `fontStyle`/`effectiveSizePct` today
([cpp:365-405](../lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp#L365)); the block `CssStyle`
is the `resolveStyle()` result `startNewTextBlock` already has.

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

The whole point of the sink is that the refactor is *mechanical and reversible per step*.
`EPUB_STAGE1` (default 0) gates only the *new* ContentSink call sites; the LayoutSink
extraction (steps 2–4) is a pure refactor that ships unconditionally and must keep every
host test + golden identical at each commit.

1. **Flag + interface.** Add `EPUB_STAGE1` (`#ifndef/#define … 0`, mirroring the retired
   `EPUB_BUILD_ARENA` pattern) and `BlockSink.h` (the pure-virtual push interface above; the
   pull `IBlockSource` is stubbed with a doc note, built at the Stage-2 flip). No call sites
   yet. Additive; zero behavior change.
2. **Introduce `LayoutSink`** implementing `BlockSink` and owning the layout members
   (`currentTextBlock`, pages, floats, `makePages`, renderer). `ChapterHtmlSlimParser` builds
   a `compiled::Block` per paragraph and calls `sink_->onBlock(...)`, with `LayoutSink`
   reproducing today's measure+paginate byte-for-byte. This is the risky move (parser now
   materializes a block before layout) — done first, verified against goldens before anything
   depends on it.
3. **Move the remaining emissions to the sink.** Route `onAnchor`/`onChapter`/
   `onPageBreakLabel`/`onFootnote`/`onSpineEnd` (and table/list-synthesized `onBlock`s)
   through `sink_`, `LayoutSink` being the sink. One family per commit, each diffed against
   goldens. At the end the state machine is renderer-free and emits only through `BlockSink`.
4. **Extract `HtmlWalkCore`** as the state machine minus the layout members;
   `ChapterHtmlSlimParser` becomes `HtmlWalkCore` + `LayoutSink` glue (Stage-2 entry point,
   unchanged signature/output).
5. **Add `ContentSink` + `ContentCompiler`** (behind `EPUB_STAGE1`): drive `HtmlWalkCore`
   with a `BlockSink` that `internStyle()`s each `onBlock` and streams it (split-at-write)
   into `content.bin`, plus the anchor/chapter/charOffset tables. New `content_stage1_dump`
   target in `test/epub_pipeline/`.
6. **Equivalence gate.** Stage-1→Stage-2 vs. today's fused path: byte-identical section
   dumps across the settings matrix (default, large font, hyphenation on/off, bionic,
   narrow margins); determinism (two Stage-1 runs identical `content.bin`); footnote/anchor
   set equality.

Steps 2–4 carry the golden risk and land first, unflagged; 5–6 are additive behind the
flag. Only after 6 is green do later steps (Stage-2 reads `content.bin` in the real build,
footnote/image-manifest folding, `html_<spine>.bin` retirement) proceed — those are the
back half of Phase 3 in the master plan.

## Non-goals for step 2c

No change to layout output, fonts, or the display path (goldens are the proof). No PC-side
compile. Stage-2 still reads XHTML+CSS in the shipping build until a later step flips it to
`content.bin`; step 2c only makes the *producer* exist and proves it equivalent.
