# Pipeline baseline — 2026-07-26 (Phase 3 relayout speedup)

Branch: `feat-stage1-extraction`. Host measurements (MSYS2 UCRT64, deterministic GfxRenderer
stub — see `test/epub_pipeline`). These record the Phase-3 Speed gate: a settings-change relayout
must be ≥3× faster once Stage-1 output (`content.bin`) is persisted and Stage-2 reads it back.

## Relayout speedup — the Phase-3 headline gate

Measured by `ContentBinSpeed.RelayoutIsFasterThanFullCompile` on **Moby Dick** (largest corpus
book), default profile, median of 3 reps:

| Path | What it does | Time (median) |
|---|---|---|
| Full (parse + layout) | ZIP → inflate → Expat → CSS → walk → LayoutSink. **What a settings change costs today** (no persisted content.bin). | ~1.6–1.7 s |
| Replay (content.bin) | read `content.bin` → Stage-2 layout only. **The settings-change fast path** once persistence is wired. | ~0.18–0.21 s |
| **Speedup** | | **~8–9×** |

- The plan's target is **≥3× faster relayout**. Host result **~8–9×** clears it comfortably.
- The host figure is a LOWER bound on the device win: the deterministic GfxRenderer stub makes
  measurement (word widths, line heights) near-free, so the layout portion is under-weighted, while
  the ZIP/inflate/Expat/CSS cost that persistence ELIMINATES is real on both. On device, where
  layout metrics are cheap flash reads but XML/CSS parsing is expensive, the ratio should be at
  least as large.
- The test asserts a conservative `>2×` (host noise floor); the printed `[ SPEED ]` line carries the
  real ratio.

## Correctness gate (context for the speed number)

The speedup is only meaningful because the fast path is byte-identical: `ContentBinReplayMatrix`
(94 cases = full corpus × 7-profile matrix + Moby default/narrow/hyphen) asserts the content.bin
read-back page dump equals a direct parse+layout, **including footnotes** (WBC1 v3). 94/94 green.

## Caveats / what this does NOT yet measure

- **Not yet wired on device.** `content.bin` write/read exists in `Section`-adjacent test drivers
  only; the device `Section` build does not yet persist or read it (that is action #1d). So the
  ≥3× win is proven achievable but not yet realized in the shipping reader. The host measurement
  uses the harness drivers `compileToContentBin` / `replayFromContentBin`.
- First-compile cost (Stage-1 = the full path) is unchanged by design — persistence pays off on
  the SECOND and later opens / settings changes, not the first compile.
- Device cold/warm open ms and settings-change re-pagination ms (plan's device timings) require the
  #1d wiring + a device run; not measured here.
- Memory (Stage-2 peak without expat/CSS working set) not re-measured this cycle; prior baselines
  `pipeline-baseline-2026-07-17.md` / `-19.md` hold for the parse path.

## Reproduce

```
cmake -S test -B build/test-msys -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/test-msys --target EpubPipelineTest
build/test-msys/epub_pipeline/EpubPipelineTest.exe --gtest_filter=ContentBinSpeed.*
```
