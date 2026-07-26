# Stage-1 — revised plan v2: block-streaming content.bin (2026-07-26)

Supersedes `stage1-revised-plan-2026-07-26.md`. Grounded in a measured memory model
(`ae56e9d1` investigation, summarized below) and Fable's adversarial review. The v1 plan
("per-spine writer + IBlockSource sliding window + Tee") is **not bounded enough** and the Tee
carries avoidable correctness risk. This version fixes both.

## Validation against two real stress books (measured 2026-07-26)

Two real EPUBs, one per hard reality, analyzed directly (block/word counts from the actual XHTML):

**(a) Pratchett — Small Gods: the giant single spine.** 2 spine items, but ONE is a **570 KB
inflated single spine** with **4,097 blocks / 91,851 words / ~400 KB text**.
- **v1 (materialize one spine)** = `4097·152 + 8·91851 + (409385+91851)` = **~1.77 MB resident** =
  **4.8× the entire 380 KB RAM → hard allocation failure; the book cannot open.** This alone
  disqualifies v1 (and any whole-spine approach) on a *real, shipping* book.
- **Current path** = the 570 KB never enters RAM (inflated to an SD temp file, fed 1 KB at a time,
  pages streamed to disk) → **~50 KB resident**, same as any spine.
- **v2 (block stream)** = one block at a time; avg block here is ~22 words, worst fat block a few KB
  → **~current + ~5–8 KB**. Bounded. Opens fine.

**(b) King's Avatar — the spine swarm.** **1,732 spine items**, 24 MB total inflated, median ~14 KB
/ max ~29 KB per spine (~151 blocks / ~3,860 words for a median spine).
- **v1 (per-spine, one median spine)** = **~74 KB resident** — already over the current ~40–50 KB
  whole-build peak, on a *median* spine, and pure waste vs. streaming.
- **Per-spine content.bin FILES** would be **1,732 tiny SD files** (FAT/dir overhead, slow
  enumeration) — v2 uses ONE content.bin + a **1,732×u32 = 6.8 KB** offset index (trivial, resident).
- **v2 (block stream + per-spine-on-first-visit)** = **~current + ~one block + ~7 KB** (pool+index),
  and the 1,732-spine book costs **nothing up front** (only visited spines are compiled). Bounded.

**Conclusion: v2 holds on both real extremes; v1 fails on Small Gods outright.** Caveat: these are
*modeled* peaks from measured block/word counts + the verified current-path streaming behavior —
the exact device peak (heap fragmentation, allocator headers, CSS-resolver working set on a
4,097-block spine) still needs a device run to confirm, which is why v2's gates include a host
peak-RAM assertion and a device fragmentation gate. But the ORDER-OF-MAGNITUDE verdict is
unambiguous: whole-spine materialization is ~1.8 MB on a real book; block-streaming stays ~tens of KB.

## The measured reality (numbers, not assertions)

Sizes on ESP32-C3 (32-bit; vector hdr 12 B, string 12 B inline + heap when >15 chars):

- `compiled::Block` = **~152 B fixed struct** (all text/image/table field-sets are always-materialized
  members, not a union) + word/text heap. One 60-word/350-byte paragraph ≈ **~1 KB resident**.
- `SpineContent` for a spine of B blocks ≈ **B·152 + 8·W + (X+W) + anchors/labels**. The `B·152`
  term dominates: a 500-block spine is **~76 KB of Block headers alone**.
- Whole-book `CompiledContent` = Σ spines = **megabytes** for a novel. Does not fit 380 KB.
- `CssStyle` = ~112 B; stylePool ("tens" of entries) ≈ **1–5 KB book-wide — negligible** (keep whole).
- One laid-out ~30-line `Page` ≈ **~4.3 KB**.

**The current (shipping) device build is already fully streaming and book-size-independent:**
- Each spine's XHTML is inflated to an SD **temp file** (`html_<spine>.bin`), never held in RAM
  (`Section.cpp:829-846`).
- The parser is fed **1 KB at a time** from that file (`PARSE_CHUNK_BYTES=1024`, `Section.cpp:892`),
  and **yields mid-spine every 1 KB** under the background time budget (`Section.cpp:908`), resuming
  from stable `BuildState` (`Section.cpp:446-585`).
- Laid-out **pages stream straight to the section-cache file** as each completes
  (`onPageComplete`, `Section.cpp:248-265`); only a 4 B/page LUT stays resident. Pages are NOT
  accumulated.
- Inflate ring (≤32 KB) is phase-(a) only and released before parse — temporally disjoint from the
  layout working set.

Measured resident peak: **~40–50 KB per spine, identical for a typical spine, a 500 KB single
spine, and a 2000-spine book** (host whole-run peaks 47–91 KB across the corpus incl. Moby;
device arena highWater 9–22 KB). **This is the bar content.bin must not regress.**

## The four hard realities → the design constraints they impose

- **(a) Spines can be hundreds of KB.** ⇒ the RAM unit CANNOT be a spine. `SpineContent` for a big
  spine (~76 KB+ of Block headers) already exceeds the current whole-build peak. **The unit must be
  the BLOCK** — the same granularity the current build streams at.
- **(b) Books can have thousands of spines.** ⇒ NO per-spine files (thousands of tiny SD files =
  FAT/dir overhead + slow enumeration). ONE content.bin, block-stream + a compact spine offset
  index. And NO whole-book resident structure — not even the spine-offset table if it can be paged
  (a 2000-spine index is only ~8 KB though, so keep it resident; that's fine).
- **(c) Fast, on ESP32-C3's limited resources.** ⇒ no per-block deep copies (fragmentation, the
  project's #1 device killer), no whole-book/whole-spine materialization, and the relayout win must
  compose with the existing 1 KB-granular sliced background build, not front-load a compile.
- **(d) Stream / early-use partial results across analyse→compile→render→display.** ⇒ content.bin
  must be WRITTEN as a block stream (a block is flushed the instant the walk emits it) and READ as a
  block stream (Stage-2 consumes one block, lays it out, streams the page to disk, drops the block).
  The reader must be able to serve page 1 of spine i after reading only the first few blocks of
  spine i — never after materializing spine i, let alone the book.

## Architecture v2 — block-streaming, never materialize a spine

The invariant, everywhere: **at most O(1) blocks + O(1) pages resident.** Same as today.

### Write side — a streaming block appender (not a whole-book ContentSink)

Replace the whole-book-accumulating `ContentSink` with a **`ContentBinWriter`** that appends each
block to the file AS IT ARRIVES from the walk and immediately drops it:

- `onBlock(Block&&)` → serialize this one block to the open content.bin file, record nothing
  in-RAM except: the current spine's growing (offset,count) and the book-level style pool (small)
  and a running spine-offset index. The block is freed after write. **Peak added RAM ≈ one block.**
- The 8 KB split (`appendTextSplit`) stays — it already operates per-block at write time.
- Anchors/labels/footnotes/xpath are already per-block or per-spine-tiny; write them inline in the
  block stream (footnotes/xpath already ride on the Block; anchors/labels as small per-spine
  sections written at `onSpineEnd`).
- Style pool: interned in RAM (≤5 KB), written once at `finish()`. Chapters: book-level but tiny
  (H·20 + titles) — buffer in RAM (measure; a 500-heading book with 30-char titles ≈ 25 KB, so cap
  or stream to a temp section if a real book exceeds a budget — flag as a checked risk, not assumed).
- Spine offset index: one u32 per spine (2000 spines = 8 KB), back-patched at `finish()`.

### Read side — a streaming block source (not readContentBin)

Replace whole-file `readContentBin` with a **`BlockStreamReader`** that yields one block at a time:

- `openSpine(i)` seeks to the spine's offset (from the index), then `nextBlock() -> Block` reads and
  returns ONE block, advancing the file cursor. **Peak added RAM ≈ one block + a small read buffer.**
- The kContinuation coalescing (needed so a split block lays out as one paragraph) is done
  **incrementally**: `nextBlock()` reads a base block, then peeks/reads following kContinuation
  records and merges them into that one logical block before returning it — bounded to ONE logical
  block, never the whole spine. (Today's `replayFromContentBin` coalesces a whole spine at once;
  v2 makes it a streaming merge of one logical block.)
- The empty-block merge is already streaming/O(1) in `LayoutSink` (`hasPendingMerge_` folds into the
  next block) — no change; it composes naturally with a block-at-a-time reader.

### First compile — NO Tee. Write-through by making content.bin the section cache's source

Fable's review showed the Tee is underspecified and risky: `onBlock(Block&&)` moves the block, so a
Tee must deep-copy every block (per-block fragmentation on the hot first-open path), AND
`LayoutSink::abbreviatePreviewRuns` MUTATES the block in place (bakes viewport-abbreviated footnote
text) — so a Tee that forwards to LayoutSink first would silently write settings-DEPENDENT data into
content.bin, a bug the current byte-identical gate cannot see.

**Drop the Tee.** Two clean options, both avoiding fan-out:

- **Option 1 (preferred): compile content.bin FIRST, then lay out FROM it — but both stream, so page
  1 is still early.** On first open of spine i with no cache: walk → `ContentBinWriter` (block
  stream to disk), THEN `BlockStreamReader` → `LayoutSink` → section cache (block stream). Because
  BOTH stages are block-streaming, the extra cost before page 1 is: the walk of spine i's *first few
  blocks* + their write + their read-back. The walk still yields at 1 KB; the read-back of spine i
  begins as soon as spine i is written. First page of spine i ≈ (walk+write first blocks of i) +
  (read+layout first blocks of i). This roughly DOUBLES the per-block CPU on the very first open of
  each spine (walk-then-replay vs walk-only), but never materializes anything and never regresses
  memory. Whether that doubling is acceptable is a **device measurement**, not an assumption.

- **Option 2 (fallback, zero risk to first open): keep today's direct walk→LayoutSink for the first
  open (unchanged, page 1 exactly as fast as now), and write content.bin LAZILY in the background
  after the reader is served** — a separate walk→`ContentBinWriter` pass scheduled like Background-B,
  1 KB-sliced, that leaves a content.bin behind for the NEXT open / settings change. First open is
  bit-for-bit today's behavior; the relayout win arrives one background pass later. This reuses the
  existing sliced-build machinery and touches `effectiveSink()`'s one-sink invariant not at all.

**Recommendation: build Option 2 first** (zero first-open risk, simplest, reuses existing slicing),
measure the Option-1 doubling on device, and adopt Option 1 only if the measured first-open cost is
within the plan's "cold open ≤ baseline+10%" budget AND Option 2's "content.bin ready one pass later"
proves to be a real UX problem (it likely isn't — how often does a user change font before the
background pass finishes?).

### Per-spine-on-first-visit (the granularity that makes (b) cheap)

Do NOT compile the whole book up front. Compile/persist content.bin **incrementally, per spine, the
first time each spine is actually built** (which the reader already does lazily — most books are
never finished). content.bin grows spine-by-spine; the spine index marks which spines are present.
A settings change relayouts a visited spine from its records; an unvisited spine still needs its
first walk anyway. This makes the 2000-spine book cost nothing up front and bounds the write to the
spines a reader touches.

## Memory model of v2 (the number that must hold)

| Path | Added resident RAM over today | Bounded by |
|---|---|---|
| Write (ContentBinWriter) | ~one Block (+ style pool ≤5 KB, spine index ≤8 KB, chapters buffer — measured) | O(1) blocks |
| Read (BlockStreamReader → LayoutSink) | ~one logical Block + read buffer (+ style pool + spine index) | O(1) blocks |
| First open | Option 2: **+0** (today's path). Option 1: +~one block of pipeline depth | O(1) blocks |

Target: **v2 peak ≤ current ~40–50 KB + O(one block ≈ 1 KB) + fixed indices (≤~15 KB)**, INDEPENDENT
of spine size and spine count. The 500 KB spine and the 2000-spine book both stay bounded — because
nothing ever holds more than one block/one page, exactly as today.

## Gates (must catch what byte-identity can't)

1. **Byte-identical page dump** (existing `ContentBinReplayMatrix`) — reworked to drive the
   BlockStreamReader (block-at-a-time), not whole-file read. Still 94/94 across corpus×profiles+Moby.
2. **NEW settings-independence gate** (Fable's catch): compile content.bin at viewport A, replay at
   viewport B; separately direct-compile at B; assert equal. Catches any settings-dependent data
   (e.g. abbreviated footnote previews) leaking into content.bin — the exact bug a Tee would cause
   and that gate (1) cannot see.
3. **NEW peak-RAM assertion** (host): instrument the writer + reader to assert resident
   `CompiledContent`/block bytes never exceed O(one logical block + indices) — i.e. prove no
   whole-spine/whole-book materialization. Reuse the malloc-interception harness the plan's Memory
   gate already names.
4. **NEW streaming/first-block assertion** (host, deterministic — Fable's point that wall-clock host
   timing doesn't predict device): assert page 1 of spine i is emitted after reading only the first
   K blocks of spine i (a structural ordering check), proving early-use works without a device.
5. **Corrupt/partial-write handling** (Fable's catch, absent from v1): a truncated content.bin, an
   unpatched spine index, or an offset past EOF must fail cleanly to "stale → recompile," never read
   garbage/crash — like the HTML cache's size-mismatch drop. Test with a deliberately truncated file.
6. **Fragmentation (device-only)**: track largest-contiguous-block across a full book build with the
   writer/reader active, not just peak bytes — the project's stated #1 device risk. Deferred to the
   device gate but explicitly listed.

## Sequenced increments (host-gated → device-gated)

- **A. Streaming write** — `ContentBinWriter` (append-per-block, drop-after-write) + spine offset
  index + format v5 header. `ContentSink` becomes a thin whole-book adapter over it for existing
  tests. Gate: round-trip equals today's model; peak-RAM assertion (gate 3); corrupt-write handling
  (gate 5).
- **B. Streaming read** — `BlockStreamReader` (openSpine/nextBlock, incremental kContinuation merge).
  `replayFromContentBin` switches to it. Gate: `ContentBinReplayMatrix` byte-identical via the
  streaming reader; peak-RAM (gate 3); first-block ordering (gate 4).
- **C. Settings-independence gate** (gate 2) — add the A-viewport/B-viewport equivalence test. (Do
  this BEFORE any first-open wiring so the leak class is caught early.)
- **D. Wire Option 2 into Section** behind `EPUB_STAGE1`: first open unchanged (direct
  walk→LayoutSink); a background, 1 KB-sliced `ContentBinWriter` pass writes content.bin per visited
  spine after the reader is served; a subsequent open/settings-change of a spine with valid records
  replays via BlockStreamReader→LayoutSink→section cache. Fold into A/B/C scheduler. Gate (host):
  section cache byte-identical with/without records. Gate (device): cold/warm open ≤ baseline+10%;
  settings-change relayout shows the ~8–9× win; slicing under budget; SD cost ≤1.5×-source; frag
  (gate 6); smoke (anchors, footnotes).
- **E. (optional) Option 1** — only if D's measurements justify write-through-before-layout; else
  keep Option 2.
- **F. Retire the book-keyed HTML cache** once content.bin supersedes it; measure net storage.

## Open risks tracked (not deferred silently)

1. **Chapters table size** — book-level, holds title strings. Measure a heavy-heading real book; if
   it exceeds a small budget, stream it to a temp section instead of buffering. (v1 assumed "tiny".)
2. **Option-1 first-open doubling** — walk-then-replay ~doubles first-open per-block CPU. Measure on
   device before choosing Option 1 over Option 2.
3. **kContinuation streaming merge** — must merge a base + its continuations into one logical block
   without reading ahead into the NEXT logical block. Bounded, but the read-ahead boundary needs a
   test (a block that splits into 3 records, followed immediately by another block).
4. **Fragmentation over a full book build** — the writer/reader alloc/free one block repeatedly;
   even at bounded peak bytes this can fragment. Device gate 6; consider a small block-sized arena
   the writer/reader reuse (bump-reset per block) to avoid churn.

## Status

Plan v2. Supersedes v1's per-spine+Tee shape with block-streaming + Option-2 lazy write. Next code
step: Increment A (streaming ContentBinWriter + spine index, format v5), host-gated on the peak-RAM
and corrupt-write gates. No code changed yet.
