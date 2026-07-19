# Compiled-Book Pipeline Migration Plan

Status: PROPOSED (not started)
Scope: `lib/Epub/**`, `src/activities/reader/EpubReaderActivity.cpp`, `test/**`

## Goal

Restructure the EPUB compilation pipeline into two stages, adopting the strongest
ideas from microreader and FreeInkBook while preserving witchhunt-only features
(footnote previews, multi-language hyphenation, TOC reliability, sentinels,
variant eviction):

- **Stage 1 (content compile, settings-independent):** one streaming pass per book
  producing style-resolved paragraph/block records, anchor table, image refs with
  pre-probed dimensions, char-offset progress table. Never invalidated by font,
  margin, hyphenation, or bionic changes.
- **Stage 2 (pagination, settings-dependent):** per-chapter page caches built
  lazily/in background from the Stage-1 artifact, keyed by a single generation
  hash. No ZIP / XML / CSS work in this stage.

Cross-cutting: ZIP central-directory fingerprint as the cache key; bump-arena
allocation with `mark()`/`release()` replacing the per-site heap-gate `#define`s.

## Equivalence definition

A phase is DONE only when all three gates pass:

| Gate | Measure | Pass criterion |
|---|---|---|
| Functionality | Golden layout dumps (per book × settings variant), full ctest suite, device smoke script | Zero golden diffs (or whitelisted with written rationale); all existing tests green; smoke script clean |
| Speed | Host benchmark suite (`BENCHMARK` line format, µs) + device timings via `EpubReaderBenchmark` | Cold open ≤ baseline +10 %; warm open ≤ baseline; page turn ≤ baseline; settings-change re-layout: Phase 3 target ≥ 3× faster |
| Memory | Host malloc-interception peak (`test/epub_benchmark` mechanism), arena `highWater()`, device free/contig heap at checkpoints | Peak ≤ documented per-phase budget; no heap-recovery restarts across the corpus; no ASAN findings |

### Test corpus

- Synthetic: the 10 EPUBs in `test/epubs/` plus the `scripts/generate_*_epub.py`
  generators (large-CSS, kerning/ligature, br-section-break, spine/TOC edges).
- Real books (device + host): Moby Dick fixture (`test/fixtures/moby-dick/`),
  one Calibre-converted book with `filepos` footnotes, the known 123 KB-CSS book,
  one huge-single-spine-chapter book, one image-heavy book, one non-Latin
  (hyphenation-exercising) book. Store under `test/corpus/` (gitignored, listed
  in a manifest with SHA-256 so runs are reproducible).

---

## Phase 0 — Equivalence harness (prerequisite, zero behavior change)

The real pipeline cannot be validated today: `Section.cpp` / `ParsedText.cpp`
never build on host — `test/layout_quality/` only mirrors their logic. This
phase builds the witchhunt equivalent of FreeInkBook's `fibcheck`.

1. **Host-build the real pipeline.** New `test/epub_pipeline/` target linking the
   actual `Section.cpp`, `ParsedText.cpp`, `ChapterHtmlSlimParser`, `CssParser`,
   `blocks/`, `hyphenation/`, `FootnotePreviews`, `EpubImageManifest` against
   expanded `test/shims/` (HalStorage→host FS exists; add a deterministic
   text-measurement shim for the GfxRenderer seam — fixed advance table or a
   dumped real-font metrics table, checked in).
2. **Golden-dump tool** (`epub_pipeline --dump`): compile a book at a given
   settings profile and emit a canonical text dump — per chapter: page count;
   per page: line breaks, word x/y placement, style flags, anchors; plus
   footnote entries and image-manifest entries. Commit goldens for the synthetic
   corpus; real-book goldens live beside the corpus manifest.
3. **Determinism check:** two consecutive runs must produce byte-identical
   section `.bin` files and dumps. (If not, fix nondeterminism first — nothing
   downstream is testable otherwise.)
4. **Host metrics baseline:** reuse the malloc-interception + `BENCHMARK` output
   from `test/epub_benchmark/EpubParserBenchmark.cpp` to record, per corpus
   book: compile µs/chapter, peak heap/chapter, artifact bytes on disk.
5. **Device baseline:** scripted run per corpus book recording — cold open ms,
   warm open ms, `EpubReaderBenchmark` forward/backward page-turn ms,
   free + contig heap at four checkpoints (post-open, mid-section-build,
   post-build, steady reading), count of heap-recovery restarts. Save as
   `docs/pipeline-baseline-<date>.md`.

Exit gate: baseline document committed; determinism check green; goldens committed.

## Phase 1 — Content-fingerprint invalidation

Replace the path-hash-only cache key (`Epub.h:56-59`) with a fingerprint of the
ZIP central directory (offset+size+CRC roll over entry records — the EOCD scan
already runs at open, cf. commit `4ff23fbb`).

Steps: compute fingerprint at open; store it in a `fingerprint.bin` sidecar in
the cache dir (deviation from the original book.bin-header idea: identical
semantics, much smaller diff — no header-offset arithmetic or version bump, and
pre-fingerprint caches are adopted on first sight instead of mass-invalidated
on firmware upgrade); mismatch ⇒ treat whole cache dir as stale (delete +
rebuild). Keep per-artifact versions for now. The central-directory walk MUST
be buffered (BufferedFileReader): unbuffered it is ~12 FsFile calls/entry at
~1.5 ms each on device — seconds for a 1000-entry book, vs a handful of 4 KB
sequential reads buffered.

- Functionality: goldens unchanged (no layout change). New unit tests:
  (a) same path, different content ⇒ stale detected; (b) mtime-only touch ⇒
  cache retained; (c) fingerprint survives cache round-trip.
- Speed: fingerprint cost at open measured on device; budget ≤ 50 ms for a 5 MB
  EPUB (it must piggyback on the existing central-directory scan, not add one).
- Memory: streaming CRC only — assert zero new peak in host harness.
- User impact: one-time full cache rebuild for existing users (identical to a
  `SECTION_FILE_VERSION` bump; note in release notes).

## Phase 2 — Arena allocation for the section-build path

Replace the ~dozen `*_MIN_FREE_HEAP_BYTES` gates (`EpubReaderActivity.cpp:129-196`,
`Section.cpp:79-94`) with two arenas: a **parse-scratch arena** (inflate ring +
parser buffers; sized to the released-FB budget, smaller resident profile) and a
**layout arena** (blocks/pages working set), deliberately split so they need not
be contiguous (FreeInkBook `ChapterLayout.h:125-134` pattern).

Steps:
1. `BuildArena` class: bump alloc, `mark()`/`release()`, `reset()`, `highWater()`,
   `failedAllocSize()`. Standalone unit tests first.
2. Convert allocation sites incrementally — one site (or one build phase) per
   commit, old path retained behind `-DEPUB_BUILD_ARENA=0/1` for A/B.
3. Delete superseded heap gates only after the whole path is converted and
   device-validated.

- Functionality: goldens must be **byte-identical** (allocation strategy, not
  logic, changes). Full corpus through the ASAN host build (`test/build-asan`).
- Memory: host — assert `highWater()` ≤ arena budget for every corpus book;
  budgets become named constants replacing the gate defines. Device — log
  `highWater()`/`failedAllocSize()` at the existing checkpoints; expect zero
  heap-recovery restarts and zero mid-build realloc failures on the corpus
  (both currently observed, cf. `967f8680`).
- Speed: host benchmark neutral-to-faster (bump alloc beats malloc); device
  chapter-build ms within ±5 % of baseline.

Exit gate: A/B flag flipped to arena-default; gate defines removed; baseline
doc updated with new memory numbers.

## Phase 3 — Stage-1 compiled content format

New per-book artifact `content.bin` (versioned magic `WBC1`), produced by one
streaming pass, evolving the existing `html_<spine>.bin` idea (raw XHTML cache,
`Section.h:87-89`) into fully parsed, CSS-resolved records:

- Style-resolved paragraph/block records, serialized size capped at 8 KB each
  (split at write time — microreader `MrbConverter.cpp:53-68` rationale: read-time
  memory safety enforced by shaping the write).
- Anchor table (id → record), image refs with pre-probed dimensions, char-offset
  progress table, chapter table. Auxiliary tables streamed through temp files
  during the pass, spliced at finish (never held in RAM).

Steps (each its own commit series, old path kept behind `-DEPUB_STAGE1=0/1`):
1. Format spec written first, in this doc's sibling `docs/compiled-content-format.md`.
2. Writer: extraction pass runs `ChapterHtmlSlimParser` + CSS resolution once,
   emits records into `content.bin`.
3. Reader API consumed by Section build phase (b) in place of XHTML + CssParser.
4. Footnote gather rewritten as a scan over `content.bin` (drops its ZIP pass).
5. Image manifest folded into the compiled records; `images.bin` retired.
6. `html_<spine>.bin` retired.

- Functionality — **the critical gate:** for every corpus book × a settings
  matrix (default, large font, hyphenation on/off, bionic, narrow margins),
  golden dumps from old path vs new path must be identical. Any diff is either
  a bug or a whitelisted, documented intentional change. Plus: footnote cache
  old-vs-new entry-set equality; anchor navigation tests (TOC jump, in-book
  links) on device smoke script.
- Speed — host: (a) first compile ≈ baseline first build (same work, different
  serialization); (b) re-layout after settings change: **target ≥ 3× faster**
  (no ZIP/XML/CSS); device: settings-change re-pagination ms, cold/warm open ms.
- Memory — Stage-2 peak must drop (no expat/CSS working set during pagination);
  assert via layout-arena `highWater()` per corpus book, publish before/after.
- Storage — SD bytes per book vs current `html_ + sections + images.bin`;
  budget: ≤ 1.5× the source EPUB size, flag if exceeded.

## Phase 4 — Generation hash + build-orchestration simplification

1. Replace the `SECTION_FILE_VERSION` + `propertyHash` pair with one
   `layoutGenerationHash` (geometry, font id+size, spacing, CSS content hash,
   hyphenation language/state, bionic, `kLayoutRevision` with an in-code
   changelog — FreeInkBook `PageCache.cpp:74-97` pattern). Variant eviction
   (max 5, LRU) unchanged, now keyed on the hash.
2. Partial-cache footer (suspend/resume) so an interrupted background build
   serves finished pages immediately on reopen instead of restarting.
3. Collapse the A/B/C scheduler where Stage-1 makes it possible: C (blocked-on
   build) and B (lookahead) become the same incremental session with different
   page budgets; re-evaluate whether the resident/released dual-floor logic is
   still needed at all once builds no longer parse XML.

- Functionality: settings-invalidation matrix test — toggle each setting, assert
  cache invalidated *iff* it should be (catches both stale-cache and
  over-invalidation); interrupted-build test — power off mid-build (device) /
  kill mid-build (host), reopen, assert pages served + build resumes.
- Speed: device page-turn latency distribution (p50/p95 over the scripted
  `EpubReaderBenchmark` run) vs baseline; pre-render hit rate must not regress
  (cf. the fc4a79ce stale-hit incident — re-run that regression scenario).
- Memory: steady-state free/contig heap during background build ≥ Phase-2 numbers.

## Reporting

After each phase: update `docs/pipeline-baseline-<date>.md` with the same table
(per corpus book: cold/warm open, page turn p50/p95, compile µs, peak heap,
artifact bytes) so the whole migration has a continuous, comparable record.

## Explicit non-goals

- No change to live rendering (GfxRenderer), fonts, or display drivers.
- No PC-side compilation (microreader precedent: on-device keeps the SD card
  drop-a-file workflow intact).
- No new user-facing features during the migration (scope discipline).
