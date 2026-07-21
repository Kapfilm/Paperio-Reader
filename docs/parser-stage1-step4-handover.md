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

## Verification status — IMPORTANT, do this first on the next machine

- **Syntax-only checks PASS** on this Windows host (clang `-fsyntax-only`, pipeline include
  set, `-DEPUB_STAGE1=1`): `ContentSink.cpp`, `Stage1DumpMain.cpp`, `PipelineRunner.cpp`,
  `ContentSinkTest.cpp` all clean.
- **The existing `content_bin` unit test still builds + passes** (5/5) — the WBC1
  serialization foundation ContentSink builds on is green.
- **Full `EpubPipelineTest` / `content_stage1_dump` were NOT built/run here.** This Windows
  `build/` dir hits a PRE-EXISTING `lib/TJpgDec/src/tjpgd.h` typedef conflict (`_WIN32`
  branch redefines `uint32_t` as `unsigned long`, clashing with `<stdint.h>`), which already
  broke the pre-existing `epub_pipeline_dump` target before step 4. It is a
  Windows-clang-only toolchain wart; CI (Linux) uses the `#else` real-`<stdint.h>` branch and
  is unaffected.

**Next session, on Linux (or CI-style):**
```
cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/test --target EpubPipelineTest content_stage1_dump
ctest --test-dir build/test --output-on-failure -R ContentSink
./build/test/epub_pipeline/content_stage1_dump test/epubs/test_headings.epub   # eyeball the dump
```
Expect: all `ContentSink.*` tests green, existing `EpubPipelineTest` goldens still green
(the fused path is untouched — `stage1Sink_` is only attached by the new driver), determinism
test byte-identical. If the split-at-write test's per-record byte estimate is off, reconcile
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
