# Stage-1 extraction — handover (2026-07-24)

State of the compiled-content / LayoutSink work on branch `feat-stage1-extraction`. Read this
first, then the design docs it points to. Everything below is committed and green unless marked.

## Where we are

- **Phase 3 steps 1–5: DONE and host-verified.** `LayoutSink` (`lib/Epub/Epub/content/
  LayoutSink.{h,cpp}`) reproduces the fused `ChapterHtmlSlimParser` measure+paginate **byte-for-byte
  across the full 12-book corpus × a 7-profile settings matrix (84 cases)**. Full real test suite
  **427/427** green. Goldens unchanged throughout; `content.bin`/ContentSink (step 4) unaffected.
- **Step 6 (unify): IN PROGRESS. 6a done; 6b–6e remain.**

Design docs (authoritative, read in this order):
- `stage1-extraction-design.md` — the 6-step contract.
- `parser-stage1-step5-design.md` + `parser-stage1-step5-handover.md` — LayoutSink build (steps 5.1–5.6).
- `parser-stage1-step6-design.md` — the unify plan (6a–6e), risks, sequence. **The plan of record.**
- `compiled-content-format.md` — the WBC1 on-disk format.

Live continuation state also in memory: `stage1-layoutsink-progress`, `stage1-settings-split-vs-microreader`.

## Verify (Windows MSYS2 UCRT64 or Linux)

```
cmake -S test -B build/test-msys -G Ninja -DCMAKE_BUILD_TYPE=Release
# build ALL real gtest targets (not just EpubPipelineTest) for the full 427-suite:
cmake --build build/test-msys --target EpubPipelineTest CompiledContentTest SaxParserTest ... (see below)
ctest --test-dir build/test-msys -E "NOT_BUILT|EpubParserBenchmark|EpubCssPerformanceTest"
```
The 3 excluded targets (`EpubParserBenchmark`, `EpubCssPerformanceTest`, `epub_pipeline_dump`) use
`<dlfcn.h>` — Linux-only malloc interposition; unbuildable on Windows, NOT a coverage gap. The
"NOT_BUILT" ctest rows are placeholders for targets you didn't build, not failures.

Equivalence harness: `test/epub_pipeline/LayoutSinkTest.cpp` — `LayoutSinkEquivalence.
PageDumpMatchesFused` runs each (book, profile) through both `runAndDump` (fused) and
`layoutViaSink` (sink) and asserts the page dumps are byte-identical. `DUMP_DIFF=1` writes both to
`$TEMP/layoutsink_diff/<tag>.{fused,sink}.txt` on mismatch — the primary debugging tool; use it.

## Remaining work (step 6)

**6b — parser owns an internal LayoutSink; route output through it.** Construct a `LayoutSink`
inside `ChapterHtmlSlimParser` from its own members (all `LayoutParams` are already parser ctor
args/fields; `imageBasePath`/`epubFilePath` come from Section — see Section.cpp:697-705). Point the
internal sink's `completePageFn` at the parser's `completePageFn`; make `getAnchors()/
getPageBreakLabels()/getParagraphLutPerPage()` proxy the sink. The fused inline layout STILL runs
but its output is bypassed. **This is the moment the shipping device path switches from the inline
layout to the sink** — so re-verify the SHIPPING goldens (device path, `stage1Sink_` null) stay
byte-identical. The step-5 matrix already proves the sink == fused, so this should be safe; the
shipping goldens are the gate. Watch the `LayoutLutEntry` ↔ `ParagraphLutEntry` type adaptation
(field-compatible; the getter proxy converts) and Section's hard `LUT.size()==pageCount` check.

**6c — delete the fused inline-layout half.** With the internal sink as the output driver, delete
the inline-layout lines at each dual-drive site. The step-6 map (in the design doc / this session's
agent output) lists exact line ranges per site: `flushPartWordBuffer` >96-word split, `makePages`,
`addLineToPage`, `emitPage`, `resolveBlockFont`, `effectiveLineHeight`, `startNewTextBlock`'s layout
half, `attachPendingFloatImage`, `placeImageBlockAsBlock`, `buildCellImage`, the `emitTableAs*`
family, the `<img>`/`<hr>` placement, and the layout state members. **Keep the producer half.**
Delete conservatively — one dual-drive site per commit — because the halves interleave.

**6d — sever the walk-side renderer em→px uses.** Once nothing consumes px `BlockStyle` in the walk,
the walk produces only `CssStyle` (pre-px) and the sink does em→px. Sever these 3 (classified in the
map): `ChapterHtmlSlimParser.cpp:786` (`<br>` gap line-height), `:1234` (`<img>` emSize for
`computeImageDisplaySize`), `:1528` (emSize for `BlockStyle::fromCssStyle`). The `<hr>`/
`effectiveLineHeight`/`emitTableAsFragments` renderer uses leave with the layout in 6c.

**6e — extract HtmlWalkCore + rename.** Split the settings-independent walk into `HtmlWalkCore`;
`ChapterHtmlSlimParser` becomes the thin Stage-2 entry owning an `HtmlWalkCore` + a `LayoutSink`.
Public contract unchanged. Can be a follow-up if large — 6c/6d deliver the functional unify.

## Architectural decisions taken (and why)

1. **Additive, verify-first, delete-last.** Steps 2–5 built the producer + LayoutSink as a PARALLEL
   consumer with the fused path untouched, proven byte-identical BEFORE any deletion. Step 6 flips
   then deletes. This kept every commit green and the golden risk bounded.

2. **Settings-split rule** (validated against `CidVonHighwind/microreader`'s .mrb; memory
   `stage1-settings-split-vs-microreader`): a walk-derived `BlockStyle` field that depends on a USER
   SETTING stays sink-side (resolved in LayoutSink); a settings-INDEPENDENT one is folded into the
   producer's captured `CssStyle` so BOTH sinks (ContentSink→content.bin, LayoutSink) reproduce it.
   Applied to: heading `kHeadingMultiplier` → CssStyle.fontSizeMultiplier (folded); `<li>` depth
   indent → marginLeft (folded); span poem-indent → textIndent (folded); ALIGNMENT stays sink-side
   (depends on paragraphAlignment); `<pre>` spacing stays sink-side (gated by a flag). This is the
   load-bearing invariant — apply it to any new block field.

3. **Shared helpers, not duplication, for stateless-enough layout.** `computeImageDisplaySize`
   (`content/ImageLayout.*`) and `packTableFragments` + `TablePageContext` (`content/TableLayout.*`)
   are called by BOTH the fused parser and LayoutSink. They are the natural step-6 seams and avoid
   two copies drifting. Image scaling was a pure function; table packing needed a page-context
   callback (it drives page emission). Text/float/HR layout was too state-coupled to share cleanly —
   ported into LayoutSink instead (to be de-duplicated when 6c deletes the fused copy).

4. **WBC1 extended additively.** `BlockType::Hr` (HR didn't cross the seam at all), `kPageBreakBefore`
   (TOC-boundary page breaks — the producer computes them from tocAnchors, which only the walk has),
   `kPreformatted` (bit 7, `<pre>` spacing suppression). All additive — old readers unaffected.

5. **XPath LUT via a non-serialized hook (6a), not content.bin.** `onXPathAdvance` transmits the
   walk's `<p>`/`<li>` counters + body-child byte offset per block. The counters are settings-
   independent (pure XML) but the LUT that maps them to PAGES is settings-dependent, so it's a sink
   concern, not content. Kept it a transient BlockSink hook (default no-op), not a serialized Block
   field.

6. **Image cache-path dump normalization.** The dump's `normalizePath` canonicalizes the settings-
   derived `img_<spine>_<propertyHash>_` prefix so the equivalence compares image IDENTITY (spine,
   counter, ext), not the section propertyHash (which the sink doesn't recompute).

## Concerns / watch-outs (ranked)

1. **6b is the real risk moment**: it switches the SHIPPING device path from the fused inline layout
   onto LayoutSink. The step-5 matrix proves equivalence on the synthetic corpus, but real books are
   more varied. Gate 6b on the shipping goldens; consider running a few real EPUBs through the device
   build (or the `run` skill) before/after 6b. If a divergence appears, it's almost certainly a
   sink-side layout edge the corpus didn't exercise — DUMP_DIFF will localize it.

2. **Getter-value equivalence is proven only indirectly.** Per the 6a scope decision, the sink's LUT
   is verified by (a) the size==pages invariant across the matrix and (b) a unit test of the
   onXPathAdvance→emitPage mechanism — NOT a direct value-diff against the fused parser's LUT (that
   needed replicating Section's CSS/tocAnchor setup, deemed too much harness for the payoff). The
   page-dump matrix proves pagination matches, and the LUT derives from the same page breaks + walk
   counters, so risk is low — but if 6b ever shows a KOReader XPath→page mismatch on a real book,
   this is the first place to add a direct value-diff.

3. **Footnote-preview renderer residue.** `abbreviateInlineFootnote` (`ChapterHtmlSlimParser.cpp:164,
   174`) measures text with the renderer during the WALK — not layout. It blocks a FULLY renderer-
   free walk (6d/6e). Options: move preview abbreviation to Stage-2, pre-measure it, or keep a
   `GfxRenderer&` on the walk solely for this. Recommend keeping it for now and documenting it as the
   one remaining walk renderer dependency — a truly renderer-free `HtmlWalkCore` is a follow-up, not
   a blocker for the functional unify.

4. **Cell-image intrinsic dims not transmitted.** `stage1EmitTableBlock` sets a cell's
   `imageEntryPath`/`imageAlt` but leaves `imageWidth/imageHeight` at 0 (noted in-code). No corpus
   book uses grid cell images, so LayoutSink doesn't reproduce them. If a real book has an image
   inside a grid `<td>`, the sink will mis-size it. Transmit the intrinsic dims (like the `<img>`
   path already does) when this surfaces.

5. **Duplication debt until 6c.** LayoutSink currently DUPLICATES the fused text/float/HR layout
   (the shared helpers only cover image scaling + table packing). Any bug fix to layout must touch
   BOTH copies until 6c deletes the fused one. The equivalence matrix catches drift, but it's a
   maintenance hazard — 6c should follow 6b promptly.

6. **>96-word split reproduction.** The producer emits a block as ONE `compiled::Block` (all words);
   LayoutSink re-applies the >96-word split rule in `layoutTextBlock`. It's byte-identical today, but
   any change to word accounting (tables, footnotes) must preserve it.

## Commit trail (this branch, newest last)

Step 4 verified → tjpgd Windows fix → step-5 design → 5.1 skeleton → 5.2 text → 5.3a image helper →
5.3b block images → 5.3c floats → 5.3d HR → 5.4 tables → 5.5 footnotes+TOC → 5.6 matrix → step-5
handover → step-6 design → **6a XPath LUT** (HEAD). `git log --oneline` on `feat-stage1-extraction`.
