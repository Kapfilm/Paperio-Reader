# content.bin device wiring — design (#1d, 2026-07-26)

Branch: `feat-stage1-extraction`. Goal: make the proven ~8-9× relayout win real on device by
persisting `content.bin` (settings-independent Stage-1 artifact) and reading it back to drive
Stage-2, skipping ZIP/XML/CSS on settings changes and rebuilds. The read-back is already proven
byte-identical (`ContentBinReplayMatrix`, 94/94) and fast (`ContentBinSpeed`, ~8-9×) via the host
harness drivers; this wires the SAME two phases into `Section`.

## What exists today (the ground truth I'm building on)

`Section::createSectionFile` is a **sliced** build (`runBuildSetup` → `runBuildParse` →
`runBuildFinalize`, each yielding `More`/`Ok`/`Failed` under a time budget for background builds).

- **Section cache** (`sections/<spine>_<propertyHash>.bin`): the fully-paginated pages.
  `propertyHash = calculatePropertyHash(font, lineCompression, spacing, align, viewport,
  hyphen, embeddedStyle, bionic, footnotePreviews, imageRendering)` — **settings-dependent**.
  Variant eviction (max 5, LRU). This is Stage-2's output. KEEP AS-IS.
- **Book-keyed HTML cache** (`getSectionHtmlCachePath`, per spine, settings-INDEPENDENT): the
  inflated XHTML, so a rebuild skips ZIP inflation (`runBuildParse` ~761-775). This is the layer
  BELOW content.bin — content.bin supersedes it (skips XML+CSS+walk too), but they can coexist
  during rollout.
- **ZIP content fingerprint** (Phase 1): `Epub::zipFingerprint()` →
  `zip.contentFingerprint(&out)` (Epub.cpp:387-395). This is the settings-independent,
  content-derived key `content.bin` must use.
- The parser drives ONE sink (`effectiveSink()`): the internal `LayoutSink` on the device path, OR
  an external `ContentSink` when `setStage1Sink` is set (today only tests set it). No fan-out.

## Design decision — TWO-PASS, not fan-out (first cut)

The parser enforces one sink at a time, and the walk's transcript is identical regardless of sink.
Two clean options:

- **(A) Fan-out**: one walk feeds BOTH a `ContentSink` (→content.bin) and the `LayoutSink` (→pages).
  Saves the double walk on FIRST compile, but requires a tee sink + touching `effectiveSink()`'s
  one-sink invariant. More invasive.
- **(B) Two-pass** (CHOSEN for the first cut): 
  - **Compile pass** (once per book, keyed on ZIP fingerprint): walk → `ContentSink` → write
    `content.bin`. This is the existing full path with a `ContentSink` attached — exactly
    `compileToContentBin` in the harness.
  - **Layout pass** (per settings variant): if `content.bin` exists and its fingerprint matches,
    read it and replay through `LayoutSink` → section cache. NO parser, NO ZIP/XML/CSS — exactly
    `replayFromContentBin`.

Rationale: (B) matches the plan's "Stage-1 then Stage-2" framing, reuses the two harness functions
that are already proven byte-identical, and keeps `effectiveSink()` untouched. The first-compile
double-walk cost is paid ONCE per book (fingerprint-keyed); every subsequent open/settings-change
takes the fast path. Optimize to (A) later if first-open latency needs it.

## Cache key & invalidation

- **`content.bin` path**: `<cachePath>/content.bin` (whole book, one file). Keyed on the ZIP
  content fingerprint: store the fingerprint in the WBC1 header (add a `uint64_t`/digest field to
  the header, version-gated) OR in a sidecar. On open, compute `Epub::zipFingerprint()` and compare;
  mismatch → treat as stale, recompile. (WBC1 currently has magic+version only — this adds the
  fingerprint field, a small format bump.)
- **`content.bin` is NEVER invalidated by settings** — only by the content fingerprint. A font/
  margin change drops the section-cache variant (existing propertyHash logic) but KEEPS content.bin
  → the rebuild takes the read-back fast path.
- Section cache keeps its `propertyHash` key unchanged.

## Fit into the sliced build

The layout pass (replay) must slice like the parse does (background build budget). Options:
- Simplest first cut: the replay is FAST (~0.18s whole-book Moby on host; per-spine far less), so a
  per-spine replay likely fits one slice without yielding. Start there; add intra-spine yielding
  only if a huge single-spine book blows the budget (the plan's "huge single-spine chapter" corpus
  book is the test).
- The compile pass already slices (it IS the current parse path); attaching a `ContentSink` instead
  of the `LayoutSink` doesn't change its slicing. But note: today the device path builds the section
  cache DIRECTLY (LayoutSink). Two-pass means: pass 1 builds content.bin (ContentSink), pass 2
  builds the section cache from content.bin (LayoutSink). The FIRST open now does BOTH — that's the
  double-walk cost noted above.

## Rollout / flag

- Gate behind a build flag (the plan names `-DEPUB_STAGE1=0/1`). Off = today's direct LayoutSink
  path (unchanged, shipping-safe). On = two-pass with content.bin.
- Increment order (each host-gated on the full suite + `ContentBinReplayMatrix` + a NEW
  Section-level test asserting the device path's section cache is identical with the flag on vs off):
  1. Add the ZIP-fingerprint field to WBC1 + `Epub::zipFingerprint` wired (format bump, round-trip
     test).
  2. `Section`: write `content.bin` during a build when the flag is on (attach ContentSink; keep the
     LayoutSink path producing pages so nothing regresses — this is a transitional fan-out JUST for
     the write, or a separate compile call).
  3. `Section`: read-back fast path in `runBuildParse`/`runBuildSetup` — if a fingerprint-valid
     content.bin exists, replay it to the LayoutSink and SKIP the parser. Gate: section cache
     byte-identical to the direct path.
  4. Retire the book-keyed HTML cache once content.bin covers its role (Phase 3 step 6).

## Risks / watch-items

- **Shipping-path change**: every step must keep the flag-OFF path bit-for-bit unchanged and the
  flag-ON section cache byte-identical to flag-OFF. The `ContentBinReplayMatrix` gate already proves
  replay==direct; add a Section-level equivalence test for the integrated path.
- **Device-only validation**: host can prove byte-identity + speedup; cold/warm open ms, background
  build slicing under the real budget, and SD write cost of content.bin need a device run + the
  smoke script (anchor nav + footnotes). Cannot be closed in the host-only environment.
- **Storage**: content.bin adds SD bytes; plan budget ≤1.5× source EPUB for all artifacts. Once it
  retires the HTML cache (step 4) the net may drop. Measure on device.
- **First-open latency** rises (double walk) until/unless we switch to fan-out (A). Acceptable if
  the plan's "cold open ≤ baseline +10%" holds; measure. If not, do (A).
- **Fingerprint mismatch handling**: a corrupt/partial content.bin (interrupted write) must fail
  cleanly to recompile, exactly like the HTML cache's size-mismatch drop.

## Reuse

`test/epub_pipeline/PipelineRunner.cpp` already has `compileToContentBin` and
`replayFromContentBin` — the Section wiring should factor the SAME logic so the device path and the
proven harness path stay in lockstep (ideally the harness calls the real Section entry points once
they exist, closing the loop).

## Status

- **Increment 1 DONE** (`ea9f7979`): WBC1 v4 stores the source ZIP content fingerprint; `Epub`
  gained a public `zipContentFingerprint()`; the harness stamps it on compile and rejects a
  mismatch on replay. Host suite 539/539; synthetic replay matrix still byte-identical.
- **Increments 2–4 NOT started** (checkpointed here): they modify `Section::createSectionFile`
  (write `content.bin`; add the read-back fast path) — the shipping build path. Deferred because
  they need device validation (cold/warm open ms, background-build slicing under the real budget,
  SD write cost) that the host-only environment cannot provide.

### Next session / on hardware — pick up at increment 2

1. **Incr 2 (write, additive)**: behind `-DEPUB_STAGE1`, attach a `ContentSink` during the build and
   write `<cachePath>/content.bin` (stamped via `Epub::zipContentFingerprint`). Keep the LayoutSink
   page production unchanged (transitional fan-out or a separate compile) so nothing regresses.
   Host gate: section cache byte-identical flag-on vs flag-off; content.bin fingerprint valid.
2. **Incr 3 (read-back fast path)**: in `runBuildSetup`/`runBuildParse`, if a fingerprint-valid
   `content.bin` exists, replay it through the LayoutSink and SKIP the parser (reuse the exact logic
   in `PipelineRunner::replayFromContentBin`, incl. the kContinuation coalescing). Host gate: section
   cache byte-identical to the direct path. Device gate: the ~8–9× win realized on a settings change.
2. **Incr 4**: retire the book-keyed HTML cache once content.bin covers it (Phase 3 step 6); measure
   storage vs the ≤1.5×-source budget.

The harness `compileToContentBin`/`replayFromContentBin` are the reference implementations — factor
the Section wiring to share that logic so device and host stay in lockstep.
