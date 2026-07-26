# Increment D — wire content.bin into the Section device build (design, 2026-07-26)

The content.bin engine (streaming writer + reader + settings-independent, byte-identical replay) is
built and host-gated (552/552; plan `stage1-revised-plan-v2-2026-07-26.md`). Increment D is the
shipping-path integration into `Section` so the reader (a) leaves a content.bin behind and (b)
rebuilds a spine's section cache FROM content.bin on a settings change, skipping ZIP/XML/CSS — the
~8–9× relayout win, on device. This note pins the exact seams, the read-back build, the write
trigger, cache keys, slicing, failure handling, and the host gate. All behind `EPUB_STAGE1`.

## Ground truth — what a Section build produces today

`Section::createSectionFile` (sliced: `runBuildSetup` → `runBuildParse` → `runBuildFinalize`, each
yielding under `budgetMs`) writes ONE section-cache file `sections/<spine>_<propertyHash>.bin`:
- header (`writeSectionFileHeader`, settings snapshot) — `Section.cpp:630`.
- page bodies: `onPageComplete` serializes each `Page` to `file` as it completes and pushes its
  offset into `st.lut` — `Section.cpp:248-265, 701`. Pages are NOT retained (streaming).
- then in `runBuildFinalize`: the page-offset LUT (`Section.cpp:1017-1028`), the anchor→page map
  (`visitor.getAnchors()`, `:1038-1044`), page-break labels (`visitor.getPageBreakLabels()`,
  `:1048/1111`), the paragraph LUT (`visitor.getParagraphLutPerPage()`, `:1059`).

`propertyHash` = ALL settings (`calculatePropertyHash`, `Section.cpp:134`). Variant eviction (max 5,
LRU). The section cache is fully settings-dependent — KEEP AS-IS.

Key: the parser's INTERNAL `LayoutSink` produces the pages (via `completePageFn` → `onPageComplete`)
and the getters `getAnchors/getPageBreakLabels/getParagraphLutPerPage` proxy to it. So a read-back
build that drives a LayoutSink directly and pulls the same getters produces the SAME section file.

## The read-back fast path (the core of D)

Add `Section::buildSectionFromContentBin(BuildState& st)` — an alternative to the parse that:
1. Opens `content.bin` with a `BlockStreamReader`; verifies `fingerprint() == epub->zipContentFingerprint`
   and that spine `spineIndex` is present. On any miss → return a "not available" result so the
   caller falls back to the normal parse (which will ALSO trigger the lazy write, below).
2. Constructs a `compiled::LayoutSink` with `LayoutParams` from `st.params` (font, viewport,
   spacing, alignment, hyphenation, bionic, embeddedStyle, fontSizeLadder, imageBasePath =
   `getImageBasePath(propertyHash)`, epubFilePath), and `completePageFn = [this,&st](page){
   st.lut.emplace_back(onPageComplete(move(page))); }` — the SAME lambda the parser uses
   (`Section.cpp:701`), so page streaming + LUT are identical.
3. Replays spine `spineIndex`'s blocks: `openSpine` → `nextLogicalBlock` loop, firing
   onAnchor/onPageBreakLabel/onFootnote/onXPathAdvance before onBlock and onChapter after — exactly
   `PipelineRunner::replayFromContentBin`'s per-spine loop (already proven byte-identical). Factor
   that loop into a shared helper so device and host use ONE implementation.
4. `sink.onSpineEnd()`, then write the section-file tail from `sink.anchors()` /
   `sink.pageBreakLabels()` / `sink.paragraphLutPerPage()` — the same finalize code, reading the
   LayoutSink getters instead of the parser's. (Simplest: give `runBuildFinalize` a mode that reads
   from a `compiled::LayoutSink&` when the build was a read-back, else from the parser `visitor`.
   Both expose the same three getter shapes.)

Slicing: the replay is FAST and per-block. First cut — replay a whole spine in one slice (Small Gods'
570 KB spine is the stress: measure; if it blows `budgetMs`, yield every N logical blocks, saving the
BlockStreamReader cursor + LayoutSink state in `BuildState`, mirroring the parse's mid-spine yield).
The reader holds ≤ one logical block; the LayoutSink holds one page + one text block — same bound as
the parse path.

## The write trigger (Option 2 — lazy, never regress first open)

- First open of a spine with no content.bin coverage: the NORMAL parse runs (unchanged). Page 1 is
  served mid-build exactly as today (`loadPageFromActiveBuild`).
- After the reader is served, a background pass (scheduled like Background-B, 1 KB-sliced) walks the
  spine ONCE more with a `ContentBinWriter` attached via `setStage1Sink` (content-only compile —
  `effectiveSink()` returns the writer, no LayoutSink pages). This appends the spine's records to
  `content.bin`. Per-spine-on-first-visit: only spines the reader touches get compiled; a
  1,732-spine book costs nothing up front.
- content.bin is opened once per book for append; the spine-offset index grows. Resumable across
  sessions is a later refinement — first cut may recompile the whole book's visited-so-far spines if
  interrupted (bounded, rare). Simpler first cut for the DEVICE TEST: a foreground "compile this
  book's spines to content.bin" triggered explicitly, so the read-back path has something to read —
  then measure relayout. Promote to background-lazy once the read-back speedup is confirmed.

## Cache keys & invalidation

- `content.bin` path: `<cachePath>/content.bin`. Keyed on the ZIP fingerprint (in the v5 header).
  Invalidated ONLY by fingerprint mismatch — NEVER by settings. A settings change drops the
  section-cache variant (existing propertyHash logic) but KEEPS content.bin → the rebuild takes the
  read-back fast path.
- Section cache: unchanged (`propertyHash`).

## Failure / corruption handling (already in the reader)

`BlockStreamReader::open` rejects a bad magic/version, an unfinished (offsets 0) file, or an offset
past EOF → the read-back path reports "unavailable" and the caller parses normally (and re-writes
content.bin). A partial content.bin from an interrupted write is thus self-healing.

## Host gate (before any device flash)

New `SectionEquivalence` host test (in `test/epub_pipeline`): for each corpus book × a couple
profiles, build a spine's section-cache file BOTH ways — (a) normal parse, (b) read-back from a
content.bin compiled for that book — and assert the two section files are BYTE-IDENTICAL. This
proves the device read-back path produces exactly today's cache. Reuse the harness `Section` driver.
Also assert flag-off is bit-for-bit master (the parse path is untouched).

## Device measurement (after the host gate is green)

Flash `[env:default]` `-DEPUB_STAGE1=1`. Books: Small Gods (570 KB spine), King's Avatar (1,732
spines). Capture vs a master baseline (flag off): cold-open first-page ms (Option 2 ≈ master),
settings-change relayout ms (target ≥3×), per-spine `arena highWater` + `Min Free` (no `failedAlloc`;
≥ master), content.bin SD size (total ≤ 1.5× source), largest-contiguous across a King's Avatar
page-through (no frag drift). See the plan's measurement table.

## Increment-D sub-steps (each small, host-gated)

1. Factor the per-spine replay loop (`replayFromContentBin`'s body) into a shared
   `compiled::replaySpine(BlockStreamReader&, spineIndex, LayoutSink&, chapters)` used by host + device.
2. `Section::buildSectionFromContentBin` + a finalize mode that reads the tail from a `LayoutSink&`.
   Gate: `SectionEquivalence` byte-identical (read-back vs parse), flag on.
3. Write trigger: a `Section::compileSpineToContentBin(spineIndex)` (parse once with a
   ContentBinWriter via `setStage1Sink`) — foreground for the first device test. Gate: the written
   content.bin replays byte-identical (existing `ContentBinReplayMatrix`, now fed by the device
   writer path via a host harness variant).
4. Wire into `stepSectionBuild`/`EpubReaderActivity`: on a build request, if content.bin covers the
   spine + fingerprint matches → read-back; else parse (+ schedule the lazy compile). Behind
   `EPUB_STAGE1`. Gate: flag-off unchanged; flag-on section cache byte-identical.
5. Background-lazy write + resumable content.bin (refinement after the device test confirms value).

## Status

Design only. Next code step: sub-step 1 (factor `replaySpine`), then sub-step 2 + the
`SectionEquivalence` host gate — all host-gated before the device flash.
