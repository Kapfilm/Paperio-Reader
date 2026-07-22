# Handover — Phase 3 step 4: ContentSink + content.bin writer

Pick this up to continue the compiled-content migration. Step 4 (the `ContentSink`
consumer + `content.bin` production + dump tool + determinism check) is **implemented but
not yet host-verified end-to-end on this machine** — see "Verification status" below. This
is the design-doc step 4 of `docs/stage1-extraction-design.md`; the master plan is
`docs/compiled-book-pipeline-plan.md` (Phase 3).

Branch: `feat-stage1-extraction`. Companion docs: `stage1-extraction-design.md` (the 6-step
sequence), `parser-stage1-handover.md` (step 2c mission), `compiled-content-format.md` (WBC1).

## Where the sequence stands

The design doc's 6-step incremental sequence:

1. Flag + `BlockSink` seam — DONE (committed earlier).
2–3. Additive producer: text/headings/anchors/images/lists/tables/footnotes + transcript
   fixes — DONE (commits `2ae9d8b0` … `75a6e0e6`).
4. **`ContentSink` + `content.bin` writer + dump tool + determinism — THIS STEP (just
   committed, pending full host test run).**
5. `LayoutSink` implementing `BlockSink`, byte-identical vs the fused path — NOT STARTED.
6. Unify + extract `HtmlWalkCore` — NOT STARTED.

## What step 4 added (this commit)

- **`lib/Epub/Epub/content/ContentSink.{h,cpp}`** — a `compiled::BlockSink` that fills one
  `CompiledContent` across a whole book. Compiles unconditionally (inert on device: nothing
  there constructs one). Responsibilities:
  - `beginSpine()` opens a `SpineContent`; the producer's `onSpineEnd()` closes it.
  - `onBlock`: `internStyle()` → stamp `Block::styleId`; capture `firstCharOffset` from the
    first block; append. TEXT blocks go through **`appendTextSplit`** — the 8 KB
    split-at-write (`kMaxSerializedBody = 8192`), tagging continuation records with
    `BlockFlags::kContinuation` (the flag the "Scaffold" commit reserved) and advancing
    `charOffset` by the codepoints each run consumes. Image/Table blocks pass through whole.
  - `onAnchor`: `{id, blockIndex = current block count, charOffsetInBlock = 0}` (block
    granularity — matches the producer's documented model).
  - `onChapter`: `{spineIndex, blockIndex = blocks.size()-1, level, title}` (heading block
    was emitted immediately before, per `stage1FlushBlock` order).
  - **Deferred (documented in-code):** `onFootnote` is ignored (footnotes get their own
    content.bin scan in master-plan Phase 3 step 4); `onPageBreakLabel` is recorded in
    `labels()` in memory but NOT serialized (WBC1 has no label table yet; folds in with
    Phase 4). This keeps the writer to the already-tested WBC1 format — no format/version bump.

- **`test/epub_pipeline/PipelineRunner.{h,cpp}`** — added `compileContent(...)`: the Stage-1
  driver. Loads the `Epub`, iterates all spines, attaches one shared `ContentSink` via
  `Section::setStage1Sink`, builds each spine at the default `Profile` (a build must run to
  drive the producer, even though the producer is settings-independent).

- **`test/epub_pipeline/Stage1DumpMain.cpp`** + `content_stage1_dump` CMake target
  (`-DEPUB_STAGE1=1`): compiles a book → `ContentSink` → `writeContentBin` → reads back →
  prints a canonical text dump. Usage: `content_stage1_dump <book.epub> [content.bin] [cacheDir]`.

- **`test/epub_pipeline/ContentSinkTest.cpp`** (added to `EpubPipelineTest`, which now also
  gets `-DEPUB_STAGE1=1`): round-trip vs the built model, split-at-write, byte-identical
  determinism, image-block compile.

- **`test/epub_pipeline/CMakeLists.txt`**: added `CompiledContent.cpp` + `ContentSink.cpp` to
  `PIPELINE_SOURCES` (neither was there — `CompiledContent.cpp` was only linked into the
  `content_bin` unit test), added `ContentSinkTest.cpp` to `EpubPipelineTest`, set
  `EPUB_STAGE1=1` on it, and added the `content_stage1_dump` target.

## Verification status — DONE (host-verified 2026-07-22)

Step 4 is now **host-verified end-to-end** on the Windows/MSYS2 (UCRT64, gcc 16.1) host:

- **`content_stage1_dump` + `EpubPipelineTest` built clean** (gcc, Ninja, `-DEPUB_STAGE1=1`).
- **ContentSink tests: 4/4 green** — `RoundTripsThroughContentBin`,
  `SplitsOversizedTextBlockAtWrite`, `ProducesDeterministicContentBin`, `CompilesImageBlocks`.
- **Stage1Producer: 9/9 green**; **full synthetic-corpus goldens: green** across `ColdRunsAreDeterministic`,
  `WarmRunMatchesColdRun`, `MatchesGolden` (fused path untouched — `stage1Sink_` only attached by
  the new driver). Zero real failures (the only ctest "failures" were `_NOT_BUILT` placeholders for
  targets deliberately not built).
- **Dump eyeballed** on `test_headings.epub`: `styles=4 spines=1 chapters=7`, headings interned to
  style 1 with `flags=1`, body to style 2, `char` offsets advancing by codepoint count
  (0→32→206→238). Coherent and correct.

### The tjpgd.h toolchain wart — FIXED

The prior session couldn't build here because `lib/TJpgDec/src/tjpgd.h` gated its manual
`uint32_t`/`int32_t` typedefs on `#if defined(_WIN32)` — but MinGW/MSYS2 defines `_WIN32` *and*
ships a real `<stdint.h>`, so the manual typedefs conflicted with the standard ones. The upstream
comment said the branch was for "VC++ or some compiler **without** stdint.h", so the `_WIN32` guard
was simply wrong for MinGW. Fixed the guard to `#if defined(_MSC_VER) && (_MSC_VER < 1600)` (only
truly old MSVC lacks `<stdint.h>`); every other toolchain (embedded, MinGW, MSVC 2010+) takes the
real `<stdint.h>`. One-line change to a vendored file; CI (Linux) was already on the `<stdint.h>`
branch and is unaffected. This unblocks *all* Windows pipeline targets, not just step 4.

**To reproduce the verification (Windows/MSYS2 or Linux):**
```
cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/test --target EpubPipelineTest content_stage1_dump
ctest --test-dir build/test --output-on-failure -R "ContentSink|Stage1Producer|EpubPipeline"
./build/test/epub_pipeline/content_stage1_dump test/epubs/test_headings.epub 2>/dev/null | grep -v '^\[DBG\]'
```
If the split-at-write test's per-record byte estimate ever drifts, reconcile
`kWordRecordBytes`/`kTextRecordOverhead` in `ContentSink.cpp` with `writeWords`/
`writeContentBin` in `CompiledContent.cpp` (they must mirror the real serializer).

## Watch-outs / open items for step 5

- **Split-at-write byte accounting is an estimate**, deliberately conservative (soft bound on
  read-time alloc, not an exact budget). `appendTextSplit` always emits ≥1 word per record so
  a single oversized word still progresses. No corpus book currently exceeds 8 KB/block, so
  the split path is exercised only by the synthetic 2000-word test — keep that test.
- **charOffsetInBlock is always 0** (block granularity), matching the producer. Mid-block
  anchor precision is a later refinement (noted in the producer too).
- **Table blocks are never split** (a >8 KB single table is an unhandled follow-up; no corpus
  book hits it — flagged, not solved).
- **Step 5 (`LayoutSink`)** is where the golden-equivalence gate actually bites: route today's
  measure+paginate through `BlockSink::onBlock` and diff section dumps vs the fused path across
  the settings matrix. ContentSink and LayoutSink then both consume the same producer stream.

## Not touched (scope guard held)

Shipping/device path, `EpubReaderActivity`, `Section` output, WBC1 format/version, goldens.
The `freeink-sdk` submodule shows modified in `git status` but that is pre-existing (present
at session start) and unrelated to step 4 — left unstaged.
