# Handover — Phase 3 step 2c: rendererless Stage-1 parser pass

Read this first if you're picking up the compiled-content migration. It hands off the
**one large remaining piece**: splitting `ChapterHtmlSlimParser` into a settings-independent
Stage-1 pass that emits `content.bin`, and a Stage-2 layout pass that reads it. Everything
around it is already built, tested, and on `refactor-parsing` (PR #82).

Companion docs: `compiled-book-pipeline-plan.md` (the master plan), `compiled-content-format.md`
(the WBC1 format this pass fills). Related memories: `project-pipeline-migration`,
`project-parser-comparison`, `project-css-arena-resident`.

## Mission

Turn the fused `XHTML → parse → CSS → measure → paginate` pass into two stages:

```
Stage 1 (once/book, NO GfxRenderer):  XHTML ─▶ parse ─▶ resolve CSS ─▶ content.bin
Stage 2 (per settings variant):       content.bin ─▶ measure + paginate ─▶ sections/<spine>_<hash>.bin
```

Goal: a font/margin/hyphenation/bionic change re-runs only Stage 2 (no ZIP/XML/CSS) →
target ≥3× faster relayout. `content.bin` is invalidated only by the ZIP content fingerprint
(Phase 1), never by a reading setting.

## What is already done (don't redo)

- **Format spec**: `docs/compiled-content-format.md` (WBC1). Seam = unit resolution.
- **Container** (`lib/Epub/Epub/content/CompiledContent.{h,cpp}`): in-memory model
  (`CompiledContent`, `SpineContent`, `Block`, `Word`, `Anchor`, `Chapter`), `writeContentBin`/
  `readContentBin`, `internStyle()`/`styleEquals()` (style-pool dedup), anchor/chapter tables,
  per-block `charOffset`. Round-trip + dedup tests in `test/content_bin/`. **Not wired into any
  build** — this pass is the first consumer.
- **Direction chosen**: two-stage, but structure the Stage-1 pass on **freeink's clean
  `BlockSink`/`flushBlock` pattern**, EXTRACTED from the existing parser (goldens preserved),
  NOT rewritten. See `project-parser-comparison`.

## The seam (what goes where)

Divide on **unit resolution**: CSS units (em/%) + logical values = settings-independent
(Stage 1); pixels (resolved against font size) + x/y positions (from measurement) = Stage 2.

| | Stage 1 → content.bin | Stage 2 (unchanged output) |
|---|---|---|
| Block style | `CssStyle` (em/%, logical align incl. start/end) | `BlockStyle` (px margins, resolved headingFontId, floatZones) |
| Text | words (raw Unicode) + per-word styleSpan + sizePct + bidiLevel | per-word `xpos[]`, line/page breaks, shaping |
| Structure | anchors, chapters, per-block charOffset | page↔anchor map, per-page paragraph LUT |

Load-bearing rule: **Stage 1 must not call `GfxRenderer`.** The current parser's renderer
uses (in `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp`) are the exact things to move to
Stage 2:
- `getFontAscenderSize` (~lines 1008, 1356) — resolves CSS em→**px** during block-style build.
- `getTextWidth` / `getSpaceWidth` (~161, 171) — word measurement.
- `getLineHeight` (~581, 1485, 2323, 2677) — line/page breaking.

## Approach

`ChapterHtmlSlimParser` is **not** a virtual-handler you can subclass — it's a `Print` sink
(`class ChapterHtmlSlimParser final : public Print`, header line 31) that feeds the **yxml**
tokenizer (`lib/SaxParser/yxml.h`) and dispatches through a 2,866-line internal state machine.
So Stage 1 is an **extraction from that state machine**, reusing its proven pieces:

1. **Reuse `CssParser::resolveStyle()`** — it already returns a `CssStyle` (em/%, pre-px),
   which is *exactly* Stage-1's block style. `internStyle()` it into the pool.
2. **Reuse the yxml tokenization + tag dispatch + word segmentation** so block boundaries and
   text extraction match today's output byte-for-byte (the goldens encode this behavior).
3. **Fork at the emit point**: where the current parser measures + lays out a block into a
   `Page`, Stage 1 instead emits a `compiled::Block` (styleId + words{styleSpan,sizePct} + text,
   or image ref + dims) through a freeink-style `BlockSink`. Stop before em→px / measure / break.
4. Drive the whole book (all spines) → one `content.bin` via `writeContentBin`. Probe image
   dims at compile time for the image records.

Structure guidance from freeink (`freeink-sdk/libs/book/FreeInkBook/src/layout/ChapterLayout.cpp`):
a `PageSink`-style virtual with `onBlock`/`onAnchor`, `onStartElement`/`onText`/`flushBlock`
block-flow. Its `Page::charStart` == our per-block `charOffset` (layout-parameter-independent
reading anchor) — model charOffset accounting on it. freeink is also the RTL blueprint
(`shapeArabic`, `reverseUtf8`, per-BYTE bidi levels) for when RTL lands — Stage 2 owns shaping.

## Guardrails / gate

- **Behind `-DEPUB_STAGE1`** (default OFF). The existing fused path stays byte-identical when
  off — every one of the 371 host tests + all goldens must stay green.
- **Golden equivalence is the gate**: build Stage-1 → Stage-2 and diff the section dumps against
  today's fused-path goldens across the settings matrix (default, large font, hyphenation on/off,
  bionic, narrow margins). Any diff is a bug or a documented, whitelisted intentional change.
- **Determinism**: two Stage-1 runs must produce byte-identical `content.bin`.
- Host harness to extend: `test/epub_pipeline/` (already builds the real pipeline on host with a
  deterministic renderer stub + goldens for the whole corpus). Add a Stage-1 dump + equivalence
  target here.

## Suggested first steps

1. Add `EPUB_STAGE1` (mirror `EPUB_BUILD_ARENA` in `Section.cpp:19-20`), default 0.
2. Define the `BlockSink` interface + a `ContentCompiler` that owns a yxml-driven walk; wire a
   host `content_stage1_dump` tool in `test/epub_pipeline/` that runs it and prints the block model.
3. Get one simple book (e.g. `test/epubs/test_headings.epub`) producing correct blocks, then grow
   coverage tag-by-tag (p, headings, br/hr, blockquote, li, bold/italic, images, tables, footnotes)
   until the Stage-1 dump is stable and complete.
4. Then Stage-2 (step 3): a layout engine reading `content.bin` → `Page` → section cache, gated by
   golden equivalence. THIS is where the actual UX win is proven.

## Where things stand

- Branch `refactor-parsing`, PR #82 (Phases 0-2 device-validated + Phase 3 groundwork). Phase 2
  fix (arena-resident CSS) is device-validated; a real-book validation sweep + merge + release is
  the parallel Tier-1 track (see `project-pipeline-migration`).
- freeink submodule now pinned to reachable `fork/chore-borrow` (jpirnay/freeink-sdk).
- `EPUB_BUILD_ARENA=1` is default; `EPUB_STAGE1` does not exist yet.
- Parser divergence for reference: witchhunt's parser is 2,866 lines vs crosspoint-reader
  upstream's 1,443 (witchhunt added footnotes/tables/floats/bionic) — the Stage-1 pass must
  reproduce witchhunt's behavior, not upstream's.
