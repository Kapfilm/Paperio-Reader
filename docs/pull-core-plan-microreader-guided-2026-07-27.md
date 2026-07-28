# Pull-core implementation plan — microreader-guided

One consolidated plan for the live-pagination layout core, using
CidVonHighwind/microreader's **proven** `TextLayout` as the structural spine and layering our
improvements on deliberately. Supersedes the scattered decisions in
`stage1-single-source-live-pagination-2026-07-27.md` §3a for the *implementation* (that doc stays
the architecture/rationale record). Clean-room: we mirror microreader's STRUCTURE, not its code.

## 0. Why microreader guides this

microreader is a shipping ESP32-C3 EPUB reader that already does exactly what we want: read live,
one page at a time, from a cursor, over a compiled book, with a small per-paragraph cache and
symmetric forward/backward layout. Its `TextLayout` is the reference implementation. We follow its
decomposition because it is proven to work in the same constraints (same chip, same RAM budget),
and we deviate only where our format/engine differs or where our improvements add value.

**Our improvements to keep in sight (not lose while copying the structure):**
1. **Background compilation + fast first page** — first open never waits for content.bin.
2. **Arena memory** — the per-paragraph cache + working lines come from a reader-owned arena, not
   heap (kills the fragmentation that broke Increment F). microreader uses plain `std::vector`.
3. **Richer layout** — our engine has floats, grid tables, inline footnote previews, the
   hyphenation page-break retry, the empty-block margin-collapse merge, and the heading aux-font
   ladder. microreader's paragraph model is simpler. These are the deviations we must handle.
4. **content.bin v7** — our compiled source, with the baked block-offset table (G1) giving O(1)
   seek. microreader's `.mrb` has the analogous baked per-paragraph descriptor table.

## 1. microreader's structure → our mapping

| microreader (`TextLayout`) | our pull core (`PageLayout`) | notes |
|---|---|---|
| `TextParagraph` (source unit) | `compiled::Block` (one logical block) | 1 block == 1 "paragraph" |
| `IParagraphSource` (windowed `.mrb` reader) | `BlockStreamReader` + a windowed reader (G3) | our `MrbChapterSource` analog |
| `layout_para_lines()` | `ParsedText::layoutAndExtractLines()` | REUSE verbatim — measures/wraps/hyphenates |
| `LaidOutParagraph` (cached lines+geom) | `LaidOutBlock` (NEW) | the per-block cache slot |
| `para_cache_` 16-slot ring, `cache_valid_` | `BlockCache` ring (NEW), invalidate on params change | arena-backed on device |
| `LaidOutParagraph::collect(idx,avail)` / `collect_backward` | `LaidOutBlock::collect` / `collectBackward` (NEW) | pull one item that fits; symmetric |
| `collect_para_items` / `_bwd` | per-block collect loops (NEW) | `used`/`pending_desc` descender bookkeeping |
| `collect_page_items(pos)` / `_backward(end)` | `collectPageForward(cursor)` / `collectPageBackward(end)` (NEW) | loop blocks until page full; precise boundary |
| `assemble_page(items,start,end)` → `PageContent` | `assemblePage(...)` → our `Page` (NEW) | absolute coords + vertical centering |
| `PagePosition{paragraph,offset,text_offset}` | `compiled::PagePosition` (G2a, extended) | offset polymorphic; + carried auxFontId/imageCounter |
| `layout()` / `layout_backward()` | `layoutPage(cursor)` / `layoutPageBackward(end)` | the public entry (G2a interface stays) |

**The three microreader ideas we must not lose in translation:**
- **Page-independent per-paragraph cache.** A paragraph is laid out ONCE (line-broken, image-scaled)
  and reused by the page that ends on it AND the page that starts on it. This is what makes live
  turning cheap. Our cache holds the block's page-independent line-set.
- **`used`/`pending_desc` descender accounting** (collect_para_items comment, TextLayout.cpp:1182):
  the last line on a page charges only its baseline (descender hangs into the bottom padding), all
  earlier lines charge full height. Tracking the uncharged descender separately makes the forward
  and backward height budgets IDENTICAL — the reason backward layout lands on the same boundaries
  as forward. We replicate this exactly.
- **`collect` returns a `next_idx` cursor**, and the boundary is a natural output of the collect
  loop — not a separate computation. Mid-block resume is just `start_idx = cursor.offset`.

## 2. Where our engine deviates from microreader (the hard parts, each with a plan)

microreader's paragraphs are more self-contained than our blocks. Each deviation gets an explicit
strategy so the byte-identical oracle passes.

**D1 — Hyphenation page-break retry (the delicate one).** Our `ParsedText` re-wraps a hyphenated
line that lands last-before-break WITHOUT hyphenation, rewriting the word vector mid-emit
(ParsedText.cpp:335-422). microreader has no such rule. *Plan:* the cache holds the
page-independent HYPHENATED line-set; the collect loop, when a cached hyphenated line becomes the
last that fits on the page, re-wraps just THAT line no-hyphen (the ParsedText single-line-no-hyphen
path) before finalizing the boundary. Retry stays a collect-layer decision; cache stays clean.
Guarded by the `hyphen` oracle profile.

**D2 — Empty-block margin-collapse merge.** Consecutive empty wrapper/`<br>` blocks accumulate a
pending merged style that folds into the next non-empty block (LayoutSink.cpp:777-820). *Plan:* when
starting a page at block N, walk back over the immediately-preceding empty/`<br>` run to rebuild the
pending-merge/alignment state (bounded window — the restart-boundary analysis). A block's cache
entry records whether it is empty so the walk-back is cheap.

**D3 — Heading aux-font latch (`auxFontId`).** Spine-scoped first-heading-size-wins; not
page-reconstructible. *Plan:* CARRIED in the cursor (already in `PagePosition`). Seed
`resolveBlockFont`'s latch from `cursor.auxFontId` before laying out the page; the page's own
headings may advance it, and the end cursor carries the new value.

**D4 — Floats.** A float image's zone spans several blocks but never crosses a page (proven).
*Plan:* the originating block's cache entry carries the float zone; blocks the float overlaps get
the zone injected during collect (as `makePages` does). Because floats are page-bounded, this is
local to the page being collected. (Corpus float coverage: `test_floats`-style books.)

**D5 — Grid tables.** One block → row-packed into independent per-page `PageTableFragment`s;
`offset` = row index. *Plan:* the cache entry holds the wrapped rows + row heights; `collect`
pulls rows until the page is full, emitting a fragment; `collect_backward` packs rows upward.
mirrors `packTableFragments`.

**D6 — Inline footnote previews + xpath LUT + page-break labels.** Side outputs LayoutSink produces.
*Plan:* preview abbreviation stays where it is (viewport-dependent, per-block, in the cache build).
The xpath/anchor/label/chapter cross-referencing (keyed on record index) is applied during collect
exactly as `replaySpine` does (PageLayout G2a already threads this). LUT entry per emitted page.

## 3. Build sequence (each step host-green against the oracle before the next)

The oracle throughout: the whole-spine `LayoutSink` run (`replaySpine`) is the GOLDEN. A pull-core
page laid out from page K's start cursor must be byte-identical (via `dumpOnePage`) to golden page
K. Extended over the corpus × profile matrix. (G2a already gates the FIRST page of each spine.)

- **P1 — Forward text collect.** `LaidOutBlock` (text only), `BlockCache` ring, `collectPageForward`
  with the `used`/`pending_desc` budget + D1 hyphen retry + D2 empty-block walk-back + D3 aux-font
  seed. Replace the scaffold's LayoutSink-driving innards for text books. Oracle: ALL pages of the
  text corpus (test_text_rendering, test_headings, test_kerning_ligature, moby-dick) × profiles ==
  golden. Precise end cursor now emitted (line granularity).
- **P2 — Images + HR.** Add block-image + HR placement. **NOTE (verified 2026-07-27): our block
  images are ATOMIC — they never split across pages.** `computeImageDisplaySize` (ImageLayout.cpp:24,
  38, 55, 64) clamps display height to `viewportHeight` in every branch, and `placeBlockImage`
  (LayoutSink.cpp:438-455) page-breaks a whole image to the next page if it doesn't fit, then places
  it entire. So — UNLIKE microreader, whose promoted/standalone images split by pixel row — our
  image `collect` is trivial: one image = one indivisible item that either fits the remaining page
  or forces a page break. NO pixel-row `offset`, NO partial slices, NO `y_crop`. The polymorphic
  cursor `offset` therefore only ever needs line-index (text) and row-index (table); the pixel-row
  case from microreader does not apply to us. HR is likewise a single indivisible item (half-line
  margins above/below). Oracle: image/HR books (test_png_images, test_jpeg_images, test_mixed_images)
  pass all pages.
- **P3 — Floats (D4). DECIDED 2026-07-27: floats stay on the SCAFFOLD fallback — a deliberate
  boundary, not a gap.** A float image's zone carries ABSOLUTE page-Y coordinates and narrows the
  line-widths of the block it rides on AND every later block it vertically overlaps (LayoutSink feeds
  `layoutAndExtractLines` a `blockStartY`/`lineHeightForFloat` derived from `currentPageNextY_`). So a
  float-affected block's LINE-BREAKING depends on page position — the one place the page-independent
  cache genuinely does not fit our engine (unlike text/image/HR/table). Floats are rare (one corpus
  book, `test_float_images`), page-bounded (a float never crosses a page), and already byte-identical
  via the scaffold. `needsScaffold` keeps deferring a Text block with a float image; a float-bearing
  SPINE is served one page at a time by the scaffold LayoutSink, while the pull core handles all
  non-float content. Revisit only if profiling shows float spines common enough to matter (they are
  not in normal reading). The rejected alternative (lazy per-page layout of float blocks in the
  collect hot path) buys full parity at the cost of the uniform cache + collect-loop complexity.
- **P4 — Grid tables (D5). DECIDED 2026-07-27: tables stay on the SCAFFOLD fallback.** A table is
  one `Block` row-packed into independent per-page `PageTableFragment`s with NO cross-page state
  (only `currentPageNextY_` crosses, which resets per page), so it COULD fit the pull core — but the
  machinery is substantial (cache `layoutRows`, run `packTableFragments` at collect time with a pull
  context, plus the paragraph-fallback + oversize-row paths that emit `PageLine`s mid-table). Only
  one corpus book uses tables (`test_tables`), already byte-identical via the scaffold, so the value
  is low next to P5 (backward layout, which the reader NEEDS for prev-page). `needsScaffold` keeps
  deferring Table blocks. Revisit if tables prove common. (Unlike floats, this is a value/effort
  call, not a cache-model limitation — tables would fit the cache cleanly.)
- **P5 — Backward layout.** `collectPageBackward(end)` + `collect_backward` per block type
  (symmetric, reuses the cache). Oracle: `layoutPageBackward(page K+1 start)` == golden page K,
  whole corpus × profiles. At this point the whole corpus passes forward AND backward (float + table
  spines excepted — served by the scaffold, see P3/P4) — the pull core is the read engine for all
  text/image/HR content; the scaffold remains only for float + table spines.
- **P6 — Arena memory home.** Move the `BlockCache` slots + working lines onto a reader-owned
  `BuildArena` (device); host keeps heap. Deterministic footprint; this is the anti-fragmentation
  improvement. (Can be deferred to G5 reader-wiring if cleaner, but decide here.)

After P5 the pull core is the sole read engine (behind `EPUB_STAGE1`). Then the outer sequence
resumes: **G4** device latency gate (ms/page fwd+bwd on King's Avatar + Small Gods — the GO/NO-GO
before deleting section files), **G5** reader read-loop rewrite (PagePosition nav + background
compile Option-2 + frontier hand-off + arena), **G6** delete section files + flip the flag.

## 4. What we explicitly reuse vs. write new

**Reuse verbatim (no behavior change):** `ParsedText::layoutAndExtractLines` (+ the single-line
no-hyphen path for D1), `buildBlockStyle`/`resolveBlockFont` (LayoutSink helpers — may lift to a
shared header), `computeImageDisplaySize`/`ImageLayout`, `packTableFragments`/`TableLayout`, the
`Page`/`PageLine`/`PageImage`/`PageHR`/`PageTableFragment` output types (renderer untouched),
`BlockStreamReader` + `seekToBlock` (G1).

**Write new (the pull core):** `LaidOutBlock`, `BlockCache`, the `collect`/`collect_backward` per
block type, `collectPageForward`/`collectPageBackward`, `assemblePage`, and the `layoutPage`/
`layoutPageBackward` entries (replacing the G2a scaffold body).

**Retire:** `LayoutSink`'s pagination role (its `onBlock`→`makePages`→`emitPage` streaming). Its
per-block measure/wrap + block-style logic survives as the reused helpers. Kept as the test golden
until P5 proves the pull core, then it stays only as the oracle + the background-compile content
sink path (which needs no pagination).

## 5. RTL readiness (near-term, not built now but not designed out)

microreader assigns word `x` during greedy fitting, then a per-line post-pass (`align_line`/
`flush_line`, TextLayout.cpp:216/276) shifts x for Center/End/Justify — direction-agnostic
line-breaking + a per-line alignment pass. Our RTL will mirror x within the line
(`line_width - x - w`) and reverse BiDi run order in that SAME per-line pass. The pull core must
therefore keep alignment/x-finalization a per-line step on a completed line (as both microreader
and our `ParsedText::extractLine` already do), never fused into the collect/page-break loop. No
collect-loop code should assume LTR. That is the only RTL constraint the pull core imposes now.

## 6. Risks / open checks

- **D1 fidelity** is the top risk; the `hyphen` profile oracle across the corpus is the guard. If a
  diff appears, it will be at a hyphenated page boundary — compare pull vs golden dumps there.
- **Vertical centering / descender** parity (P1): the `used`/`pending_desc` accounting must match
  `addLineToPage`'s Y math exactly. Pin with a small unit test on a 2-line page before the matrix.
- **Cache size N**: 16 (microreader) is a starting point; a page rarely spans >8 of our blocks.
  Tune at G4 with real books. Undersized N just re-lays-out (correct, slower) — never wrong.
- **Latency (G4)** remains the GO/NO-GO. The cache is the mitigation; measure before deleting
  anything.
