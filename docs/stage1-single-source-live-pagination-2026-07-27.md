# Stage-1 rebuild: content.bin as the single conclusive source + live pagination

Status: DESIGN (awaiting sign-off). Supersedes the Increment E/F reader integration
(reverted in `revert(stage1): remove E/F content.bin reader integration`). Follows the
microreader model deliberately, enriched with our two differentiators: **background
compilation** (fast first page) and **arena-based memory**.

## 1. Objective

One conclusive compiled source per book, read live. Concretely:

- **content.bin (WBC1 v6) is the ONLY persisted book cache.** Delete the per-settings
  section-file page store (`sections/<spine>_<hash>.bin`) entirely.
- The reader **lays out one page at a time, live, from a cursor**, sourced by a
  **sliding-window block reader** over content.bin. Nothing paginated is persisted.
- A settings change (font size, margins, alignment, line height, hyphenation, bionic)
  is **free**: there is no page cache to invalidate — the next page just re-lays-out.
- **Background compilation stays**: first open shows page 1 as fast as today while
  content.bin is compiled in the background; live pagination begins for a spine as soon
  as that spine is committed to content.bin.
- **Arena-backed memory**: the sliding window + per-page layout scratch come from a
  reader-owned `BuildArena`, not the general heap — deterministic footprint, no
  fragmentation (the exact failure that broke Increment F).

This is microreader's architecture: `book.mrb` → single source; `MrbChapterSource` →
windowed on-demand paragraph reader; `TextLayout` → live one-page layout from a
`PagePosition`. Our equivalents already exist in embryonic form (content.bin,
`BlockStreamReader`, `LayoutSink`); the work is to make them do one-page-from-cursor
live layout and delete the whole-spine page store.

## 2. What we keep vs. what changes

### Keep (already host-proven)
- **content.bin format (WBC1 v6)** — the compiled, settings-independent source. Per-spine
  self-contained (styles/chapters/anchors/labels in each spine's aux), front-loaded
  spine-offset index, `spineAvailable()` frontier semantics.
- **`ContentBinWriter`** — writes content.bin. Reused by the background compile pass.
- **`LayoutSink`** — the live Stage-2 layout engine (measure/wrap/paginate). Its internals
  (resolveBlockFont, addLineToPage, makePages, image/table/float placement) are reused
  verbatim; we add a cursor-start / one-page-stop mode around them.
- **`BlockStreamReader` + `replaySpine`/`replaySpineStep`** — on-demand block reader +
  driver. Extended (not rewritten) with per-block random access.
- **The two independent bug fixes**: OPF memory-gated index (14ce6dee),
  `Epub::loadForCover` (5c45bc52).
- **`book.bin`** — spine/TOC index. Unchanged (microreader keeps its EPUB/OPF structures too).

### Delete
- **`sections/<spine>_<hash>.bin`** — the per-settings fully-paginated page store, its
  page-offset LUT, anchor→page map, printed-page-label map, per-page paragraph LUT.
- **`Section`'s whole-spine build-to-completion pagination** as the reader's page source
  (the parse→LayoutSink→section-file write path, and `loadPageFromSectionFile`). The
  *compile* half (parse→content.bin) survives, relocated into the background compile pass.
- **The property-hash variant cache + `evictOldVariants`** — no per-settings artifact exists
  anymore, so there is nothing to key or evict.

### Add
1. **Per-spine block-offset table BAKED into content.bin aux** (decided 2026-07-27) — O(1)
   seek to any logical block, so layout can start mid-spine and go backward. This is
   microreader's actual approach: `MrbChapterSource` bulk-reads a baked `count×8`-byte
   descriptor table (file offset + cumulative char offset per paragraph) in one `fread` at
   open — it does NOT scan content. We bake a `blockCount × (u32 fileOffset + u32 charOffset)`
   table into each spine's aux region at `onSpineEnd` (the writer already back-patches the
   spine header + writes aux there); `openSpine` gains one bulk read, O(1) even for a giant
   spine. Content.bin version bump v6→v7 — a stale/foreign .bin just triggers recompile (it
   is a rebuildable cache; the reader already rejects on magic/version/fingerprint mismatch).
   Rejected the "scan on openSpine" alternative: it would fully `readBlock` every record of
   the spine at open (boundaries need the continuation-merge logic), i.e. a full-spine read
   at open for pathological spines — the exact cost the windowed model exists to avoid.
2. **`LayoutSink` cursor mode** — lay out exactly one page starting at a `PagePosition`
   `{blockIndex, wordOffset}`, forward or backward, reporting the end/start position.
3. **`ContentReader`** (new) — the windowed, arena-backed, random-access block source for a
   spine. Our `MrbChapterSource` analog: builds the block-offset table on open, keeps N
   recent blocks resident in an arena, evicts furthest-from-cursor.
4. **Reader read loop rewrite** — `PagePosition`-cursor navigation (next/prev page,
   chapter cross, jump-to-anchor/percent) driving live one-page layout, replacing
   the section-file blit path.

## 3. The read model (per page turn)

```
PagePosition { uint16 spine; uint16 blockIndex; uint16 wordOffset; }   // the cursor
```

- **Render current page**: `ContentReader` for `pos.spine` (arena window) → `LayoutSink`
  in one-page mode starting at `{pos.blockIndex, pos.wordOffset}` → emits ONE `Page` →
  blit. Records the end position for `next_page`.
- **next_page**: start position = previous page's end position. If end == spine end,
  cross to `{spine+1, 0, 0}`.
- **prev_page**: backward layout from the current start (microreader's `layout_backward`):
  measure upward to find the page start that ends exactly at the current start. If at
  spine start, cross to previous spine's last page (backward layout from its end).
- **Settings change**: re-render current page at `pos` with new `LayoutParams`. No
  invalidation, no rebuild — just a re-layout. (This is the whole point.)
- **Progress / anchor / percent jumps**: resolve to a `PagePosition` via the spine's
  anchor table (already in content.bin per spine) + char-offset table (microreader stores
  `para_char_offsets_`; we add per-block char offsets to the block-offset table). No
  page-number LUT needed — position is a cursor, and progress is char-offset / total-chars.

## 3a. The layout engine — pull core, not streaming sink (decided 2026-07-27)

Studying microreader's `TextLayout` against our `LayoutSink` surfaced an architecture fork.
They are opposites, and the microreader shape is the one we adopt.

- **microreader `TextLayout`**: a PURE function — `layout(pos) → PageContent` and
  `layout_backward(pos) → PageContent`, both `const`, computing exactly ONE page from a cursor.
  Built from composable primitives: `layout_paragraph()` (break one paragraph into lines) →
  `LaidOutParagraph` (the cached per-paragraph result, in a 16-slot ring) → `collect()` /
  `collect_backward()` (pull items that fit a pixel budget, symmetric) → `collect_page_items`
  (walk paragraphs until the page is full) → `assemble_page` (absolute coords). Alignment is a
  post-pass (`align_line` shifts word `x` on a finished line vector), fully decoupled from
  pagination.
- **our `LayoutSink`**: a STATEFUL STREAMING SINK — blocks are pushed in via `onBlock`, and
  pages are EMITTED via `completePageFn` as a side effect of the stream. `makePages`/
  `addLineToPage` thread `currentPageNextY_`/`currentPage_`/float-propagation/margin-collapse/
  footnote-assignment/LUT-emission together. No one-page entry, no backward layout, no
  per-paragraph reuse across a page boundary.

**Decision: build a microreader-shaped PULL CORE** (`layout(pos)` / `layout_backward(pos)` over
our compiled `Block` stream, with a per-paragraph line cache) and RETIRE `LayoutSink`'s
pagination role. Reuse `LayoutSink`/`ParsedText`'s per-paragraph MEASURE+WRAP primitive
(`ParsedText::layoutAndExtractLines` == microreader's `layout_para_lines`) and the
block→BlockStyle resolution (`buildBlockStyle`, `resolveBlockFont`) verbatim; what changes is
the page-assembly AROUND it. Rationale:
  1. **Native one-page + backward layout** — the read model needs both; the sink has neither.
  2. **Per-paragraph cache de-risks G4** — a paragraph spanning a page boundary is laid out
     once and reused by the page that ends on it and the page that starts on it. This is the
     single thing that makes live per-page turning cheap; bolting one-page mode onto the sink
     would enter the latency gate WITHOUT it.
  3. **RTL (near-term requirement)** — in microreader, direction is a contained per-line pass
     (mirror word `x` → `line_width - x - w`, reverse BiDi runs); pagination never sees
     direction. In `LayoutSink`, alignment/x-inset is woven into the page state machine, so RTL
     would touch pagination. The pull core keeps the RTL seam where microreader has it.

**Our-types mapping** (microreader → ours):
  - `TextParagraph` (runs) → our `Block` (words/text/style) — one LOGICAL block = one paragraph.
  - `layout_para_lines` → `ParsedText::layoutAndExtractLines` (already measures/wraps/hyphenates).
  - `LaidOutParagraph` (cache slot) → NEW: cache the `std::vector<TextBlock-line>` + per-line
    heights for a block index, in a small ring; key = block index; invalidate on any
    LayoutParams change (font/viewport/spacing/align/hyphenation/bionic/embeddedStyle).
  - `PageContent{items, start, end}` → our `Page` (elements) + the start/end `PagePosition`.
  - `collect`/`collect_backward` → NEW forward/backward "pull items until height budget" over the
    cached lines, returning the boundary cursor.
  - `align_line` → today lives inside `TextBlock`/`PageLine` x-offset; keep as the RTL seam.

**PagePosition** for us: `{blockIndex, lineIndex}` within a spine (mirrors microreader's
`{paragraph, offset}`); `wordOffset`/`text_offset` as the stable re-sync key across a settings
change (microreader's `resolve_stable_position`). PLUS two spine-scoped carried scalars (see the
cross-block analysis below): `auxFontId` and `imageCounter`.

### Cross-block restart-boundary analysis (resolved 2026-07-27 — the algorithm-defining result)

An audit of every LayoutSink member mutated across block boundaries (file:line-cited) settled
how far back a "lay out the page starting at block N" computation must look. The load-bearing
fact: **`emitPage()` (LayoutSink.cpp:76-93) resets ALL pixel-affecting cross-block accumulators**
— `currentPageNextY_→0`, `lastBlockMarginBottom_→0`, all float state cleared, continuing block's
`floatZoneCount→0`. And a float **provably never crosses a page boundary** (attachFloatImage
page-breaks before an oversized float; emitPage clears float state; float height ≤ viewportHeight).

So starting layout at a page's FIRST block with a zeroed cursor is pixel-correct, with exactly
two caveats:

- **Caveat A (bounded local window)**: an empty-block / `<br>` margin-collapse chain
  (`pendingMergeStyle_`/`hasPendingMerge_`/`lastBlockAlignment_`) is NOT flushed at a page
  boundary (empty blocks emit no page). If block N is preceded by a run of empty/`<br>` blocks,
  walk back over just that contiguous run to the last non-empty/image/HR block and replay it to
  rebuild the merge/alignment state. Formally unbounded (a book could have many empty divs),
  practically 0-1 blocks.
- **Caveat B (the ONE genuinely unbounded, pixel-affecting latch)**: `auxFontId_` — a
  spine-scoped, first-writer-wins latch (the first heading size that snaps to the size ladder
  claims the single aux-font budget for the whole spine; later different-size headings fall back
  to scaling). Never reset. A heading at block N renders in a real ladder font vs. a scaled body
  font depending on what resolved FIRST, arbitrarily far back. **Not page-reconstructible → carry
  it in the cursor.**

Non-geometry side effects to seed for byte-identical SIDE tables (not pixels): `imageCounter_`
(image cache filename `img_<spine>_..._<counter>`) and the xpath LUT counters (supplied per-block
by the stream, so they arrive correct with block N — no replay needed). Carry `imageCounter` in
the cursor for stable cache paths.

**Restart boundary (final)**: previous page boundary, extended backward over any immediately-
preceding empty/`<br>` run. Initialize like `emitPage` (zeroed Y, fresh page, no float, no margin
carry); reconstruct the merge/alignment state by replaying only that trailing empty-block run;
take `auxFontId` + `imageCounter` from the cursor. `layout_backward` is symmetric — it finds the
PRECEDING page boundary, well-defined because floats/margins are page-reset. This makes mid-spine
`layout(cursor)` local to "this page boundary + a trailing empty-block lookback," never a
stream-from-spine-start.

**Test oracle**: the existing whole-spine `LayoutSink` pagination stays as the GOLDEN. Page K
produced by the pull core (`layout` from page K's start cursor) must be position-identical to
page K from a full `replaySpine` run, across the corpus × profile matrix. Backward layout:
`layout_backward(page K+1 start)` must reproduce page K. This makes the rewrite verifiable
against the byte-identical engine we already trust.

## 4. Background compilation + fast first page (our differentiator)

Preserve today's first-open latency. On open:

1. **First page fast path**: parse spine 0 (or the text-reference spine) directly through
   `LayoutSink` in one-page mode — show page 1 immediately, exactly as fast as today's
   in-place build shows it. (We do NOT wait for content.bin.)
2. **Background compile**: a sliced background pass (`ContentBinWriter`, Background-B-like,
   1 KB-budgeted, arena-backed) compiles the book to content.bin spine-by-spine, starting
   at the current spine and reading ahead. NO tee — this is a separate walk with the
   content-only sink (the v2 plan's Option-2 shape). It writes content.bin and commits each
   spine's index slot on completion.
3. **Hand-off**: once the current spine is committed (`spineAvailable`), the reader switches
   that spine's `ContentReader` to read from content.bin (frontier chase via `refreshIndex`).
   Until then, the current spine is served by the direct one-page parse path. Because layout
   is per-page and cheap, the "not yet compiled" window is only ever the current spine.

So: first page is never gated on content.bin (fast open preserved), and content.bin becomes
the source for everything the moment it exists. This is the "background compilation + as
fast as possible first page" principle carried forward, now with a single conclusive source.

## 5. Memory (arena-backed)

A reader-owned `BuildArena` holds, per turn:
- The `ContentReader` sliding window: N recent logical blocks for the current spine
  (microreader uses 32 paragraphs; we size N to fit a page's worth + lookahead, TBD by
  measurement). Blocks are arena-allocated, reset/evicted as the cursor moves.
- The one-page `LayoutSink` scratch (current `ParsedText`, the single in-flight `Page`).

Fixed reserved region, reset per page turn → deterministic footprint, **no per-turn heap
churn, no fragmentation**. This directly removes the F failure mode (inflate-ring OOM from a
fragmented heap): the hot path no longer heap-allocates per turn, and the ~32 KB inflate
ring is only touched by the background compile pass (which is arena-gated and off the
render path).

## 6. Feasibility risk + the gate

The one thing that must be proven on-device before we delete section files: **is live
one-page layout fast enough to turn pages without perceptible lag on our heaviest books?**
Microreader can (its layout is lean); our per-word measure path is heavier. So:

- **Gate step (build first, measure, then commit to deletion)**: implement `LayoutSink`
  one-page mode + a minimal `ContentReader` over an already-compiled content.bin, and
  measure **ms/page** for forward and backward turns on King's Avatar and Small Gods
  (our known heavy/pathological books) at real settings, arena-backed.
- Target: a page turn's layout must be comfortably under the e-ink refresh it triggers
  (~a few hundred ms budget; a HALF refresh is ~1.7 s on X4, so layout ≪ that is fine).
  Pre-render of the next page (we already do this) hides forward-turn latency entirely; the
  risk is backward turns and jumps, which can't be pre-rendered.
- If a pathological spine is too slow live, the fallback is a **bounded per-spine page-start
  index** (cursor positions of page boundaries, tiny) cached in the arena for the current
  spine only — NOT a persisted per-settings page store. Decide only if measurement demands it.

## 7. Proposed sequencing (each step host-green + device-buildable, flag-gated)

1. **G0 — this doc + sign-off.** (here)
2. **G1 — block-offset table**: bake a per-spine `blockCount × (u32 fileOffset + u32 charOffset)`
   table into each spine's aux region (writer: capture `file->position()` at each `onBlock`;
   write the table in `onSpineEnd` after chapters; bump v6→v7). Reader: `openSpine` bulk-loads
   it; add `seekToBlock(i)`. Host test: seek-to-block-N == sequential-read-to-N; round-trip.
3. **G2 — `LayoutSink` one-page mode**: lay out one page from `{blockIndex, wordOffset}`,
   report end position; backward layout for prev. Host test vs the existing whole-spine
   pagination (page K from cursor == page K from full run — byte/'position-identical').
4. **G3 — `ContentReader`**: windowed arena-backed random-access spine reader. Host test:
   window hits/evictions, random access == sequential.
5. **G4 — device gate**: measure ms/page on King's Avatar + Small Gods. GO/NO-GO on full
   deletion. (This is the decision point; nothing deleted before it passes.)
6. **G5 — reader read loop**: `PagePosition` navigation on live layout; background compile
   pass (Option-2 shape, no tee); frontier hand-off. Behind `EPUB_STAGE1`.
7. **G6 — delete section files**: remove the page store + variant cache + `loadPageFromSectionFile`
   once G5 is the sole read path and G4 proved the latency. Flip `EPUB_STAGE1` default when
   device-validated on the full corpus.

## 8. Open questions for sign-off

- ~~**Block-offset table location**~~ — RESOLVED 2026-07-27: bake into content.bin aux (v7).
  This is microreader's actual approach (baked descriptor table, one bulk read on open); the
  "scan on openSpine" alternative would be a full-spine read at open. Version bump is cheap
  (rebuildable cache → auto-recompile on mismatch).
- **Window size N**: start with microreader's spirit (small, ~a page + lookahead), tune at G4.
- **Backward-layout cost**: microreader re-lays-out a page backward each prev-turn. Confirm
  at G4 that's acceptable for us, or cache the current spine's page-start cursors in the arena.
