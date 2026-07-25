# Stage-1 extraction — 6e handover (2026-07-25)

Branch: `feat-stage1-extraction`. This document records where the compiled-book-pipeline
migration stands, why the remaining fused-layout deletion was **stopped** (functional goal met,
remaining work is delicate code hygiene), and exactly what a future session needs to finish it.

## TL;DR

- **The settings-split goal is achieved.** Since 6b the device path renders through the internal
  `LayoutSink` (Stage-2), driven by the Stage-1 `BlockSink` transcript. A font/margin/hyphenation/
  bionic change re-runs only Stage-2 (no ZIP/XML/CSS). `content.bin` is settings-independent.
- **Full suite 431/431 green**; synthetic corpus byte-identical vs the committed goldens.
- The fused inline-layout **math still exists in the parser but drives no device output** (its
  `emitPage` output is gated off by `if (!layoutSink_)`). It is dead-for-output, not dead-for-walk.
- Deleting that math is **not** a mechanical delete — see "Why we stopped" — so it was deferred as
  pure hygiene. Nothing functional depends on it.

## Commit series (this line of work)

```
0002a990 refactor(stage1): footnote anchor index from producer, not fused block (step 6e prep)
3673a38a test(stage1): regenerate <pre> golden to match post-flip sink output
c6f3c644 refactor(stage1): delete dead fused table + cell-image layout (step 6c, part 3)
d82c5d54 refactor(stage1): strip fused table layout + >96-word split (step 6c, part 2)
b10446b1 refactor(stage1): strip fused <hr> and <img> layout placement (step 6c, part 1)
38171822 feat(stage1): drive output through the internal LayoutSink; device path flipped (step 6b)
```

## Why we stopped (the load-bearing finding)

"Delete the fused inline-layout half + the layout state members" reads like a mechanical delete.
It is not. `currentTextBlock` and `currentPage` are **load-bearing walk state** — the HTML walk
reads them to make decisions, and the sink's own equivalents are NOT drop-in substitutes at those
decision points. Two independent probes proved this:

1. **`<pre>` blank-line desync.** Substituting the producer helper `stage1BlockIsEmpty()` for
   `currentTextBlock->isEmpty()` at the `<pre>` `\n` handler (cpp ~1887) **changed the golden**
   (dropped trailing-space words). Root cause: in `startNewTextBlock`'s empty-block reuse branch
   (cpp ~735-781) the fused `currentTextBlock` is **reused** (kept) while `stage1OpenBlock`
   **flushes + resets** `stage1Block_`. The two block lifecycles diverge exactly there, so their
   `isEmpty()` disagree on a `<pre>` blank line. Reverted; golden restored.

2. **Moby page-break SEGFAULT.** Making CSS `page-break-before` transmit `stage1PendingPageBreak_`
   unconditionally (dropping the fused `currentPage && !elements.empty()` guard at cpp ~1416)
   **segfaulted on Moby Dick**. Root cause: the unconditional `emitPage()` pushes a spurious
   `paragraphLutPerPage` entry on a leading/empty-page break, desyncing it from
   `completedPageCount`; later LUT indexing crashes. The **sink already** gates `kPageBreakBefore`
   on its own non-empty page (`LayoutSink.cpp:650`), so transmitting the intent is fine — but the
   **fused `emitPage` must stay guarded**. Restored to the original guarded form.

Both echo the earlier SEGFAULT from a mechanical `startNewTextBlock` strip (the walk dereferences
`currentTextBlock` at ~15 sites). Conclusion: `currentTextBlock`/`currentPage` cannot be deleted
wholesale.

## What 6e actually is (the safe shape, for whoever finishes it)

Keep `currentTextBlock` and `currentPage` as **lightweight walk bookkeeping** and delete only the
pure layout **math**:

- **Keep** (walk still needs them):
  - `currentTextBlock` as a word-accumulator — for `isEmpty()` (cpp 981, 1087, 1118, 1267, 1887,
    2215) and the block-style reads (`<br>` alignment cpp ~1486, margin-left span ~1767,
    empty-block alignment resets ~1088/1119/2216).
  - `currentPage`/`currentPageNextY`/`completedPageCount` — for the page-break-before guard
    (cpp 1416), the TOC-boundary `emitPage` guards (cpp 758, 794), and `paragraphLutPerPage`.
- **Delete** (pure layout math, drives no output post-6b):
  - `makePages()` line-breaking body (cpp 2520+), `addLineToPage` (2450+), float geometry in
    both, `resolveBlockFont` (2429), `effectiveLineHeight` (2446), `attachPendingFloatImage`
    layout half (467), the deferred-page-image machinery, and `emitPage`'s float/scratch upkeep.
  - The `if (!layoutSink_) completePageFn(...)` in `emitPage` is already effectively dead on the
    device path (layoutSink_ always set) — but it is LIVE for the **external ContentSink compile**
    path (content-only compile, no internal sink). Do NOT delete `completePageFn` without
    re-checking that path; the ContentSink compile still relies on the fused page production.
    **This is the sharpest remaining trap.** Grep `effectiveSink()` / `stage1Sink_` / `layoutSink_`.

Gate every increment on: the 12 goldens byte-identical + `ctest -E "NOT_BUILT|EpubParserBenchmark|EpubCssPerformanceTest|epub_pipeline_dump"` fully green (431 tests).

## Producer-side substitutes already available

Added in `0002a990` (`ChapterHtmlSlimParser.h`):
- `stage1BlockIsEmpty()` → `!stage1Block_ || stage1Block_->words.empty()`
- `stage1BlockWordCount()` → words in the current transcript block

Footnote anchor index already uses `stage1BlockWordCount()`. **Caveat proven above:**
`stage1BlockIsEmpty()` is NOT equivalent to `currentTextBlock->isEmpty()` at the `<pre>` reuse
site — do not blind-substitute it there.

## Build / test

```
cmake -S test -B build/test-msys -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/test-msys --target EpubPipelineTest CompiledContentTest SaxParserTest
ctest --test-dir build/test-msys -E "NOT_BUILT|EpubParserBenchmark|EpubCssPerformanceTest|epub_pipeline_dump"
```

The 3 excluded targets are Linux-only (`dlfcn.h`) — not a Windows coverage gap. To regenerate a
golden after an intentional change: `UPDATE_GOLDENS=1 build/test-msys/epub_pipeline/EpubPipelineTest.exe
--gtest_filter="*MatchesGolden*<stem>*"` (writes into `test/epub_pipeline/goldens/`).

## Remaining tasks (priority order)

1. **6d** — sever the walk-side renderer em→px uses (small, self-contained). Line numbers have
   shifted; grep the walk for `renderer.` em→px conversions.
2. **6e** — the safe fused-layout-math deletion described above (pure hygiene; the ContentSink
   compile path is the trap).
3. Optional — the residual Moby +4px heading-spacing sink bug (deferred; cosmetic, real-book
   back-matter only). It is a plain LayoutSink bug now that the fused reference is gone.

## Cross-refs

- `docs/parser-stage1-step6bc-plan.md` — the 6b/6c plan.
- `docs/stage1-test-status-2026-07-24.md` — test inventory + what's excluded and why.
- Memory: `stage1-currenttextblock-load-bearing`, `stage1-layoutsink-progress`,
  `stage1-settings-split-vs-microreader`.
