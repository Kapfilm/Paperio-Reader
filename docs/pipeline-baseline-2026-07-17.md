# Pipeline host baseline — 2026-07-17

Phase-0 metrics baseline (see compiled-book-pipeline-plan.md). Every later
phase re-runs `test/epub_pipeline/run_baseline.sh` and appends a dated section
here so the migration has a continuous, comparable record.

Environment: Linux host, `epub_pipeline_dump --bench` (deterministic GfxRenderer
metrics stub, ESP heap gates stubbed wide open). Numbers are host-relative —
they track *regressions between phases*, not device performance. "cold" = fresh
cache dir (full compile: index + CSS + footnotes + every section build + dump
read-back); "warm" = same cache dir again (cache-hit path, still re-reads every
page for the dump). Peak heap is whole-process live-bytes peak over the cold
run (malloc/free interception, test/epub_pipeline/HeapTrack.cpp); cache KB is
the on-disk cache footprint after the cold run.

## Baseline (branch refactor-parsing, post d35ff2b7)

| book | spines | pages | cold ms | warm ms | peak heap KB | cache KB |
|---|---:|---:|---:|---:|---:|---:|
| test_br_section_break | 6 | 6 | 1.401 | 0.773 | 47 | 12 |
| test_display_none | 12 | 13 | 2.313 | 1.501 | 78 | 31 |
| test_float_images | 7 | 8 | 1.522 | 1.005 | 78 | 22 |
| test_font_sizes | 1 | 2 | 0.918 | 0.543 | 53 | 11 |
| test_headings | 1 | 2 | 0.743 | 0.399 | 49 | 8 |
| test_jpeg_images | 10 | 11 | 1.800 | 1.218 | 76 | 14 |
| test_kerning_ligature | 13 | 45 | 7.158 | 5.755 | 75 | 207 |
| test_mixed_images | 4 | 5 | 0.920 | 0.594 | 76 | 4 |
| test_png_images | 10 | 11 | 1.842 | 1.307 | 76 | 13 |
| test_spine_toc_edges | 14 | 77 | 9.676 | 7.257 | 75 | 320 |
| test_tables | 4 | 4 | 1.053 | 0.495 | 77 | 14 |
| test_text_rendering | 8 | 10 | 1.319 | 0.945 | 49 | 24 |
| moby-dick | 29 | 979 | 100.553 | 88.244 | 85 | 4812 |

Observations worth carrying into the later phases:

- Whole-run peak heap sits at 47–85 KB on host for every book, including a
  full-length novel — the working set is bounded by the pipeline's streaming
  design, not book size, which matches the plan's premise that a fixed arena
  (Phase 2) can replace the per-site heap gates.
- Warm runs only save ~10–30 % over cold on host because the dump read-back
  (every page deserialized) dominates at host I/O speeds; on device the ZIP +
  XML + CSS cost dominates instead, so the device baseline (plan Phase 0 step
  5, still pending hardware time) is the authoritative speed reference.
- moby-dick's cache (4.8 MB for a 1.2 MB EPUB) is ~4× source size — above the
  ≤1.5× budget Phase 3 sets for the compiled-content format; worth watching.

## Device baseline

Pending a device run (plan Phase 0 step 5): cold/warm open ms,
EpubReaderBenchmark forward/backward page-turn ms, free+contig heap at the four
checkpoints, heap-recovery restart count.
