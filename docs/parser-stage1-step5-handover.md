# Handover — Phase 3 step 5 (LayoutSink) — COMPLETE; next is outer step 6 (unify)

Step 5 is **done**: LayoutSink reproduces the fused measure+paginate **byte-for-byte across the
full 12-book corpus × a 7-profile settings matrix (84 cases)**. Full real suite 426/426 green;
goldens unchanged throughout. Design: `parser-stage1-step5-design.md`. Master plan:
`compiled-book-pipeline-plan.md` Phase 3. Branch: `feat-stage1-extraction`.

Next work is the OUTER step 6 (unify) — see the last section.

## What step 5 is

Build `LayoutSink : compiled::BlockSink` (`lib/Epub/Epub/content/LayoutSink.{h,cpp}`) — the
Stage-2 consumer that reproduces the fused ChapterHtmlSlimParser measure+paginate **byte-for-byte**
from the walk's `onBlock` stream. ADDITIVE: the fused path is untouched and still ships; LayoutSink
is a parallel consumer proven equal by an equivalence gate. The actual unify (delete fused inline
layout, extract HtmlWalkCore) is the OUTER step 6, gated by this.

## Verification harness

`test/epub_pipeline/LayoutSinkTest.cpp` — `LayoutSinkEquivalence.PageDumpMatchesFused` runs each
corpus book through BOTH `runAndDump` (fused) and `layoutViaSink` (LayoutSink driver in
`PipelineRunner.cpp`) and asserts the page dumps are byte-identical. `DUMP_DIFF=1` env var writes
both dumps to `$TEMP/layoutsink_diff/<book>.{fused,sink}.txt` on mismatch for `diff`.

Build/run (Windows MSYS2 UCRT64 or Linux):
```
cmake -S test -B build/test-msys -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/test-msys --target EpubPipelineTest
ctest --test-dir build/test-msys -R "LayoutSink|MatchesGolden|ContentSink|Stage1Producer"
```

## Done (commits, newest last)

- **5.1** skeleton + LayoutParams + reconstruction test.
- **5.2** text path: empty-block merge, <br> gap/neutral style, makePages, addLineToPage, >96-word
  split, onSpineEnd. **Settings-split** (validated vs microreader, see the memory note): heading
  multiplier / <li> indent / span poem-indent are folded into the producer's captured CssStyle
  (settings-independent → both sinks reproduce); ALIGNMENT stays sink-side (user-setting-dependent).
- **5.3a** extracted `compiled::computeImageDisplaySize` (`content/ImageLayout.{h,cpp}`) — shared by
  the fused <img> path and LayoutSink. Threaded the image's CSS through the seam (stage1EmitImageBlock
  now forwards imgStyle, was CssStyle{}).
- **5.3b** LayoutSink block-image placement. `normalizePath` in the dump now canonicalizes the
  settings-derived `img_<spine>_<hash>_` prefix (image-bearing goldens regenerated for THIS only).
- **5.3c** LayoutSink float images + active-float zone propagation + deferred-image yPos.
- **5.3d** HR crosses the seam as **BlockType::Hr = 3** (new empty block variant; WBC1 additive):
  model + (de)serialization + stage1EmitHrBlock producer + ContentSink + LayoutSink::placeHr.

**Byte-identical fused-vs-sink now:** test_headings, test_font_sizes, test_br_section_break,
test_png_images, test_jpeg_images, test_mixed_images, test_float_images, test_text_rendering (8/12).
No regression: all goldens, ContentSink, Stage1Producer green throughout.

## Commits 4-6 — DONE (2026-07-24)

- **5.4 tables**: extracted shared `compiled::packTableFragments` (`content/TableLayout.{h,cpp}`)
  driven by a `TablePageContext` callback; both the fused parser and LayoutSink use it. Fixed
  `onSpineEnd` to emit a cover-only page (no text block). `test_tables` byte-identical.
- **5.5 footnotes + TOC page-breaks**: `onFootnote` buffered + assigned in addLineToPage; TOC-
  boundary anchors transmitted as `kPageBreakBefore` (producer has `tocAnchors`); `<br>` text-
  indent leak fixed. Full corpus byte-identical at default profile.
- **5.6 settings matrix**: 7 profiles. Fixed table-cell `ParsedText(false,false)`, paragraph-
  fallback default alignment, `<pre>` spacing via new `kPreformatted` flag (bit 7), consecutive-
  `<hr/>` empty-block layout. 84/84 matrix cases byte-identical.

## (historical) Remaining — now all complete

### Commit 4 — tables
Port the table layout: `emitBufferedTable` / `emitTableAsFragments` / `emitTableAsParagraphs` /
`emitCellImagesAsBlocks` / `buildCellImage` (ChapterHtmlSlimParser.cpp ~2740-3060). The producer
already emits `BlockType::Table` with rows/cells (words + optional cell image). LayoutSink must
reproduce the grid-vs-paragraph decision (font-dependent) and PageTableFragment placement.
Gate book: **test_tables**. Watch: cell-image intrinsic dims aren't currently probed at compile
(noted in stage1EmitTableBlock) — check test_tables doesn't rely on them, else transmit like <img>.

### Commit 5 — side outputs (anchors / chapters / labels / footnotes)
LayoutSink stubs these today: onAnchor stashes to pendingAnchorId_ but the anchor→page recording,
the onChapter TOC-boundary forced page break (tocAnchors), onFootnote→currentPage->addFootnote
(the pendingFootnotes machinery in addLineToPage — currently DROPPED in the sink), and the xpath
LUT (xpathParagraphIndex/listItemIndex, currently always 0 in the sink) are NOT wired. The fused
parser exposes getAnchors()/getPageBreakLabels()/getParagraphLutPerPage(); add matching getters to
the driver and assert equality. Gate books with footnotes/covers: **test_kerning_ligature,
test_spine_toc_edges** (both have cover IMG + many footnotes; the FN lines currently differ —
sink shows footnotes=0). NOTE: the xpath indices are fed by the walk (body-child byte offsets),
which the sink doesn't see — may need the producer to transmit them, OR accept that the LUT is a
fused-only concern until step 6. Decide when you get there.

### Commit 6 — full matrix gate
Once all block kinds + side outputs pass, expand LayoutSinkEquivalence to the whole 12-book corpus
× the settings Profiles (mirror EpubPipelineTest's profile matrix if it has one; currently the
gate uses the default Profile only). Byte-identical across the matrix = step 5 complete.

## Then: OUTER step 6 (separate, larger)
Unify — delete the fused inline layout, make the parser drive ONLY sinks, split into HtmlWalkCore +
LayoutSink glue. Sever the 4 remaining walk-side GfxRenderer uses (cpp:1200,1562,1694,2896 — em→px
sizing). The shared ImageLayout helper and the settings-split folds already did much of the seam
groundwork. Full equivalence gate is the proof.

## Watch-outs carried from earlier
- tjpgd.h `_WIN32` guard is fixed (memory: tjpgd-win32-stdint-conflict) — Windows host builds work.
- The >96-word split in the sink lays out the WHOLE block at once (the producer sends one Block);
  it re-applies the 96 rule in layoutTextBlock. Kept identical so far, but any table/footnote work
  that changes word accounting must preserve it.
