# Increment E sub-step 5 — device measurement analysis + revised plan (2026-07-27)

Flag-on device flash, caches cleared, both corpus books. **Result: the producer never ran on either
book** — no `ContentBinProducer started`, nothing `served from content.bin`, every spine parsed via
`INCR_RELEASED` Background-C, exactly as pre-E. Heap healthy throughout (Min Free 7140, arena highWater
≤16984, no `failedAlloc`). This is a **scheduling / architecture** failure, not memory.

## Two books, two independent reasons the producer starved

### King's Avatar (1732 spines) — starved by Background-B
The BG debug line shows Background-B continuously busy: `B runs=6 completes=3`, cycling
`probe → waitheap → building → settled` without pause. With 1732 spines there is ALWAYS a look-ahead
section cache to pre-build, so the loop never sits in an unchanged `Settled`/`Probe` state — my producer
gate (`backgroundBuildState_ == bStateBefore && (Settled||Probe)`) never fires.

### Small Gods (2 spines, one 584 KB) — starved by Background-C
Only two spines, but the moment the cover (spine 0) renders the reader page-turns into spine 1
(`inflatedSize=583991`), which Background-C builds for many seconds (`buildPct 3→16→…`, pages
0→80+). Throughout that build `serviceBackgroundWork` returns early at the `section->hasActiveBuild()`
check — the producer (lowest priority, after C and B) never gets a tick. Even with no B contention, an
actively-read book keeps C busy and the producer starves.

### Unifying root cause
The producer sits BELOW both Background-C (the reader's own active build) and Background-B (look-ahead)
in `serviceBackgroundWork`. The "runs only in fully-idle gaps" design assumed idle gaps exist during
reading. On the two representative shapes — a huge book (B never idles) and an actively-read book (C
never idles) — they effectively don't. The producer is correct but unreachable.

## The deeper architectural finding

Background-B **already pre-builds section caches** for the spines ahead of the reader. So for FORWARD
reading, a content.bin read-back is redundant: the reader hits `cacheHit=1` from B's work regardless of
content.bin. content.bin's UNIQUE value is **relayout** — a font/margin/hyphenation change drops every
section-cache variant and today re-parses every spine (ZIP/XML/CSS); with content.bin it would re-run
only Stage-2. That is the ≥3× win the whole Increment targets. But relayout needs content.bin to EXIST,
and nothing builds it because the producer starves.

So two things are now clear:
1. The producer must not be gated behind B/C as lowest priority — it needs its own guaranteed progress.
2. For forward reading, content.bin and Background-B overlap; the real, non-redundant payoff is relayout.

## Options (for decision)

**A. Producer gets a guaranteed cadence, independent of B/C idleness.** Keep B for forward-read cache
hits; additionally run one producer slice on a fixed time cadence (e.g. ≥1 slice / N ms) even while B is
busy, capped so it can't starve the reader. content.bin fills in over time; relayout gets fast once it
covers the book. Smallest change; B and producer coexist, some duplicated Stage-1 work (B parses for the
section cache, producer parses for content.bin — the SAME walk, done twice).

**B. Producer REPLACES Background-B when EPUB_STAGE1 is on.** The look-ahead work becomes "pre-compile
content.bin ahead of the reader"; forward reads are served by the sliced read-back instead of a
pre-built section cache. One background pipeline, no duplication, content.bin always built. Cost: forward
reads pay read-back (Stage-2 layout) instead of a section-cache hit — but a read-back is cheap (no
ZIP/XML/CSS) and can itself be pre-warmed into a section cache. Biggest change, cleanest end-state.

**C. Trigger content.bin compile only for relayout.** Don't pre-build at all during normal reading (let
B handle forward reads as today). On a settings change, compile content.bin (or compile-and-relayout)
so the EXPENSIVE case — re-paginating a whole book after a font change — uses Stage-2 only. Narrowest
scope, directly targets the one case with a clear win, but the first relayout after a settings change
still pays the compile (subsequent ones are fast), and it abandons the "content.bin as the primary
representation" vision for a targeted optimisation.

**D. Deduplicate: make Background-B ITSELF emit content.bin.** B already parses each look-ahead spine to
build its section cache. Attach the ContentBinWriter to THAT existing walk (setStage1Sink) so one parse
produces both the section cache AND the content.bin spine — no separate producer, no duplicated work,
content.bin fills exactly as fast as B pre-builds. Forward reads keep their cache-hit; relayout gets
content.bin for free. Medium change; elegant because it removes the producer-vs-B competition entirely
by merging them.

## Recommendation

**Option D** looks strongest: it dissolves the producer-vs-Background-B conflict (the actual bug) by
having the ONE look-ahead parse emit both artefacts, so content.bin is a free by-product of work B
already does, and forward reads keep their section-cache hit. It needs the on-demand foreground build
(the reader's own spine, Background-C) to ALSO emit content.bin for the spine the user is actually on
(so the current spine is covered too, not just look-ahead). Relayout then reads content.bin for every
spine B or C has visited.

Open question D raises: B builds a section cache for a SPECIFIC settings variant (propertyHash); the
content.bin it emits alongside is settings-INDEPENDENT (that is the whole point), so the two are written
in one walk but keyed differently — needs care that a settings change invalidates the section caches but
KEEPS content.bin (already the design: content.bin keyed on ZIP fingerprint only).

## Status

**DECIDED (user, 2026-07-27): Option D.** Background-B (and Background-C) emit content.bin as a
by-product of the look-ahead / current-spine parse they already run — attach the ContentBinWriter to
that walk via setStage1Sink, so one parse produces both the section cache and the content.bin spine. The
standalone ContentBinProducer scheduling from 66193f60 is removed (superseded). content.bin keyed on ZIP
fingerprint only → a settings change drops section caches but keeps content.bin → relayout reads it.

Implementation shape for D:
- A book-scoped, lazily-opened ContentBinWriter owned by the reader (open on first B/C build for a book
  whose content.bin is absent/stale; finish/close on book exit).
- Before each Background-B / Background-C section build, if that spine's content.bin slot is not yet
  committed, `beginSpineAt(spine)` + `setStage1Sink(&writer)` so the SAME parse that fills the section
  cache also streams the spine's records to content.bin. onSpineEnd (fired by the walk) commits+flushes
  the slot (durable, sub-step 3).
- The section-cache build and the content.bin emission share ONE walk — no second parse, no producer
  tick, no competition. The read-back consumer (stepReadBackFromContentBin) stays as the relayout path.
- Open care: B builds a specific-variant section cache (propertyHash); the content.bin it emits is
  settings-independent. One walk, two keyed outputs. A low-heap/css-degraded B build that gets discarded
  must NOT commit a content.bin slot from a degraded parse — gate the slot commit on a clean build.
