# Increment E — content.bin as a background producer the reader consumes concurrently (design, 2026-07-26)

## Why E supersedes the D wiring

D-4/D-4b (commits `63f5f2f6`, `fde3569e`) wired the content.bin read-back into the reader, but on-device
measurement (King's Avatar + Small Gods, caches cleared, `EPUB_STAGE1=1`) proved it is **dormant**: a
**chicken-and-egg deadlock**. The one-time compile ran only on the *blocking* build route, but normal
reading always takes the *Background-C incremental* route, so `compileBookToContentBin` never fired →
content.bin was never written → every read-back missed → every spine parsed. No regression (parse
timings and heap identical to master), but zero benefit.

The reframe (user, 2026-07-26): **content.bin is not a lazy side-cache. It is THE compiled representation
the whole reading/rendering cycle builds from.** A **non-blocking background compiler (producer)** writes
it; Stage-2 read-back → layout → render (consumer) kicks off per spine **as soon as that spine is
written**, while later spines are still compiling. The consumer chases the producer's write frontier.

This dissolves the deadlock: the compile no longer piggybacks on a build path normal reading avoids — it
is its own first-class background pass, started at book-open, driven by read position.

## The three decisions that fix the architecture (user, 2026-07-26)

1. **Reader outruns the compiler** (needs spine N not yet produced) → **prioritize N in the compiler**:
   reorder the compile queue so N is produced next, then read-back from content.bin once it lands.
   Everything flows through the single content.bin producer (not a parallel parse fallback).
2. **Scope** → **rolling window ahead of the reader**: keep current + a few spines ahead compiled,
   advancing as the reader moves. content.bin is **partial by design**. Re-opens far from the last
   position re-compile the new neighbourhood.
3. **Process** → **design doc first, sign-off before code** (D-4's blocking-only placement was wrong;
   lock the design this time).

**Unifying insight:** #1 and #2 are one mechanism — **read position drives compile order.** The producer
services the reader's current/next spine first (priority jump), and fills a rolling window ahead as its
idle-time default. It is NOT a linear 0..N walk.

## What already exists that E reuses (do not rebuild)

- **Background-B pre-build state machine** — `EpubReaderActivity` already runs a rolling-window
  pre-builder: `backgroundBuildState_` (Probe → WaitHeap → build-slice → Settled), cursor
  `backgroundBuildSpineIndex_` walking forward from `currentSpineIndex+1`, gated by
  `BG_BUILD_LOOKAHEAD_PAGES` runway ahead of the reader (`EpubReaderActivity.cpp:840-891`). This is
  ALREADY "rolling window ahead of reader, read position drives it." Today it produces **section-cache
  files** (settings-dependent, `sections/<spine>_<hash>.bin`). **E turns its product into content.bin**
  (settings-independent) — the window logic, the heap gate, the per-tick slicing all carry over.
- **`compileBookToContentBin`** — whole-book one-pass walk with a `ContentBinWriter` via `setStage1Sink`.
  E needs it **sliced + reorderable** (per-spine, priority-jumpable), not one-shot.
- **`buildSectionFromContentBin`** — per-spine read-back (run-to-completion today; the 64 KB cap in
  D-4b is a stopgap). E's consumer. Slicing it lifts the cap (was a deferred D follow-up).
- **WBC1 v5 content.bin** — already random-access: fixed header + back-patched per-spine offsets + style
  pool + spine index + chapters. A partial file with only some spines' offsets committed is already a
  valid shape to read committed spines from.
- **`replaySpine`**, **`LayoutSink`**, **`ContentBinWriter`/`BlockStreamReader`** — unchanged.

## Producer / consumer / frontier — the core state

### Producer (the background content.bin compiler)
A background pass, sliced on the loop task under a budget (like `stepSectionBuild`), that:
- Maintains a **compile queue** ordered by read position: `[reader-current, reader-next, …window…]`.
- Compiles one spine's content-only records (walk → `ContentBinWriter`, no LayoutSink pages,
  `skipEviction=true`) into content.bin, appends the spine's blocks, then **commits its offset into the
  index** (the commit is the frontier advance — see atomicity below).
- Advances through the rolling window; **re-sorts the queue toward the read position** whenever the
  reader moves (priority jump for decision #1).
- Stops when the window ahead of the reader is satisfied (mirrors `BG_BUILD_LOOKAHEAD_PAGES`).

### Consumer (the reader)
On a section-build request for spine N:
- If content.bin **covers N** (offset committed, fingerprint matches) → `buildSectionFromContentBin`
  (read-back) → layout → render. No ZIP/XML/CSS.
- If content.bin does **not** cover N (reader outran the frontier) → **decision #1: prioritize N in the
  producer** (move N to the queue head, wait for it to land), then read-back. The existing per-spine
  parse (Background-C) remains as the **last-resort** fallback (producer wedged / disabled / flag off).

### Frontier handshake
- The **committed spine set** in the content.bin index IS the frontier. The consumer reads it; the
  producer advances it. One writer (producer), one reader (consumer) — both on the loop task (see
  concurrency below), so the handshake is cooperative, not preemptive.
- "Prioritize N" = producer API `requestSpineNext(N)` that re-heads the queue; the consumer polls
  "is N committed yet?" across ticks (it already yields via the sliced build loop).

## The sharp open question — buffer & arena ownership

**This is the design's hardest constraint and must be nailed before code.** Today a Background-C build
**BORROWS the ~52 KB secondary display buffer** as its build arena (`borrowSecondaryBuffer` →
`BuildArena`, `EpubReaderActivity.cpp:2778-2785`); the borrow is single-owner for the duration of one
spine's build. E has **two** concurrent Stage-1 activities that each want working RAM:
- the **producer** (walk + ContentBinWriter: peak ~one logical block, plus the inflate ring ≤32 KB during
  extraction), and
- the **consumer** (read-back LayoutSink: one page + one text block; image decode releases/reallocs the
  buffer per-image as today).

They cannot both own the borrowed buffer. Options for the doc to decide (leading candidate first):
1. **Serialize on the loop task, single arena, time-sliced.** Producer and consumer never run in the
   same tick — the loop runs one slice of one of them per tick, arbitrated by "consumer (reader) always
   wins when it needs a spine; producer fills otherwise." One borrowed-buffer arena, handed to whichever
   is slicing. Simplest; matches the existing single-task model; no true parallelism but the producer
   runs in the reader's idle gaps exactly like Background-B does now.
2. Producer uses **heap arena**, consumer keeps the borrowed buffer. Doubles RAM pressure; risks the
   <8 KB-min-free zone the borrow model was built to avoid. Reject unless (1) starves the producer.
3. Give each its own budget on separate ticks with explicit arena hand-off. More states, more failure
   modes. Only if (1) proves too coarse.

Recommendation for sign-off: **(1)** — one loop task, one arena, consumer-priority arbitration; the
"background producer" is literally the Background-B slot repurposed to emit content.bin. This keeps E
inside the proven single-task, borrow-one-buffer memory discipline.

**DECIDED (user, 2026-07-26): Option 1 — one loop task, one borrowed-buffer arena, consumer-priority.**
The reader (consumer) always wins the arena when it needs a spine; the producer runs only in the
reader's idle ticks. No producer-on-heap, no separate-tick arena hand-off. This is the memory contract
for all E sub-steps.

## Format change — v6 per-spine self-contained styles (DECIDED 2026-07-26)

The v5 format is whole-book-at-`finish()`: the shared **style pool**, **spine index**, and **chapters**
are appended only at `finish()`, and the header offsets back-patched then. Blocks reference styles by
index into a book-GLOBAL pool that grows as later spines intern new styles. A consumer therefore cannot
read spine N until the whole book is finished — fatal for E's readable-while-growing frontier.

**Decision (user, 2026-07-26): per-spine self-contained styles → WBC1 v6.** Each spine carries its OWN
local style table, written inside that spine's section (at `onSpineEnd`, alongside the anchors/labels
aux tables). A committed spine is then fully self-describing and replayable the instant its offset
commits — zero dependency on any later-written book-global structure. Consequences:
- `internStyle` interns into a PER-SPINE pool (reset at `beginSpine`), not `stylePool_` (book-global).
  Block `styleId` is a local index; the reader loads the spine's style table from its section before
  streaming its blocks (same seek-aux-first pattern as anchors/labels).
- Styles that recur across spines are duplicated on disk. Bounded and small (a spine's distinct styles
  are few); acceptable for the clean producer/consumer semantics. Measure total content.bin size on
  device (still target ≤ 1.5× source).
- The book-level **spine index** and **chapters** still need a commit path the consumer can read
  mid-compile — see atomicity below; with self-contained spines the index only needs each spine's
  offset, committed slot-by-slot, and chapters can be per-spine too (a spine already knows its own
  chapter entries at `onSpineEnd`).
- v5 → v6 is a clean break: no device has shipped content.bin, no migration. Host tests move to v6.

## content.bin atomicity (partial file the consumer can trust)

- The producer appends a spine's blocks, THEN writes its offset into the index; the consumer treats a
  spine as "covered" ONLY once its index offset is committed and non-zero. A crash mid-append leaves an
  uncommitted tail the consumer ignores (offset still 0) — self-healing, same principle as
  `BlockStreamReader::open` rejecting unfinished files today.
- Index-commit must be a single small write (offset slot back-patch) that is atomic enough on the SD FS
  that a torn write can't present a bogus non-zero offset. Spell out the exact write + flush order in
  implementation; first cut may re-verify the spine's block tail on read (cheap) before trusting it.
- Fingerprint (v5 header) gates the whole file; a source change invalidates content.bin wholesale
  (KEEP — never settings-keyed).

## Failure / disable / flag-off

- `EPUB_STAGE1` off → producer never starts, consumer never runs; the reader is **exactly** today's
  Background-C path (bit-for-bit; already true for D-4b and must stay).
- Producer disabled/failed at runtime → consumer misses → last-resort per-spine parse (Background-C).
  The reader never blocks on a wedged producer.
- Low heap → producer yields (WaitHeap gate, reused) so it never competes with the reader for the
  headroom a build needs.

## Host gate (before any device flash)

- **Reuse `SectionEquivalence`**: a spine built via read-back from a (partial) content.bin is
  byte-identical to the parse — already proven for D; extend to a **partially-committed** content.bin
  (only some spines' offsets present) to prove the consumer reads committed spines correctly and treats
  uncommitted ones as absent.
- **Producer-order test (host)**: drive the producer with a scripted read-position sequence (forward
  read, a jump, a back-jump) and assert the compile order re-heads to the read position (decision #1)
  and the committed set matches the rolling window (decision #2). No device needed — the producer is a
  state machine over spine indices.
- **Flag-off bit-for-bit master** — unchanged parse path.

## Device measurement (after host gate green)

Flash `[env:default]` `-DEPUB_STAGE1=1`. Books: King's Avatar (1,732 tiny spines — the read-back
eligibility case that D-4b could not exercise), Small Gods (2 spines, one 584 KB — the read-back-slicing
stress once the cap is lifted). Capture vs master (flag off):
- **cold-open first page**: ≈ master (producer must not delay the first page — reader-current is compiled
  first, but page 1 may still parse on the very first tick; measure the gap).
- **forward-read page/section latency**: spines ahead should be read-back (fast) once the frontier leads
  the reader; log `served from content.bin` per spine and the read-back vs parse ms.
- **relayout after a settings change**: the ≥3× target — content.bin already exists, so relayout is pure
  read-back for every covered spine.
- **heap**: per-spine `arena highWater`, `Min Free` (≥ master, no `failedAlloc`), largest-contiguous
  across a page-through (no frag drift) — the two-activity model must not dip below the ~7 KB floor the
  current logs hold.
- **content.bin size**: rolling-window partial file ≤ 1.5× the covered spines' source.

Baselines already captured (flag-on, parse path, from the D-5 logs): King's Avatar spine0/1
`createSectionFile` 731/640 ms, `arena highWater` 12076/12668, `failedAlloc=0`, `Min Free` 7160; Small
Gods spine0 343 ms, spine1 (583991 B) sliced. Relayout target is ≥3× off these.

## Increment-E sub-steps (each small, host-gated)

1. **Slice + reorder the producer.** Turn `compileBookToContentBin` into a per-spine, resumable,
   queue-ordered producer (`stepContentBinCompile(spineIndex, budgetMs)` + a queue re-headed by read
   position). Host gate: producer-order test; each spine's committed records replay byte-identical.
2. **Slice the consumer** (`buildSectionFromContentBin`), lifting the D-4b 64 KB cap. Host gate:
   `SectionEquivalence` for a large single-file spine (Small Gods 584 KB).
3. **Frontier handshake**: index-commit atomicity + consumer "is N committed?" + producer
   `requestSpineNext(N)`. Host gate: partially-committed `SectionEquivalence`.
4. **Wire into the reader**: repurpose the Background-B slot as the content.bin producer; route section
   builds through the consumer with consumer-priority arena arbitration (buffer decision from the sharp
   question above). Behind `EPUB_STAGE1`. Gate: flag-off bit-for-bit; flag-on section cache
   byte-identical.
5. **Device measure** (table above). Then tune the window size / budget.

## Status

Design complete; **arena model DECIDED (option 1, consumer-priority).** User will review this doc before
implementation starts (design-doc-first, decision #3). Next code step when green-lit: sub-step 1 (slice +
reorder the producer). D-4b stays committed (dormant, no regression) — E gives its read-back a producer
to consume. See `stage1-incr-D-design-2026-07-26.md` (superseded wiring),
`stage1-revised-plan-v2-2026-07-26.md`.
