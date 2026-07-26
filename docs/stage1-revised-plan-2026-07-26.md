# Stage-1 — revised plan to resolve the streaming/memory deficiencies (2026-07-26)

Supersedes the two-pass device-wiring design (`...content-bin-device-wiring-design-2026-07-26.md`)
for the shape of the device integration. Grounded in the reassessment
(`stage1-reassessment-2026-07-26.md`), which found the content.bin pipeline **as built** is a
streaming + memory regression: whole-book blob, whole-RAM model, no random-access reader, and a
"compile-whole-book-before-page-1" barrier.

## The three coupled deficiencies (what we must fix)

1. **Whole-book RAM.** `ContentSink` accumulates the entire book's `CompiledContent` before any
   write; `readContentBin` reconstructs the whole book in RAM. On 380 KB this defeats objective 4
   (memory drop) — the current per-spine build holds one spine and streams pages to disk.
2. **No random-access reader.** The only reader is whole-file `readContentBin`. The designed
   `IBlockSource` (sliding window over content.bin) does not exist. Stage-2 cannot read spine N or
   the first K blocks without loading everything.
3. **First-page barrier.** content.bin does not exist until every spine compiles, and replay
   re-buffers per spine — so a two-pass first open is *slower* than today's streaming build, which
   already serves page 1 mid-build (`onPageComplete` → `loadPageFromActiveBuild`, A/B/C scheduler).

These are coupled: fixing (3) well requires (1) and (2). The target end state is a **write-through,
per-spine, randomly-readable** content.bin that never regresses first-page latency or peak memory.

## Design principles (non-negotiable, from the objectives)

- **Never hold the whole book in RAM.** Per-spine granularity everywhere: compile a spine, flush it,
  release it. Read with a bounded window.
- **First page is sacred.** The first open must stream page 1 as fast as today. content.bin is a
  *by-product* of the first build (write-through), never a prerequisite computed before pages.
- **content.bin stays settings-independent**, keyed on the ZIP fingerprint; the section (page)
  cache stays settings-keyed. A settings change drops pages, keeps content.bin.
- **Byte-identical gate throughout.** Every step keeps the section-cache page dump byte-identical to
  today's direct path (host `ContentBinReplayMatrix` + a new Section-level equivalence test).

## Target architecture

```
FIRST open (nothing cached), per spine, streaming:
  walk spine i  ──►  TEE ──►  LayoutSink  ──► pages stream to section cache (page 1 available NOW)
                       └────►  ContentSink ──► append spine i's records to content.bin + spine index
  (one walk, both consumers; the walk already produces the same transcript for both)

SETTINGS change / rebuild (content.bin valid), per spine, streaming:
  IBlockSource.readSpine(i)  ──►  replay blocks  ──►  LayoutSink  ──► pages stream to section cache
  (no ZIP/XML/CSS; bounded RAM via the sliding window; page 1 available as soon as spine 0 replays)
```

## Format change — per-spine, indexed content.bin (WBC1 v5)

- **Layout**: header (magic, version, fingerprint, spineCount) + a **spine offset table**
  (`uint32 byteOffset` per spine, patched after each spine is written) + the style pool + then each
  spine's records written back-to-back. The style pool is book-level (small, tens of entries) —
  keep it whole at the front, OR make it per-spine if a book's pool is large (measure first; likely
  keep book-level).
- **Streaming write**: a new `ContentBinWriter` that `writeSpine(SpineContent&&)` appends one spine
  and records its offset, then `finish()` back-patches the spine table + fingerprint. `ContentSink`
  flushes and RELEASES each spine at `onSpineEnd()` instead of accumulating. Aux tables (anchors,
  labels, chapters) are per-spine already; chapters are book-level but tiny — buffer or write a
  chapter section at finish (bounded).
- **Random-access read**: `IBlockSource` opens the file, reads the header + spine table, and
  exposes `readSpine(i) -> SpineContent` (seek to offset, read just that spine). A sliding-window
  cache holds ≤N spines (N=1–2) so Stage-2's per-spine replay never loads the whole book.
- Reuse the existing per-block/8 KB split — it already bounds per-record read allocation.

## Sequenced increments (each host-gated, then device-validated on hardware)

**Increment A — per-spine streaming writer + spine index (format v5).**
- Add the spine offset table + `ContentBinWriter::writeSpine/finish`; make `ContentSink` flush per
  spine (release memory). Keep `writeContentBin(whole CompiledContent)` as a thin wrapper over the
  writer for existing callers/tests.
- Gate: round-trip test (write per-spine, read back whole) equals today's model; peak-RAM assertion
  in the host harness (compile Moby, assert resident `CompiledContent` never exceeds ~1 spine).

**Increment B — `IBlockSource` random-access reader + sliding window.**
- Implement `IBlockSource` over the v5 file: header/spine-table parse, `readSpine(i)`, bounded LRU
  window. `replayFromContentBin` switches to read spine-by-spine via `IBlockSource` (drop the
  whole-file `readContentBin`).
- Gate: `ContentBinReplayMatrix` stays byte-identical (now driven per-spine through IBlockSource);
  peak-RAM assertion on replay (≤ window size).

**Increment C — TEE sink (fan-out) so first compile is write-through.**
- A `TeeBlockSink` that forwards every BlockSink call to BOTH a LayoutSink and a ContentSink. One
  walk drives both; pages stream out of LayoutSink exactly as today while ContentSink writes
  content.bin per spine. This resolves the one-sink `effectiveSink()` invariant by making the tee
  the single sink.
- Gate: section-cache page dump byte-identical with the tee vs LayoutSink-only; content.bin
  produced is fingerprint-valid and replay-equivalent. First-page latency unchanged (host timing).

**Increment D — wire into `Section`, behind `EPUB_STAGE1`, per-spine + streaming.**
- `runBuildSetup/Parse`: if a fingerprint-valid content.bin has spine i, replay it via IBlockSource
  → LayoutSink → section cache (fast path, no parser). Else walk with the Tee (write-through),
  streaming pages via the existing `onPageComplete`/`loadPageFromActiveBuild` so page 1 stays
  mid-build-available. Fold into A/B/C: a settings change makes Background-C replay from records
  instead of re-parsing.
- Gate (host): section cache byte-identical flag-on vs flag-off. Gate (device): first-open cold/warm
  ms ≤ baseline+10%; settings-change relayout shows the ~8–9× win; background-build slicing under
  budget; SD write cost within the ≤1.5×-source storage budget; smoke script (anchor nav, footnotes)
  clean.

**Increment E — retire the book-keyed HTML cache** (Phase 3 step 6) once content.bin covers its
role; measure net storage.

## What this buys per objective

- **Memory (obj 4):** per-spine write + windowed read means peak ≈ one spine, not the whole book →
  the drop the plan wanted (vs today's parse working set, Stage-2 also sheds expat/CSS).
- **First feedback (obj 5):** the Tee makes the FIRST open stream page 1 exactly as today, and
  leaves content.bin behind; subsequent opens/settings-changes replay per-spine (page 1 after spine
  0, not the whole book).
- **Speed (obj 3):** the ~8–9× relayout is preserved (still no ZIP/XML/CSS), now with per-spine
  streaming so it composes with A/B/C's incremental "build-while-you-read".
- **Streaming/parallel (the (d) question):** yes — per-spine + IBlockSource + Tee is exactly what
  lets Stage-2 render early pages before the whole book (or even the whole spine, via the existing
  >96-word intermediate flush) is done, using the SAME background-scheduler machinery that ships
  today.

## Risk / sequencing notes

- A→B→C are host-only and fully gated; D is the shipping-path + device step. Do NOT start D before
  A–C are green and the peak-RAM assertions hold.
- The Tee (C) is the piece that makes content.bin a pure win; if C proves hard, the fallback is a
  background POST-first-open compile (compile content.bin lazily after page 1 is served) — slower to
  first relayout benefit but never regresses first open. Prefer the Tee.
- Keep the byte-identical gate as the spine — it has caught every regression so far (the <pre>
  desync, the Moby page-break segfault, the 8 KB-split replay divergence).

## Status

Plan only. Supersedes the two-pass device-wiring shape. Next code step: Increment A (per-spine
streaming writer + spine index, format v5), host-gated with a peak-RAM assertion.
