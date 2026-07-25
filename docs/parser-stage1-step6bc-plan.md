# Step 6b/6c implementation plan — flip the parser onto LayoutSink, delete the fused layout

Written before code because the unify touches the hot parse path AND the equivalence harness that
proves it. Grounds every decision in the 6b edit-site inventory (this session's agent map).
Companion to `parser-stage1-step6-design.md`. Branch `feat-stage1-extraction`.

## Current state (what's proven, what isn't)

- `LayoutSink` reproduces the fused layout **byte-identical across the synthetic corpus × 7 profiles
  (84 cases)** and all three getter tables (anchors/labels/LUT; LUT via the 6a XPath plumbing).
- Moby Dick (real book) diverges by ~105 lines — a localized top-margin/pagination gap at Project
  Gutenberg boilerplate; zero word-level diffs. Marked known-divergent (skips) in the gate.
- Today the device path uses the FUSED inline layout; `stage1Sink_` is null on device and the
  producer is inert. The equivalence test attaches a LayoutSink AS the external `stage1Sink_`.

## The core tension for 6b

6b makes the parser own an internal LayoutSink and route output through it. But the equivalence
harness currently *is* an external LayoutSink attached via `setStage1Sink`. After 6b the parser has
its OWN internal LayoutSink — attaching a second one doubles up. So 6b cannot be done without
reworking the harness, and the harness is the correctness proof. This coupling is why 6b is bigger
than "construct a sink in setup()".

## Decision 1 — MERGE 6b and 6c (do NOT build the parallel-run tee)

The plan's 6b (parallel run: internal sink + fused layout both running, output from the sink) needs
scaffolding — a `TeeBlockSink` fan-out, an always-on producer, keeping the fused `emitPage` running
for its scratch state — that **6c immediately tears down**. That scaffolding is throwaway and adds
its own risk (the `onBlock(Block&&)` move-vs-copy wrinkle: a tee must copy the block into all-but-
last sink; correctness-neutral but extra surface).

**Chosen: collapse 6b+6c into one coordinated change.** The parser drives ONLY the internal
LayoutSink; the fused inline-layout half is deleted in the same pass; getters come from the sink.
No tee, no dual-run, no always-on-fused-scratch. Rationale: less throwaway work, smaller net diff,
and the end-state is what we want anyway. The Moby margin gap is absorbed here (no fused reference).

Trade-off accepted: we lose the "parallel run asserts sink==fused at runtime" intermediate. We don't
need it — the step-5 matrix already proved sink==fused offline, and the synthetic goldens are the
post-merge gate. Rollback is `git revert` of the single unify commit series.

## Decision 2 — the external ContentSink still needs the producer stream

`content.bin` compilation (host `content_stage1_dump` + ContentSink tests) drives a ContentSink via
the producer. After the merge, the producer must drive BOTH the internal LayoutSink (always) and,
when compiling, the ContentSink. Two options:

- **(A) Internal sink is the only BlockSink; ContentSink attaches separately.** Keep `stage1Sink_`
  as the *ContentSink* tap (external, optional), and add the internal LayoutSink as a distinct
  always-present consumer. The producer fans out to both. This is the tee after all — but minimal
  (only the ContentSink compile path pays the block copy, and only offline).
- **(B) Keep the producer single-sink; ContentSink compile uses a DIFFERENT entry.** The device
  build constructs the parser with the internal LayoutSink; the ContentSink compile path swaps the
  internal sink for the ContentSink (no LayoutSink, no pages needed — the compile only wants
  content.bin). i.e. `setStage1Sink(contentSink)` *replaces* the internal layout sink rather than
  adding to it.

**Chosen: (B).** The ContentSink compile does not need pagination — it only serializes content.bin.
So when an external ContentSink is set, the parser drives THAT (no internal LayoutSink, no
completePageFn). When none is set (device + the layout equivalence test), the parser drives the
internal LayoutSink. One sink at a time — no tee, no block copy, no move wrinkle. `setStage1Sink`
becomes "use this sink instead of the internal layout sink."

Consequence: `createSectionFile` (which produces the section cache = pages) always uses the internal
LayoutSink. `compileContent` (content.bin) sets an external ContentSink and gets no pages — which is
already how `compileContent` behaves (it ignores pages). Verify: `compileContent` never reads
`getParagraphLutPerPage`/pages, only `sink.content()`. (It does — it's a content-only driver.)

## Decision 3 — getter proxy + LUT type adapter

- `getAnchors()` / `getPageBreakLabels()` — same types on both sides; proxy the internal sink
  directly (return `layoutSink_->anchors()` / `->pageBreakLabels()`).
- `getParagraphLutPerPage()` returns `vector<ParagraphLutEntry>&`; the sink exposes
  `vector<LayoutLutEntry>&` (field-identical, distinct type). Adapter: give the parser a
  `mutable std::vector<ParagraphLutEntry> lutAdapter_`; the getter rebuilds it from the sink's
  vector on call (cheap; called once per spine at serialize time). Keeps Section.cpp untouched.
  (Alternative: make the two a single shared type — cleaner but touches more files; defer.)

## Decision 4 — harness rework (the equivalence test)

`layoutViaSink` currently attaches its own LayoutSink via `setStage1Sink` and collects pages from
its completePageFn. After the merge, the parser's INTERNAL sink produces the pages through the
parser's own completePageFn. So `layoutViaSink` becomes: build via `createSectionFile` with a
completePageFn that collects pages (exactly what `runAndDump` already does!) — i.e. **`layoutViaSink`
collapses into `runAndDump`**. The fused-vs-sink equivalence test becomes "the section cache the
parser now builds (via the internal sink) vs the committed goldens." The `MatchesGolden` test
ALREADY does this. So post-merge:
- `LayoutSinkEquivalence.PageDumpMatchesFused` is redundant with `MatchesGolden` (both now exercise
  the same single layout path). Keep one; the goldens ARE the gate.
- The Moby known-divergent case moves to a golden comparison (or stays as a dedicated dump diff if
  we want to keep watching the gap without committing a Moby golden).

This is the biggest harness change and must be done in lockstep with the parser flip.

## Sequence (each step builds + full suite green)

1. **Construct the internal LayoutSink in setup()** from parser members (+ `epub->getPath()`),
   completePageFn = parser's completePageFn. Do NOT wire it in yet (dead object). Build green.
2. **Route the producer to the internal sink; make it always-on.** Replace `stage1Sink_->M(...)`
   with `effectiveSink()->M(...)` where `effectiveSink()` = external `stage1Sink_` if set else the
   internal LayoutSink. Flip the 7 `if (!stage1Sink_) return;` gates to `if (!effectiveSink()) return;`
   (always non-null now). The producer now drives layout.
3. **Disconnect the fused emitPage output** (remove only line 473 `completePageFn(...)`; keep the
   rest). Proxy the 3 getters to the internal sink (+ LUT adapter). Now pages + getters come from
   the sink; fused layout still computes but emits nothing.
   → At this point 6b is functionally done: device path is on the sink. GATE: synthetic goldens
   byte-identical (they must be — sink==fused proven). Run the FULL suite.
4. **Delete the fused inline-layout half** (6c): makePages, addLineToPage, emitPage body, the layout
   half of startNewTextBlock, attachPendingFloatImage, placeImageBlockAsBlock, the emitTableAs*
   family, the >96-word split, <img>/<hr> placement, and the layout state members. Keep the walk +
   producer. GATE: synthetic goldens still byte-identical; full suite green.
5. **Rework the harness** (fold layoutViaSink into the golden path; handle Moby). GATE green.

Steps 1-3 = the flip (6b). Step 4 = the delete (6c). Step 5 = harness. Each is independently
buildable; the risky one is step 3 (device flip) — gated on synthetic goldens.

## Gates / rollback

- Every step: `ctest -E "NOT_BUILT|dlfcn"` fully green + synthetic goldens byte-identical.
- The device flip (step 3) additionally: confirm `MatchesGolden` (the fused device dump) is
  unchanged — it now comes from the sink, and must equal the committed goldens.
- Rollback: the merge is a small commit series; `git revert` restores the fused path. Do NOT delete
  the fused code (step 4) until step 3 has been golden-verified and, ideally, device-smoke-tested.

## Open risk

- The Moby margin gap ships to the device at step 3 (cosmetic, real-book back-matter only). Accepted
  per the user's "flip now" decision; it becomes a plain sink bug to fix post-unify.
- `fontSizeLadder_` must be populated before `setup()` constructs the sink — Section sets it via
  `setFontSizeLadder()` before `setup()`. VERIFY this ordering holds (inventory flagged it).
- Anything still reading the fused `anchorData`/`completedPageCount` after the getter proxy — the
  inventory says finalize's trailing-anchor bookkeeping still writes fused `anchorData` (now unread).
  Confirm nothing reads it post-proxy before deleting in step 4.
