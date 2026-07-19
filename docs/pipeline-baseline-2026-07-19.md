# Pipeline baseline — 2026-07-19 (post-arena, post-FB-borrow)

Second dated section for the migration record (see compiled-book-pipeline-plan.md
and the first baseline, `pipeline-baseline-2026-07-17.md`). Captures the state
after **Phase 2** landed and was device-validated: arena-backed build path
(`EPUB_BUILD_ARENA=1` default), secondary-framebuffer **borrow** for released
Background-C builds, and CSS resolution relocated **into the borrow arena**
(resident ruleset when it fits, else offset index).

## Host (`epub_pipeline_dump --bench`)

Same harness/method as 2026-07-17 (deterministic GfxRenderer stub, heap gates
stubbed wide; "cold" = full compile, "warm" = cache-hit re-dump; peak = whole-run
live-bytes peak; cache KB = on-disk footprint after cold). Numbers are
host-relative regression trackers, not device performance.

| book | spines | pages | cold ms | warm ms | peak heap KB | cache KB |
|---|---:|---:|---:|---:|---:|---:|
| test_br_section_break | 6 | 6 | 1.262 | 0.812 | 50 | 12 |
| test_display_none | 12 | 13 | 2.364 | 1.578 | 87 | 31 |
| test_float_images | 7 | 8 | 1.483 | 1.028 | 87 | 22 |
| test_font_sizes | 1 | 2 | 0.866 | 0.482 | 62 | 11 |
| test_headings | 1 | 2 | 0.664 | 0.394 | 57 | 8 |
| test_jpeg_images | 10 | 11 | 1.863 | 1.257 | 86 | 14 |
| test_kerning_ligature | 13 | 45 | 6.646 | 5.243 | 84 | 207 |
| test_mixed_images | 4 | 5 | 0.716 | 0.596 | 85 | 4 |
| test_png_images | 10 | 11 | 1.406 | 1.014 | 85 | 13 |
| test_spine_toc_edges | 14 | 77 | 8.124 | 6.683 | 84 | 320 |
| test_tables | 4 | 4 | 1.089 | 0.507 | 86 | 14 |
| test_text_rendering | 8 | 10 | 1.304 | 0.897 | 51 | 24 |
| moby-dick | 29 | 979 | 104.651 | 86.811 | 91 | 4812 |

Deltas vs 2026-07-17:

- **Timings**: within run-to-run noise (±5 %); no regression.
- **cache KB**: byte-identical — goldens are unchanged across the whole Phase-2
  work (the equivalence gate held).
- **peak heap**: up ~9 KB per book (e.g. display_none 78→87, moby 85→91). This is
  the up-front 10 KB owned `BuildArena` (`SCT_PARSE_ARENA_BYTES`), now the default
  allocation model, not a per-build regression — it *replaces* transient per-site
  allocations rather than adding to a peak.
- The CSS-arena change specifically is **host-neutral**: the host builds run
  against the *owned* arena, and CSS on that path stays heap-backed
  (`setIndexArena(nullptr)`), so only the borrowed-FB device path exercises the
  new resident/index arena code (covered by the `CssParserArena` gtest instead).

## Device (X3, `EPUB_BUILD_ARENA=1`, refactor-parsing @ f9e989fe)

Authoritative speed/memory reference. Measured on the cold-cache first open of
**"Lindsey Davis — Shadows in Bronze"** (108 spines, 178 CSS selectors), spine 60
(`ch50.html`, 8 pages) — the section whose CSS-heavy build reproduced the pinned
degraded-mode failure during earlier ZIP-arena validation.

### Borrowed section build (spine 60, cold cache)

| metric | value |
|---|---|
| build mode | `INCR_RELEASED` → **secondary buffer BORROWED** (size 52272) |
| CSS load | **RESIDENT in arena**, 178 selectors, 21360 bytes, **0 disk lookups** |
| CSS resolve stats | calls=13, **lowHeapSkips=0**, lowHeapDiskBypasses=0 |
| build result | **complete, 8 pages** (no "incomplete", no released fallback) |
| build time | createSectionFile total 1157 ms (setup 104 / parse 388 / finalize 24) |
| arena | cap 52272, **highWater 22384**, failedAlloc 0, zipHW 0 |
| buffer return | `returnSecondary -> 1` (cannot fail — never entered the heap) |
| page render | 524 / 528 / 523 ms, AA on (`Deferred AA planes 126 / gray 228 ms`) |
| min free heap mid-build | ~23.9 KB (clears the 24 KB lean CSS floor; no fault) |

### Before → after the CSS-arena fix (same book, same section)

| | before (borrow only) | after (borrow + arena CSS) |
|---|---|---|
| CSS during build | heap, dipped to 40212 < 40 KB floor | **resident in arena, no disk** |
| `lowHeapSkips` | 2 (degraded) | **0** |
| build outcome | flagged "incomplete" → released rebuild | **complete, 8 pages** |
| secondary buffer | released rebuild → `reallocSecondary -> 0` | **borrowed → returned, clean** |
| recovery | **heap-recovery reboot** → 1883 / 3079 ms pages | none → **524 ms pages** |
| user-visible | second indexing popup + reboot | single smooth open |
| arena highWater | 15616 (52 KB block ~30 % used) | **22384** (CSS now uses the slack) |

Result: the borrow's realloc-failure / hole-fragmentation / heap-recovery-reboot
class is eliminated *by construction* for CSS sections — they now complete inside
the borrowed block instead of falling back to the released path that reintroduced
the bug.

## Phase 2 exit gate

- Functionality: goldens byte-identical (host); device smoke clean on the pinned
  book. ✅
- Memory: arena `highWater` well under `cap` on every observed build
  (22384 / 52272 here); no heap-recovery restarts on the validated path;
  `failedAlloc=0`. ✅
- Speed: host neutral; device chapter build ~1.16 s, page turns ~0.52 s. ✅
- **Open**: the `*_MIN_FREE_HEAP_BYTES` gate defines are not yet removed. Two still
  actively gate the **Background-B resident-build** CSS decision
  (`EpubReaderActivity.cpp` ~898/914), a path that still resolves CSS on the heap —
  so they guard a real decision and are a scoped follow-up, not a blind delete.
  Tracked separately from this baseline.
