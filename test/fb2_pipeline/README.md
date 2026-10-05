# Native FB2 package/open regression

Builds the production FB2 converter, EPUB metadata loader, and chapter renderer with filesystem/FreeRTOS host adapters. No source books are modified. Use a fresh, disposable cache directory under /tmp: the runner deliberately truncates a generated section index and changes its version, then checks recovery and bookmark preservation.

```sh
cmake -S test/fb2_pipeline -B build/fb2-pipeline -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/path/to/cached/googletest
cmake --build build/fb2-pipeline --target fb2_pipeline -j 4
build/fb2-pipeline/fb2_pipeline test/fixtures/fb2_split_anchors.fb2 /tmp/new-fb2-test-cache
# A local .fb2 or .fb2.zip book may also be supplied.
```

The exit code must be zero. Checks include cold package creation, EPUB metadata loading, TOC targets, section-size count, truncated-index rejection, version-28 rebuild preserving bookmarks, warm reopening, and streaming the first chapter. Filesystem operation counters and wall-clock timings are reported by stage. This does not emulate ESP32 heap limits, physical SD latency, or display/page-layout behavior.

## Footnote navigation regression

Build `nav_index_test` and `fb2_notes` in the same directory. The first checks direct section offsets, ID collision handling, duplicate IDs, malformed navigation files, and bounded lookup reads. The second opens a real book, resolves IDs listed one per line in a text file, and writes sample XHTML plus filesystem-operation counters:

```sh
cmake --build build/fb2-pipeline --target nav_index_test fb2_notes -j 4
build/fb2-pipeline/nav_index_test
build/fb2-pipeline/fb2_notes /path/to/book.fb2 /tmp/notes-cache /path/to/ids.txt /tmp/notes-html
```

For the 2011 Russian Bible regression use `n1`, `n1337`, and `n2672`. The runner also renders the first text chapter containing a note link. Compare sample text against the source XML. An optional `FB2_NOTES_BASELINE_DIR` containing the earlier `Fb2.cpp` and `Epub.cpp` builds `fb2_notes_baseline` for identical before/after runs. Use separate disposable caches. Read counts measure host HalFile operations, not elapsed time on the device.

## Inline verse and table targets

`anchor_index_test` checks the bounded-memory ID index with 50,000 IDs, exact hash-collision comparisons, duplicate IDs and damaged files. `fb2_crossrefs book.fb2 cache-dir target-ids.txt output-dir` resolves every requested ID and renders every virtual chapter. Verify each mapped chapter contains the hashed target marker and compare generated link labels/counts with the original XML. Use a separate disposable cache for each run. The synthetic split-section fixture should cover early/middle/late paragraphs, title IDs, nested sections, table cells and an ID on a link.

FB2 table cells are emitted as sequential paragraphs to preserve targets and outgoing links through the existing page layout engine. The original column layout is not retained. The additional anchor cache invalidates only derived page/source caches on first upgrade; progress, bookmarks, metadata and covers are preserved.

## Encoding preparation and stack regression

`python3 test/fb2_pipeline/encoding_regression.py build/fb2-pipeline/fb2_pipeline /tmp/new-encoding-regression`
checks UTF-8, Windows-1251, KOI8-R, incorrectly declared UTF-8 and an absent declaration,
each in plain and zipped form. It checks cold/warm preparation, Cyrillic TOC and
transcoded text. The output directory must not exist. For the device stack regression,
inspect the ESP32-C3 disassembly: encoding preparation must not reserve the 4 KiB
sample or conversion buffers on the Arduino task's 8 KiB stack, even on the UTF-8 early return.

## Cold indexing comparison

Build a baseline pipeline from the previous revision, then compare with the current pipeline:

```sh
python3 test/fb2_pipeline/indexing_regression.py /path/to/baseline/fb2_pipeline build/fb2-pipeline/fb2_pipeline /tmp/new-index-comparison
```

The output directory must not exist. The script generates ordinary, long inline-anchor and short-chapter books, tests each as FB2 and FB2.ZIP, runs the full cache recovery/reopen regression, compares every anchor's target chapter, and reports storage operations. Use these counters to assess redundant I/O; host milliseconds do not predict physical SD/display timing.

## Original chapter continuity

`fb2_chapter_continuity NEW_CACHE_ROOT [fixture.fb2.zip]` builds the production
FB2 converter and real Section/page layout, generates a chapter with 1,200 paragraph
IDs (>24 KiB and >1,024 anchors), and checks uninterrupted prose pagination, all
paragraphs, late links/targets, the whole-chapter TOC page range, four images in one
section, byte-equivalent warm layout fingerprints, a targeted preview after the old
anchor limit, reading an early page during incremental construction, and removal of
scratch indices after cancellation. The opening title page is allowed to end early;
all non-final prose pages must extend below y=600 in the 760-pixel viewport.

```sh
cmake --build build/fb2-pipeline --target fb2_chapter_continuity -j 4
build/fb2-pipeline/fb2_chapter_continuity /tmp/fb2-continuity-plain
# Zip /tmp/fb2-continuity-plain/chapter.fb2, then exercise the same checks:
build/fb2-pipeline/fb2_chapter_continuity /tmp/fb2-continuity-zip /tmp/fixture.fb2.zip
```

Both cache directories must be new. The optional input must contain this generated
fixture, because assertions intentionally use its known paragraph/image/section counts.
The renderer uses deterministic host font metrics; this does not measure physical SD
latency or the ESP32 heap peak.
