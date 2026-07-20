# Compiled content format (`content.bin`, magic `WBC1`)

Status: DRAFT (Phase 3 step 1 of compiled-book-pipeline-plan.md). Written before
the writer/reader so the record layout is reviewed up front. Seam decision (see
below) is **split the parser in two**.

## Why

Today `ChapterHtmlSlimParser` fuses three jobs in one tree walk and caches only
the fully-paginated result, keyed by a per-settings `propertyHash`:

```
XHTML ─▶ parse tags ─▶ resolve CSS ─▶ measure words (GfxRenderer) ─▶ break lines/pages ─▶ Page{TextBlock,ImageBlock}
        └──────────── settings-INDEPENDENT ────────────┘ └──────────────── settings-DEPENDENT ─────────────────┘
                                                                    sections/<spine>_<propertyHash>.bin
```

Changing font, margins, hyphenation, or bionic throws away the cache and re-runs
the **whole** chain — inflate ZIP, parse XHTML, resolve CSS, re-measure,
re-paginate. `content.bin` freezes the settings-independent left half so a
relayout only re-runs the cheap right half:

```
Stage 1 (once/book):   XHTML ─▶ parse ─▶ resolve CSS ─▶ content.bin
Stage 2 (per variant): content.bin ─▶ measure + paginate ─▶ sections/<spine>_<genHash>.bin   (no ZIP/XML/CSS)
```

Invalidation: `content.bin` is keyed only by the ZIP content fingerprint (Phase 1)
plus `WBC1` version — never by any reading setting. Stage-2 caches keep their
generation hash (Phase 4).

## The seam — settings-independent vs settings-dependent

The dividing line is **unit resolution**. Anything expressed in CSS units (em, %,
rem) is settings-independent and belongs in `content.bin`; anything in **pixels**
(resolved against the chosen font size) or in **x/y positions** (from word
measurement) is layout output and stays in the Stage-2 section cache.

| Concept | Settings-independent → `content.bin` | Settings-dependent → section cache |
|---|---|---|
| Block style | `CssStyle` (**logical** align incl. start/end, margins/indent as `CssLength` em/%, font-weight/style, size multiplier, page-break, list-style, float, base direction) | `BlockStyle` (margins/indent in **px**, start/end→left/right, resolved `headingFontId`, `floatZones`, `firstLineExtraIndent`) |
| Text | word strings (raw Unicode) + per-word inline-style flags + per-word size% + bidi level | per-word `xpos[]`, line breaks, page assignment, per-line reorder, shaping |
| Image | entry path, intrinsic `width`/`height`, alt, float side | crop windows, on-page x/y, placeholder decisions |
| Structure | anchor→(block,charOffset) table, char-offset progress table, chapter/heading table | page→anchor label map, per-page paragraph LUT |

Load-bearing consequence: **Stage 1 must not touch `GfxRenderer`.** The current
parser measures words mid-walk; the refactor (step 2) extracts the tag→block +
CSS-resolution walk into a rendererless Stage-1 pass, and moves word measurement
+ line/page breaking into a Stage-2 layout engine fed from `content.bin`.

## File layout

All integers little-endian (device is LE; matches existing `.bin` caches).
Strings are `u16 len` + bytes (no NUL). One `content.bin` per **book** (not per
spine), so cross-spine anchor resolution needs no extra opens.

```
Header (fixed):
  magic         u8[4]   "WBC1"
  version       u8      format revision (bump to invalidate)
  flags         u8      bit0: hasCharOffsets
  spineCount    u16
  reserved      u8[8]
Section table (spineCount entries), by spine index:
  blockTableOff u32     file offset of this spine's block-record run
  blockCount    u32
  anchorOff     u32     offset of this spine's anchor table (0 = none)
  anchorCount   u16
  firstCharOff  u32     absolute char offset of the spine's first char (progress)
Block records (per spine, in document order):  ← the payload
  each record ≤ 8 KB serialized (split at write time; see "Record cap")
Anchor table (per spine):
  count u32, then entries: { idStr, blockIndex u32, charOffsetInBlock u32 }
  (id stored as a string, not a hash — anchors per spine are few and a hash
   collision would silently resolve the wrong target)
Chapter/heading table (book-level):
  count u32, then entries: { spineIndex u16, blockIndex u32, level u8, titleStr }
Char-offset (reading progress): folded into each block record as `charOffset u32`
  (absolute char offset of the block's first char) rather than a separate table.
String/aux tables: streamed through temp files during the pass, spliced at finish
  (never all held in RAM — mirrors endCacheCompile's temp+patch approach).
```

### Block record

```
type          u8      0 = TEXT, 1 = IMAGE
common:
  styleId     u16     index into the book-level dedup'd CssStyle pool (see below)
  flags       u8      bit0: startsChapter, bit1: pageBreakBefore, bit2: pageBreakAfter,
                      bits3-4: base direction (0 auto, 1 LTR, 2 RTL) — see "RTL / BiDi"
  charOffset  u32     absolute char offset of this block's first char (reading progress)
TEXT:
  wordCount   u16
  per word:   textOff u16 (into text[]), styleSpan u8 (bits 0-6 = bold/italic/
              underline/strikethrough/super/sub/smallcaps; bit 7 = attaches to previous
              word with no leading space, e.g. <b>foo</b>bar), sizePct u8
              (100 = inherit), bidiLevel u8 (Unicode embedding level; 0 = pure-LTR — see RTL)
  text        char[]  words back-to-back, each NUL-terminated (raw Unicode; shaping
              is a Stage-2 concern)
IMAGE:
  entryPath   str     EPUB-internal path (e.g. OEBPS/images/x.jpg)
  width,height i16    intrinsic dimensions (pre-probed at compile)
  floatSide   u8      0 none / 1 left / 2 right
  alt         str
```

Notes:
- **CssStyle pool**: block styles repeat heavily (every `<p>` shares one). Dedup
  into a book-level pool written once; blocks carry a `u16 styleId`. Keeps records
  small and the pool is the Stage-2 relayout input. (~108 B/style, deduped.)
- **Per-word style/size** is the settings-independent slice of today's TextBlock
  (`styles[]`, `sizes[]`); `xpos[]` is dropped — Stage 2 computes it.
- Record cap: a text block longer than 8 KB serialized is split into multiple
  records at write time (microreader `MrbConverter` rationale — read-time memory
  safety enforced by shaping the write). Continuation records set a `flags`
  continuation bit so Stage-2 layout treats them as one logical paragraph.

## Stage-2 layout engine (consumer)

Reads a spine's block records + the CssStyle pool, and for each block:
1. Resolve `CssLength` insets/indent → px against the current font em.
2. Resolve heading multiplier → `headingFontId` or body-scale.
3. Measure words (`GfxRenderer`), break lines to `effectiveWidth`, break pages.
4. Emit today's `Page{TextBlock,ImageBlock}` → `sections/<spine>_<genHash>.bin`.

i.e. exactly the current parser's right half, with its input swapped from a live
XHTML walk to `content.bin` records. Golden equivalence (old fused path vs new
Stage1+Stage2) is the critical gate — byte-identical section dumps across the
settings matrix.

## RTL / BiDi readiness (Hebrew, Arabic)

Not implemented now, but the format is **provisioned** so adding RTL later needs
no `WBC1` bump / full recompile. The Unicode Bidirectional Algorithm decomposes
along the same seam as unit resolution:

- **Settings-independent (Stage 1 → `content.bin`)**: base paragraph direction
  (block `flags` bits 3-4), resolved embedding **levels** (per-word `bidiLevel`),
  *logical* alignment (keep `start`/`end` — do NOT collapse to left/right at parse
  time as `CssParser::interpretAlignment` does today), and raw Unicode text.
- **Settings-dependent (Stage 2)**: per-**line** reordering (UBA rule L2, needs the
  line breaks), Arabic cursive shaping, neutral mirroring, measurement.

Concretely this requires, when RTL lands: (a) a `CssStyle.direction` field feeding
the block flags; (b) resolving `start`/`end` alignment against direction in Stage 2
instead of at CSS parse time; (c) running the UBA in the Stage-1 writer to fill
`bidiLevel`; (d) an L2 reorder + shaping pass in the Stage-2 layout engine. All
four are additive to the record fields reserved above.

BiDi granularity (resolved by the freeink comparison): freeink's ChapterLayout
stores embedding levels **per byte**, which confirms the per-*word* `bidiLevel`
reserved above is too coarse for mixed-script text. When RTL lands, levels move to
per-character storage (a parallel levels blob per text block) — a format addition
at that point; the per-word field stays a fast-path hint for pure-LTR/pure-RTL
blocks. freeink also shapes Arabic + mirrors neutrals at *measurement* time and
bakes the result into runs, so Stage 2 owns shaping and the renderer needs none —
matching our seam. Its `Page::charStart` is exactly our per-block `charOffset`
(a layout-parameter-independent reading anchor), independent validation of the split.

## Migration steps (plan Phase 3, each its own commit series, behind `-DEPUB_STAGE1`)

1. This spec.
2. **Writer**: rendererless Stage-1 pass emits `content.bin` (extraction + CSS
   resolve once). Reuses `ChapterHtmlSlimParser`'s tag/CSS logic minus measurement.
3. **Stage-2 reader/layout**: consumes `content.bin` in place of XHTML+CssParser.
4. Footnote gather → scan over `content.bin` (drops its own ZIP pass).
5. Image manifest folded into image records; `images.bin` retired.
6. `html_<spine>.bin` retired.

## Open questions (resolve during step 2)

- **Inline style spans**: today per-word `styles[]`/`sizes[]` already flatten
  inline `<b>/<i>/<span style>`; confirm that byte-per-word encoding is lossless
  vs. the parser's `StyleStackEntry`, or store spans as runs `{startWord, len,
  styleSpan}` if denser.
- **Tables**: the parser buffers a table model and emits grid-or-paragraphs. Does
  the table model serialize as a distinct block type (2 = TABLE) or pre-flatten to
  text/image blocks at Stage 1? Leaning: flatten at Stage 1 (keeps Stage 2 simple),
  but grid geometry is font-dependent — needs a check.
- **char-offset table** granularity (per block vs per word) for progress accuracy
  vs size — start per-block, revisit if progress % is too coarse.
- **Storage budget**: plan caps `content.bin` at ≤ 1.5× source EPUB; the
  block+text+style-pool encoding should sit well under that — measure in step 2.
```
