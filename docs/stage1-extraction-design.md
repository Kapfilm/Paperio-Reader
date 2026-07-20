# Stage-1 extraction design — shared yxml-walk core

Phase 3 step 2c of `compiled-book-pipeline-plan.md`. Companion to
`parser-stage1-handover.md` (mission) and `compiled-content-format.md` (WBC1 output).
This is the **contract** for the chosen approach — *extract a shared walk core* — written
before any parser surgery, because the gate is **byte-identical goldens** and the state
machine is 2,862 lines.

## Decision

`ChapterHtmlSlimParser` fuses a settings-independent **walk** with settings-dependent
**layout**. We split it so both a Stage-1 content compiler and today's Stage-2 layout
parser drive the *same* walk through a sink interface:

```
                       ┌───────────────────────────┐
  XHTML ─▶ yxml ─▶     │  HtmlWalkCore             │ ─▶ HtmlWalkSink (virtual)
                       │  depth/skip, inline-style │        ├─ LayoutSink   → pages   (Stage 2, today's output)
                       │  stack, CSS resolve, tag  │        └─ ContentSink  → content.bin (Stage 1, new)
                       │  dispatch, word segment,  │
                       │  tables, lists, anchors,  │
                       │  chapters, charOffset      │
                       └───────────────────────────┘
```

The walk core calls `resolveStyle()` and hands the sink a **`CssStyle`** (em/%, logical
align) — *exactly* Stage-1's block style — never touching `GfxRenderer`.

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
(page-fitting) and lives entirely in `LayoutSink`. Stage-1's only block cap is the
serialization 8 KB split at write time (format spec) — a *different*, settings-independent
cut. So the core emits words; each sink decides its own block boundaries.

## Event interface (`HtmlWalkSink`)

Grounded in the parser's current emission points. All positional/size resolution is the
sink's job; the core passes logical values only.

| Sink method | Emitted from (today) | Payload |
|---|---|---|
| `onSpineStart(spineIndex)` / `onSpineEnd()` | setup / finalize | — |
| `onBlockStart(const CssStyle&, uint8_t flags)` | `startNewTextBlock` [cpp:563](../lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp#L563) | resolved block style; flags = chapter/pagebreak bits |
| `onWord(const char* text, EpdFontFamily::Style, uint8_t sizePct, bool continues)` | `flushPartWordBuffer` `addWord` [cpp:396,405](../lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp#L396) | one segmented word + inline style span |
| `onBlockEnd()` | next `startNewTextBlock` / end-of-doc | closes the current block |
| `onBlockImage(entryPath, w, h, floatSide, alt)` | block image / `placeImageBlockAsBlock` | pre-probed intrinsic dims |
| `onAnchor(const std::string& id)` | `pendingAnchorId` resolution [cpp:723](../lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp#L723) | id → (block, charOffsetInBlock); sink maps to page/blockIndex |
| `onChapter(uint8_t level, const std::string& title)` | heading close | TOC/heading entry |
| `onPageBreakLabel(const std::string& label)` | `recordPageBreakLabel` | printed-page label at the current position |
| `onFootnote(int wordIndex, const FootnoteEntry&)` | `pendingFootnotes` | note anchored to a word in the current block |

Tables and lists are **synthesized inside the core** (it already buffers `currentTable`
and `listStack`) and replayed to the sink as ordinary `onBlockStart`/`onWord`/`onBlockImage`
calls — so the sink never sees `<table>`/`<li>`, only the resulting blocks. This keeps
both sinks simple and preserves the exact block sequence the goldens encode.

`anchorData` (id→page) is a `LayoutSink` product; `ContentSink` records id→(blockIndex,
charOffsetInBlock). Both consume the same `onAnchor`.

## Incremental commit sequence (each golden-green)

The whole point of the sink is that the refactor is *mechanical and reversible per step*.
`EPUB_STAGE1` (default 0) gates only the *new* ContentSink call sites; the LayoutSink
extraction (steps 2–4) is a pure refactor that ships unconditionally and must keep every
host test + golden identical at each commit.

1. **Flag + interface.** Add `EPUB_STAGE1` (`#ifndef/#define … 0`, mirroring the retired
   `EPUB_BUILD_ARENA` pattern) and `HtmlWalkSink.h` (the pure-virtual interface above). No
   call sites yet. Additive; zero behavior change.
2. **Introduce `LayoutSink`** owning the layout members, but keep `ChapterHtmlSlimParser`
   calling it *directly* (methods delegate). Goldens identical — this is the risky move,
   done first and verified before anything depends on it.
3. **Route emissions through the sink.** Replace each direct layout touch in the state
   machine with the corresponding `sink_->onX()` call, `LayoutSink` being the sink. One
   event family (block / word / image / anchor / chapter / table / list) per commit, each
   diffed against goldens. At the end the state machine is renderer-free.
4. **Extract `HtmlWalkCore`** as the state machine minus the layout members;
   `ChapterHtmlSlimParser` becomes `HtmlWalkCore` + `LayoutSink` glue (Stage-2 entry point,
   unchanged signature/output).
5. **Add `ContentSink` + `ContentCompiler`** (behind `EPUB_STAGE1`): drive `HtmlWalkCore`
   with a sink that `internStyle()`s + emits `compiled::Block`/`Word`/`Anchor`/`Chapter`
   into `content.bin`. New `content_stage1_dump` target in `test/epub_pipeline/`.
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
