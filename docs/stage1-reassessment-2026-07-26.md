# Stage-1 pipeline — mid-course reassessment (2026-07-26)

A deliberate step back before wiring content.bin into the device path. Four questions:
(a) done/missing + plan, (b) vs objectives, (c) architecture soundness after the byte-equivalence
work, (d) is the code fit for streaming / "render page 1 before compile finishes".

## (a) What's done, what's missing, is the plan clear

**Done (committed, host 539/539 green):**
- Stage-1 producer (walk → `BlockSink` transcript) + Stage-2 consumer (`LayoutSink`); the fused
  inline-layout engine deleted.
- WBC1 `content.bin` format complete + settings-independent: blocks, styles, anchors, chapters,
  footnotes, xpath LUT, page-break labels, source ZIP fingerprint (v4).
- Read-back replay proven **byte-identical** to direct layout (94/94 corpus × profiles + Moby).
- Relayout speed **~8–9×** measured (full parse+layout vs content.bin replay).

**Missing:**
- content.bin is **not wired into the device `Section` build** (write + read-back fast path) —
  increments 2–4 of the device-wiring design. Needs hardware validation.
- **No streaming/incremental content.bin** (see (d)) — the pull side (`IBlockSource`) is
  designed-but-absent.
- Phase 4 (single generation hash, partial-cache resume, A/B/C collapse) — not started.

**Plan clarity:** yes for the *mechanical* next steps (device-wiring design note lists increments
2–4 with gates). BUT (d) below surfaces a design gap the plan does not yet resolve — the whole-book
two-pass shape conflicts with the streaming/first-page-latency goal. The plan needs a revision
before increments 2–4, not just execution.

## (b) Vs the objectives

Plan objectives: (1) settings-independent Stage-1 that never re-runs on font/margin change;
(2) Stage-2 pagination with no ZIP/XML/CSS; (3) ≥3× relayout; (4) memory drop; (5) superior UX
(memory, speed, quick first feedback).

- **(1),(2),(3): MET and proven.** The split is real, content.bin is settings-independent, replay
  is byte-identical and ~8–9× faster.
- **(4) memory: NOT met — likely REGRESSED as currently built.** `ContentSink` accumulates the
  **whole book's** `CompiledContent` in RAM before any write (`ContentSink.h:6-9`), and
  `readContentBin` reconstructs the whole book in RAM. The current per-spine build holds only ONE
  spine and streams each finished page straight to disk (`Section::onPageComplete`,
  Section.cpp:248-265). On a 380 KB device a whole-book compiled model is a real peak-memory risk.
  The plan explicitly wanted "auxiliary tables streamed through temp files … never held in RAM" —
  the implementation does the opposite.
- **(5) quick first feedback: NOT met — REGRESSED as designed (see (d)).**

## (c) Architecture soundness after the byte-equivalence work

**Sound, with justified complexity — plus real comment debt.**

- The empty-block merge machinery (`hasPendingMerge_`, `<br>`-gap injection, the #1026 alignment
  reset) and the producer emitting empty wrapper/spacer transcript blocks is **inherent**, not a
  band-aid: it is how CSS margin collapsing across wrapper elements is reproduced settings-
  independently. Folding those margins in the producer instead would bake settings-dependent px
  into content.bin — the exact thing we designed against. Keep it.
- `LayoutSink` (875 lines) is larger than the ~470 fused lines it replaced because it now OWNS,
  cohesively in one place, what was scattered across the parser (measure, paginate, floats, tables,
  HR, footnote-preview abbreviation, empty-block replay). That is a net structural improvement.
- **Comment debt (worth fixing):** ~58 references in `LayoutSink.cpp` point back to the DELETED
  fused parser — dangling `cpp:NNNN` line numbers and "reproduces the fused path exactly / keep in
  lockstep until step 6 removes the originals" framing. Step 6 is done; those originals are gone.
  The comments now describe LayoutSink as a *port of code that no longer exists* instead of the sole
  layout implementation on its own terms. This is misleading to a future reader (the line numbers
  resolve to nothing) but is pure documentation — no behavior risk. A cleanup pass should re-anchor
  these comments to what the code does, not to the deleted original.
- The byte-equivalence "patches" (the <pre> unconditional-space, the page-break decouple, the
  footnote coalescing on replay) are individually justified and tested; none is a hack left in a
  load-bearing spot. The one that reads as incidental — the 8 KB split needing re-coalescing on
  replay — is a genuine consequence of the read-time-memory-bound split, not sloppiness.

**Verdict:** the architecture is sound and the complexity is largely inherent. The debt is comment
staleness, not structural rot.

## (d) Is the code fit for streaming / "render page 1 before compile finishes"?

**This is the important finding: NO — the content.bin design as built is a STREAMING REGRESSION,
and it is a design problem, not just unfinished wiring.**

What the CURRENT (pre-content.bin) path already does — and does well:
- Sliced background build (`runBuildSetup/Parse/Finalize` under `budgetMs`, yielding `More`).
- **Pages stream to disk mid-parse**: `completePageFn → onPageComplete` serializes each page as it
  is produced and bumps `pageCount` (Section.cpp:701, 248-265).
- **First pages served before the spine finishes**: `loadPageFromActiveBuild` reads the in-memory
  live LUT (Section.cpp:1366), used by the pre-render pass and Background-C "build-while-you-read"
  (EpubReaderActivity.cpp:2366-2381, 2998-3010).
- A (pre-render next page) / B (build next section) / C (build current section, post update per
  built page) is exactly the "parallel actions + quick first feedback" the objective wants — and it
  EXISTS today.

What the content.bin two-pass design does to that:
- content.bin is **whole-book, one blob, no per-spine flush**: it does not exist until Stage-1 has
  compiled EVERY spine (`writeContentBin` takes a fully-populated whole-book `CompiledContent`;
  `ContentSink` accumulates all spines then writes once — CompiledContent.cpp:188-284,
  ContentSink.h:6-9, PipelineRunner.cpp:246-269).
- The reader is **whole-file only**: `readContentBin` loads the entire book into RAM; there is **no
  `IBlockSource` / random-access / per-spine reader** (it is named as future work in
  BlockSink.h:15-17 but has NO implementation anywhere).
- Stage-2 replay **re-buffers per spine** (coalesce all continuation records, then match anchors/
  footnotes/labels by blockIndex) before it can lay out block 0 (PipelineRunner.cpp:335-385).
- Net: with content.bin, the first open must **compile the whole book → write file → read whole
  file → lay out** before page 1. The device-wiring design itself flags "the FIRST open now does
  BOTH passes — double-walk cost" and "First-open latency rises until/unless we switch to fan-out."
- The walk + LayoutSink ARE push/streaming when the LIVE parser drives them — but the content.bin
  round-trip DISCARDS that: the artifact is whole-book and the replay re-buffers.

**Conclusion:** the pipeline is fit for streaming ONLY in its live-parser form (which is what the
current build already exploits via A/B/C). The persisted-content.bin form, as designed, is
anti-streaming: whole-book barrier + whole-RAM + no random access. Shipping increments 2–4 as
designed would layer a whole-book compile in front of today's streaming build — regressing both
first-page latency and peak memory to WIN only on the SECOND+ open / settings change.

## Recommendation — revise the design before wiring the device

The ≥3× relayout win is real and worth shipping, but not at the cost of first-open latency and
memory. Before increments 2–4, resolve the streaming/memory shape:

1. **Per-spine content.bin (or a spine index) instead of one whole-book blob.** Write/flush each
   spine's records as it compiles (temp-file splice at finish, as the plan originally said), and add
   a per-spine offset index to the header. This restores per-spine granularity: Stage-2 can read
   spine N alone, and the first spine's content.bin exists before later spines compile.
2. **A random-access reader (the designed `IBlockSource`)** over that indexed file, so Stage-2 reads
   the blocks it needs with a sliding window — not the whole book into RAM. This is what makes the
   memory number actually DROP (objective 4) instead of regress.
3. **Fan-out on first compile (design Option A):** one walk feeds BOTH content.bin (persist) AND the
   LayoutSink (produce pages now), so the FIRST open streams pages exactly like today AND leaves a
   content.bin behind for fast subsequent relayouts. This is the key to "quick first feedback" — it
   makes content.bin a pure win (write-through cache) rather than a front-loaded cost.
4. Only then wire the read-back fast path, and fold it into the A/B/C scheduler (a settings change
   drops the page cache, keeps content.bin, and Background-C replays from records instead of
   re-parsing — same streaming UX, ~8–9× less work).

This is a plan revision, not a rewrite: the format (add a spine index), the reader (add IBlockSource),
and the first-compile path (fan-out) are the three targeted changes. The byte-identical replay gate
and the ~8–9× measurement already de-risk the core.

## Status

Analysis only — no code changed. The device-wiring increments 2–4 should NOT proceed as currently
designed; adopt the per-spine + IBlockSource + fan-out revision above first. Host suite 539/539
green; tree clean.
