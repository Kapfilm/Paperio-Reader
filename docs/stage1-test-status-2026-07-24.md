# Stage-1 / LayoutSink test status (2026-07-24)

Snapshot of what is tested and what passes/fails on branch `feat-stage1-extraction`, after the
design-review fixes and adding a real book (Moby Dick) to the equivalence gate. Companion to
`stage1-design-review-2026-07-24.md`.

## How to run

```
cmake -S test -B build/test-msys -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/test-msys --target EpubPipelineTest CompiledContentTest SaxParserTest \
  FloatLayoutTest LayoutQualityTest WordSizeLayoutTest DifferentialRoundingTest MemoryModelTest \
  BuildArenaTest ContentOpfParserTest MdParserTest NaturalSortTest OpdsParserTest ZipEntryReaderTest \
  BufferedFileIOTest CssGapsTest GifDecoderTest JpegDecoderTest HyphenationEvaluationTest \
  OpdsFilenameTest OpdsFormatLabelTest SerialTransferTest UrlUtilsTest
ctest --test-dir build/test-msys -E "NOT_BUILT|EpubParserBenchmark|EpubCssPerformanceTest"
```
`DUMP_DIFF=1` on the pipeline binary writes fused vs sink dumps to `$TEMP/layoutsink_diff/` on any
equivalence mismatch — the primary debugging tool.

The 3 excluded targets (`EpubParserBenchmark`, `EpubCssPerformanceTest`, `epub_pipeline_dump`) use
`<dlfcn.h>` and only build on Linux; unbuildable on the Windows/MSYS2 host (not a coverage gap).

## PASSING

### Full real suite — GREEN
Every correctness gtest builds and passes. As of the last full run: **428/428** (the count grows as
tests are added; the point is zero real failures). This includes:
- `CompiledContentTest` (WBC1 round-trip, table, dedup, empty, bad-magic) — exercises the format
  changes (BlockType::Hr, the split-cap fix, the unknown-type reject).
- `ContentSinkTest` (round-trip, 8 KB split, the new long-word text-cap regression, determinism,
  image blocks).
- `Stage1ProducerTest` (block/word/anchor/chapter/footnote/table/float emission, determinism).
- `LayoutSinkTest` unit tests (BlockStyle reconstruction, skeleton, label recording, XPath LUT).
- All non-pipeline suites (SaxParser, CssGaps, Float/Layout/WordSize, Opds*, Md, hyphenation, etc.).

### LayoutSink equivalence — SYNTHETIC CORPUS GREEN
`LayoutSinkEquivalence.PageDumpMatchesFused`: the LayoutSink page dump is **byte-identical** to the
fused path across the **whole 12-book synthetic corpus × the 7-profile settings matrix = 84 cases**
(default, bigFont, leftAlign, spacing+lineCompression, narrow, hyphen, noEmbed). This is the
step-5 gate and it holds after every fix in this session.

### Goldens — GREEN
`EpubPipelineTest.MatchesGolden` (the fused device path) — all 12 synthetic corpus books byte-
identical vs committed goldens. Unchanged by this session's fixes (the producer folds and page-break
transmission are additive to the fused output).

## FAILING

### Moby Dick real-book equivalence — FAILING (documented, not yet fixed)
`LayoutSinkEquivalence.PageDumpMatchesFused/moby_dick_epub_*` — Moby Dick (`test/fixtures/
moby-dick.epub`, 836 KB, 29 spines) was added to the gate at 3 profiles (default, narrow, hyphen)
per the plan's "real books are part of the critical gate." It currently **FAILS** the default
profile. This is the whole reason for adding it: the synthetic corpus never exercised these patterns.

**Progress this session:** two real sink bugs were found and fixed (this commit + the alignment/
page-break commit), cutting the default-profile diff from **608 → ~105 lines**. What was fixed:
- Empty-block alignment reset (Moby's `<p class="toc">CONTENTS</p>` centered vs justified) — FIXED.
- CSS `page-break-before: always` on `<h2>` not transmitted (most of the 608) — FIXED.

**Remaining divergence (~105 lines, 2 of 29 spines):** spine 1 and spine 28 each diverge by one
page. Root cause localized but NOT fixed: a **top-margin / pagination reconstruction difference at
Project Gutenberg boilerplate boundaries**. Concretely, in spine 28 the fused path ends a page at
the "…redistribution." line and pushes `START: FULL LICENSE` onto a fresh page, while the sink fits
`START` on the same page (y=696). Verified NOT to be a `page-break-before` (the `#pg-footer` /
`#project-gutenberg-license` divs carry no such CSS at that point). It is consistent with the sink
applying **less top margin** to that block than the fused path, so one extra line fits per page and
the page count drifts (15→14 pages in spine 28, 32→31 in spine 1). Spine 1 shows the same signature
(a `mult=0.950` centered block starting a page 24 px higher in the sink than the fused).

This remaining bug is in the same class the model agent bounded: a fused layout mutation the producer
does not transmit / the sink does not reconstruct. It is localized (2 spines, one margin/pagination
behavior) and does not affect the synthetic corpus or the device (fused) path.

## Interpretation

Adding a real book did exactly what the design review predicted: it surfaced reconstruction-fidelity
bugs the synthetic corpus missed. Two are fixed and verified; one remains, documented above. None of
these affect the shipping device path (still the fused layout) or `content.bin` correctness — they
are LayoutSink-vs-fused equivalence gaps only. The remaining margin/pagination gap is the next
concrete item for whoever continues; once it (and any siblings it reveals) is closed, Moby Dick joins
the byte-identical gate. Alternatively, outer step 6c (delete the fused path, drive only the sink)
eliminates this entire bug class by construction, since there is then no fused reference to diverge
from — a reason to weigh 6c sooner.
