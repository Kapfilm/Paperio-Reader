# Increment F — content.bin as the single persisted cache (design, 2026-07-27)

## Why F supersedes the E scheduling work

The E sub-step-5 device measurement (docs/stage1-incr-E-substep5-measurement-analysis) showed the
standalone background producer starves: it sits below Background-B (section-cache pre-build) and
Background-C (the reader's own build), which never both idle on a real book (huge → B never idles;
actively-read → C never idles). The deeper finding: Background-B already pre-builds SECTION CACHES, so
content.bin is redundant for forward reading; its unique value is relayout.

The user challenged the premise: **why have a settings-dependent section cache as its own persisted
cache at all?** Given a content.bin read-back (Stage-2 layout only, no ZIP/XML/CSS) is fast, the section
cache's "avoid re-paginating" value is small while its costs are real (N variants × every spine on disk,
eviction, and the producer-vs-B conflict). Decision: make content.bin the SINGLE persisted artifact.

## The model

- **content.bin** — settings-INDEPENDENT, keyed on ZIP fingerprint. The ONLY persisted cache. Built
  spine-by-spine; a spine committed once is reusable across every settings variant forever.
- **Section file → transient current-spine page store.** The reader still needs O(1) random page
  access WITHIN the spine it is displaying (page-turn, jump-to-page, back-nav, percent-jump all read
  page N by LUT). On entering a spine, paginate content.bin ONCE into a section file (same on-disk
  format + LUT) as SCRATCH for the current spine only — rebuilt on a settings change, NOT accumulated
  as per-variant persisted caches across spines. No propertyHash variants persisted, no eviction.

### Entering a spine
1. content.bin covers this spine (committed slot, fingerprint OK) → paginate it into the transient
   section store via the sliced read-back (stepReadBackFromContentBin). Fast (Stage-2 only).
2. content.bin does NOT cover it → **parse-and-display now**: the current fused parse builds the
   transient section store AND (Increment F) emits the spine to content.bin in the SAME walk (so next
   visit / relayout is a read-back). First page appears as fast as today.

### Background look-ahead
Background-B's job BECOMES "compile ahead to content.bin" (not "pre-build a section cache"). One
pipeline, no competition, no starvation — the reader never waits on it (it parses-and-displays on a
miss). Look-ahead simply advances content.bin's frontier ahead of the reader.

### Relayout (the win)
A font/margin/hyphenation change re-paginates the CURRENT spine from content.bin with the new settings
(Stage-2 only) — no re-parse. Nothing to invalidate (no per-variant caches); the transient store is
just rebuilt. This is the ≥3× target, now the natural behaviour rather than a special case.

## What this removes vs. adds

REMOVES: per-variant persisted section caches (`sections/<spine>_<propertyHash>.bin` as a durable
multi-variant store), the eviction/LRU logic, the standalone ContentBinProducer + its starving
scheduler (E 66193f60), the producer-vs-Background-B conflict.

ADDS: the parse-and-display path also emitting content.bin (the tee — one walk, LayoutSink for the
transient page store + ContentBinWriter for content.bin); Background-B retargeted to compile-ahead;
"enter a spine" routed through content.bin-first.

KEEPS (all host-proven): v6 content.bin format, ContentBinWriter (durable per-spine commit),
BlockStreamReader + refreshIndex, replaySpine/replaySpineStep, stepReadBackFromContentBin (sliced
consumer), the transient section-file format + Page (de)serialization + LUT.

## The tee (one walk, two sinks) — still needed for the parse-and-display miss path

On a content.bin miss the fused parse must feed BOTH the LayoutSink (transient page store, so the first
page shows now) AND the ContentBinWriter (so the spine lands in content.bin for next time). The parser's
effectiveSink() is single-sink today (stage1Sink REPLACES layout). Add a tee mode: a TeeBlockSink
forwards every call to both; onBlock copies the Block for one sink and moves into the other (one bounded
Block copy per block, transient ~one block RAM). Content-only mode (host compile path) is preserved via
a separate flag. Only 3 parser sites touch stage1Sink_ (effectiveSink, setup's layout-sink gate, a
comment), so the change is contained. Host-gate: one tee walk produces a section file identical to
today's parse AND a content.bin spine identical to a standalone compile.

## Migration / safety

- flag OFF (EPUB_STAGE1==0): the reader keeps the current persisted section-cache behaviour EXACTLY.
  Increment F lives entirely behind the flag; env:default ships unchanged until F is proven on device.
- No device has content.bin; a first run on flag-on simply has no content.bin and parse-and-displays
  everything, filling content.bin as it goes — no migration.
- The transient section store reuses the existing section-file format, so Page load/turn/jump code is
  unchanged; only its LIFECYCLE changes (scratch for the current spine vs. persisted multi-variant).

## Sub-steps (each small, host-gated where possible; device build both flags)

F1. TeeBlockSink + parser tee mode (setStage1TeeSink); host-gate one-walk-two-outputs equivalence.
F2. "Enter a spine" routed content.bin-first: read-back if covered, else parse-and-display via the tee
    (spine lands in content.bin). Behind the flag. Device build both flags.
F3. Retarget Background-B to compile-ahead to content.bin (drop section-cache pre-build under the flag);
    remove the standalone producer (E 66193f60). Device build both flags.
F4. Transient section store: stop persisting per-variant section caches under the flag (current-spine
    scratch only); relayout re-paginates from content.bin. Device build both flags.
F5. Device measure: served-from-content.bin on real reading, relayout ≥3×, cold-open ≈ flag-off, heap
    ≥ baseline, no failedAlloc; content.bin size ≤1.5× source.

## Open questions to resolve during implementation

- Eviction/space: content.bin grows to cover visited spines; on a 1732-spine book read end-to-end it
  could approach ~source size. Bounded by ≤1.5× source (measure). A cap / LRU over content.bin spines
  is a possible later refinement, NOT in the first cut.
- The transient section store on a settings change: rebuild lazily on next page access vs. eagerly.
- Whether F4 (dropping persisted section caches) can be flag-gated cleanly given loadSectionFile /
  propertyHash / evictOldVariants are threaded through the reader — may need care to keep flag-off
  identical.

## Status

Design for review. Decisions locked (user, 2026-07-27): content.bin is the single persisted cache;
section file becomes a transient current-spine page store; on a miss, parse-and-display now + compile in
background. No revert of the sound E machinery; the E standalone producer scheduling is dropped.
