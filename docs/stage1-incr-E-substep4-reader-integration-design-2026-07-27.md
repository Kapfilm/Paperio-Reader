# Increment E sub-step 4 — wiring the producer + consumer into the reader (design, 2026-07-27)

Sub-steps 1–3 built and host-gated the pieces in isolation:
- **Producer** `ContentBinProducer` — sliced, read-position-ordered, durable per-spine commits (`begin`
  / `setReadPosition` / `step(budgetMs)` / `finish`, `spineAvailable` via committed slots).
- **Consumer** `Section::stepReadBackFromContentBin(params, budgetMs, skipEviction)` — resumable
  read-back; returns `More` / `Done` / `Failed` / `NotAvailable`; `NotAvailable` when the spine is not
  yet committed (frontier). `buildSectionFromContentBin` wraps it run-to-completion.
- **Handshake** `BlockStreamReader::refreshIndex()` — cheap re-read of the committed frontier.

Sub-step 4 makes them live in `EpubReaderActivity`. It is device firmware; the host suite does not
compile it, so the gate is a device build (flag on + flag off) plus the sub-step 5 measurement. The
overriding constraint: **flag off (`EPUB_STAGE1==0`) must be bit-for-bit the current reader.**

## Where the pieces attach (real code, current tick model)

`serviceBackgroundWork()` (ERS.cpp:661) runs each idle loop tick, in strict priority:
1. `runDeferredGrayscalePass()` — AA owed → return (display bus busy).
2. **Background-C**: `if (section->hasActiveBuild()) stepCurrentSectionBuild(); return;` — the section
   the reader is waiting on. Highest build priority: nothing to read until it makes pages.
3. **Background-B**: `stepBackgroundSectionBuild()` — look-ahead pre-build of spines ahead of the
   reader (`Probe → WaitHeap → Building → Settled`, one `stepSectionBuild` slice/tick, RESIDENT — no
   buffer borrow, hence its strict heap floors).

The blocking/first-open build path is `buildSection()` (ERS.cpp ~2700+): probe cache → if `needBuild`,
D-4b already tries `buildSectionFromContentBin` (size-capped) before choosing Background-C incremental
vs blocking.

This priority order IS the consumer-priority arbitration the arena decision calls for. The producer is
the LOWEST-priority background user; the consumer runs on the reader's critical path.

## The design

### 1. Producer lifecycle — a fourth, lowest-priority background phase

Add a `std::unique_ptr<compiled::ContentBinProducer> contentBinProducer_` member (behind `#if
EPUB_STAGE1`). 

- **begin**: lazily, on first need — the first time `serviceBackgroundWork` reaches the producer phase
  with no content.bin yet covering the book (fingerprint check). NOT in `onEnter` (keep cold-open
  cheap; the first page must not wait on a whole-book producer spin-up). `begin()` only opens the file
  + queues; it compiles nothing until stepped.
- **step**: a new lowest-priority phase at the END of `serviceBackgroundWork`, after Background-B:
  ```
  runDeferredGrayscalePass();            if (AA owed) return;
  if (hasActiveReadBack) { stepReadBack; return; }   // consumer — NEW, see §2
  if (section->hasActiveBuild()) { stepCurrentSectionBuild(); return; }   // Background-C
  stepBackgroundSectionBuild();          if (B did work this tick) return;  // Background-B
  stepContentBinProducer();              // producer — NEW, lowest priority
  ```
  The producer only runs on a tick where AA is idle, no read-back is active, no Background-C build is
  live, and Background-B had nothing to do. That is "producer fills the reader's idle gaps" — the
  consumer-priority contract, expressed purely by ordering (no locks beyond the existing `RenderLock`).
- **setReadPosition**: call `contentBinProducer_->setReadPosition(currentSpineIndex)` whenever the read
  position moves (same trigger as `resetBackgroundBuild`, ERS.cpp:837). Read position drives compile
  order (decisions #1 + #2).
- **finish/teardown**: `onExit` (book close) drops the producer (its dtor `finish()`es the file). A
  fingerprint mismatch on `begin` (book changed) starts fresh.
- **heap gate**: the producer is a RESIDENT content-only build (peak ~one block + inflate ring), so it
  reuses Background-B's WaitHeap floors before stepping. If heap is too tight it simply doesn't step
  this tick — same discipline as B. It NEVER borrows the secondary buffer (see arena, §3).

### 2. Consumer on the critical path — drive the sliced stepper, lift the cap

Replace D-4b's run-to-completion, size-capped call in `buildSection` with the **sliced** stepper, so a
large spine no longer blocks the loop and the 64 KB cap (`STAGE1_READBACK_MAX_INFLATED_BYTES`) is
removed:

- First entry for a spine: `stepReadBackFromContentBin(params, budgetMs=0-or-sliced, skipEviction)`.
  - `Done` → served from content.bin, fall through to the render tail (as D-4b does now).
  - `NotAvailable` → the producer has not committed this spine yet (or no content.bin). Two choices:
    **(a)** fall back to the existing Background-C/blocking parse immediately (never wait) — simplest,
    always responsive; **(b)** `setReadPosition(spine)` to prioritize it and let the read-back retry
    next tick. Per decision #1 the intent is (b), but (a) is the safe first cut — the parse still
    produces the page, and the producer will have content.bin ready for the NEXT visit / relayout. The
    first device cut does **(a)**; a follow-up can add the (b) prioritize-and-wait once (a) is proven
    to not regress cold-open.
  - `More` → hand control back like Background-C: the read-back becomes the active build; subsequent
    ticks pump it via a new `stepCurrentReadBack()` in the consumer phase (§1 ordering), showing the
    "indexing" popup exactly as an incremental parse does, until `Done`.
  - `Failed` → fall back to parse (remove partial handled by `abortReadBack`).
- The cap removal is the whole point of sub-step 2; it is only safe BECAUSE the read-back is now
  sliced. Keep a *sanity* upper bound only if a spine's read-back working set could exceed a parse's
  (it cannot — read-back is strictly lighter), so the cap is deleted, not just raised.

### 3. Arena / secondary-buffer ownership (the decided constraint)

Decision (signed off): **one loop task, one borrowed-buffer arena, consumer-priority.** Concretely:
- The **consumer** read-back, when it runs on the critical path for the section the reader is waiting
  on, may use the borrowed secondary buffer as its arena exactly like Background-C does today
  (`borrowSecondaryBuffer` → `BuildArena` → `setExternalBuildScratch`). It is the reader's foreground
  work, so it wins the buffer.
- The **producer** NEVER borrows the secondary buffer. It runs only in fully-idle ticks (§1 ordering)
  and builds resident against the reading heap, gated by the Background-B floors. If the reader needs
  the buffer (a navigation triggers a consumer read-back or a Background-C build), the producer simply
  doesn't get a tick until that finishes — no hand-off, no contention, because they never run in the
  same tick and the producer never holds the buffer across ticks.
- This keeps the whole feature inside the proven single-task, borrow-one-buffer discipline; there is
  never a moment where two Stage-1 activities both own working RAM.

### 4. Flag-off invariance

Everything above is under `#if EPUB_STAGE1`. With the flag off:
- no `contentBinProducer_` member, no producer phase in `serviceBackgroundWork` (the added lines are
  guarded), so the tick chain is exactly AA → C → B as today;
- `buildSection` keeps the D-4b structure, which already collapses to the plain parse when the flag is
  off (`servedFromContentBin` always false).
The device `env:default` builds with the flag OFF (measured earlier: 95.9% flash, bit-for-bit parse).
The flag-on build is the measurement target.

## Risks & how the design contains them

- **Cold-open regression** — the producer must not delay the first page. Contained: producer `begin` is
  lazy (not in `onEnter`) and only steps in idle ticks after B; the first section still builds via the
  normal path. Measure cold-open ms flag-on vs flag-off (must be ≈).
- **Heap pressure from a running producer** — contained by reusing Background-B's WaitHeap floors and
  never borrowing the buffer; the producer yields the tick under pressure.
- **Stray section-cache file from the content-only compile** — pre-existing quirk (`stepSectionBuild`
  opens `filePath` even in stage1-sink mode). Inherited from `compileBookToContentBin`; a separate
  cleanup, out of scope here (note it, don't fix it in sub-step 4).
- **Producer/consumer both wanting the reader's glyph renderer** — both run under `RenderLock` in the
  loop task, never concurrently; no new locking.
- **Consumer `More` on a huge spine mid-read-back while the user navigates away** — `abortReadBack`
  (dtor + explicit) drops the partial; the new target re-probes. Same lifecycle as an aborted parse.

## Sub-steps of the wiring (REVISED 2026-07-27 — "measure first, then decide")

Key realisation: D-4b's consumer call site already goes through the sliced stepper — sub-step 2 made
`buildSectionFromContentBin` a thin wrapper over `stepReadBackFromContentBin`. So "route the call
through the sliced path" is ALREADY DONE. A read-back is Stage-2 layout ONLY (no ZIP/XML/CSS), so it is
much faster than the parse it replaces; whether a big-spine read-back running to completion blocks the
loop unacceptably is an EMPIRICAL question. Decision (user): do NOT build the speculative cross-tick
pump; wire the producer + lift the cap with the read-back run-to-completion, and let the sub-step 5
measurement decide if the pump is needed.

4b. Lift the `STAGE1_READBACK_MAX_INFLATED_BYTES` cap: read-back runs to completion for any spine size.
    Justified because read-back is strictly lighter than the parse the reader already runs blocking on
    the same spine (no inflate ring, no Expat, no CSS parse). Device build flag-on/off green.
4c. Producer lifecycle: member + lazy `begin` + lowest-priority `stepContentBinProducer()` phase +
    `setReadPosition` on navigation + `onExit` teardown. Device build flag-on/off green.
4d. (deferred, build ONLY IF sub-step 5 shows a big-spine read-back blocks the loop too long) the
    cross-tick read-back pump: a `stepCurrentReadBack()` mirroring `stepCurrentSectionBuild`, showing
    page 0 fast (pages already stream to disk via onPageComplete during replay) and replaying the rest
    in idle ticks. Also (deferred) prioritize-and-wait on `NotAvailable`.

## Verification

- Device build BOTH `env:default` (flag off — must stay 95.9% flash, parse path untouched) and
  `-DEPUB_STAGE1=1` (flag on) — both compile clean.
- No host gate (activity not host-compiled); correctness of the producer/consumer machinery is already
  host-proven (sub-steps 1–3). Sub-step 4 is wiring; its gate is the device flash + sub-step 5
  measurement: `served from content.bin` on small spines, producer commits advancing in the 5 s BG
  debug line, relayout ≥3×, Min Free ≥ baseline, no `failedAlloc`, cold-open ≈ flag-off.

## Status

**DECIDED (user, 2026-07-27):** on consumer `NotAvailable`, first cut = **immediate parse fallback
(a)** — the page is never delayed; content.bin wins on re-visits / relayout (where the ≥3× lives).
Prioritize-and-wait (b) is deferred to optional 4d. Arena model already decided (consumer-priority, no
producer buffer borrow). Coding starts at 4a.
