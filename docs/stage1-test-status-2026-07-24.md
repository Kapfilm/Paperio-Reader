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

**Update 2026-07-25 — the 24px bug is FIXED; a smaller 4px gap remains.** A full vertical-space flow
analysis root-caused the dominant divergence as an **`<hr>` + `<br>`-gap double-injection**: the fused
`<hr>` handler flushes the empty `<br>` block with `makePages()` (which does NOT clear its
`fromBrElement`), then `startNewTextBlock(emptyStyle)` re-reads that flag and injects a SECOND br-gap
(one lineHeight = 24 px) into the block after the rule. `LayoutSink::placeHr` cleared
`pendingMergeFromBr_`, injecting the gap once. Fixed (commit "carry the <br>-gap past an <hr>"):
`placeHr` now re-establishes a pending fromBr merge after the rule. **Moby default diff: 105 → 85.**

**Remaining (~85 lines): a separate +4 px heading-spacing gap.** After a scaled heading (mult=1.400,
e.g. Moby's "EXTRACTS." front-matter), the following body paragraph sits 4 px lower in the fused than
the sink. Instrumentation confirmed the body block itself has identical margins/collapse
(`mTop=4 mBot=4 lastMBot=4`) in both paths — so the 4 px originates elsewhere in the heading→body
transition (heading marginBottom, or an intervening empty/wrapper block), not yet pinned to the exact
block (the divergent block's first words did not match the probes tried). Small, cosmetic, front-
matter only; zero word-level diffs. Deferred to step 6c (which deletes the fused reference) or a
follow-up.

Both remaining/​fixed issues are the class the model agent bounded: fused layout mutations invisible
to the producer that the sink must reconstruct. They do not affect the synthetic corpus (84/84) or
the shipping device (fused) path.

## Interpretation

Adding a real book did exactly what the design review predicted: it surfaced reconstruction-fidelity
bugs the synthetic corpus missed. Two are fixed and verified; one remains, documented above. None of
these affect the shipping device path (still the fused layout) or `content.bin` correctness — they
are LayoutSink-vs-fused equivalence gaps only. The remaining margin/pagination gap is the next
concrete item for whoever continues; once it (and any siblings it reveals) is closed, Moby Dick joins
the byte-identical gate. Alternatively, outer step 6c (delete the fused path, drive only the sink)
eliminates this entire bug class by construction, since there is then no fused reference to diverge
from — a reason to weigh 6c sooner.
