# Stage-1 pipeline — status, gap analysis, and the content.bin persistence plan (2026-07-25)

Branch: `feat-stage1-extraction`. This document reconciles what the Stage-1 work has actually
delivered against the objectives in `docs/compiled-book-pipeline-plan.md` (Phase 3), records the
load-bearing gap, and specifies the next action (persist + read `content.bin` on the device path).

## UPDATE 2026-07-26 — payoff now PROVEN end-to-end; only device wiring remains

Since this doc was written, the format+proof gaps below were closed:
- **WBC1 completed** to a full Stage-1 artifact: footnotes + xpath LUT + page-break labels (v3,
  `02e1f430`), then the source ZIP fingerprint for stale-cache rejection (v4, `ea9f7979`).
- **Read-back replay proven byte-identical**: `ContentBinReplayMatrix` — content.bin → LayoutSink
  reproduces a direct parse+layout across 94 cases (full corpus × 7 profiles + Moby, incl.
  footnotes). `5f4f4661`.
- **≥3× speed gate MET and measured**: ~**8–9×** on Moby (full parse+layout ~1.6s → content.bin
  replay ~0.18s). `9010b087`, baseline `docs/pipeline-baseline-2026-07-26.md`.
- **Device-wiring designed**: `docs/parser-stage1-content-bin-device-wiring-design-2026-07-26.md`
  (two-pass, ZIP-fingerprint key, flag-gated 4-increment rollout). Increment 1 (v4 fingerprint) DONE.

**Remaining**: increments 2–4 of the device wiring — touch `Section::createSectionFile` to write
`content.bin` and add the read-back fast path. Shipping-path change; needs device validation (cold/
warm open ms, background-build slicing, SD cost) that the host-only environment cannot provide.
Full host suite 539/539 green.

## Where we are — honest status (as of 2026-07-25, superseded above for the format/proof items)

The two-stage split now **exists in code and is proven correct**, but the migration's user-facing
payoff is **not yet delivered**.

### Delivered (committed, gated byte-identical, 442/442 host tests green)

- **Stage-1 / Stage-2 separation in code.** `ChapterHtmlSlimParser` walks the XHTML and emits a
  `compiled::BlockSink` transcript (`onBlock`/`onAnchor`/`onChapter`/`onFootnote`/…). All layout
  (measure, line-break, paginate, floats, tables, HR, footnote-preview abbreviation) lives in
  `compiled::LayoutSink` (Stage-2). The parser's own "fused" layout engine was **deleted** (−470
  lines, commit `1514dd1e`).
- **`content.bin` (WBC1) format.** Defined, versioned (**v2**), round-trips through
  `writeContentBin`/`readContentBin`, and is **provably settings-independent**: the footnote-preview
  abbreviation leak was fixed (`d561d552`) — full text is stored and Stage-2 abbreviates to the
  viewport at layout time. `ContentSink.InlineFootnotePreviewTextIsSettingsIndependent` compiles a
  book wide-vs-narrow and asserts identical block text.
- **Functionality gate.** Golden layout dumps are byte-identical across the corpus × 7-profile
  settings matrix + Moby Dick; determinism matrix green; full ctest suite 442/442.

### NOT delivered — the load-bearing gap

1. **`content.bin` is never persisted or read on the device.** `writeContentBin` / `readContentBin`
   are referenced only in `lib/Epub/Epub/content/**` and `test/**`. `Section.cpp` and
   `EpubReaderActivity.cpp` never call them. On-device, the parser drives an **in-memory**
   `LayoutSink`; there is **no settings-independent artifact on disk**. This is **Phase 3 step 3**
   ("Reader API consumed by Section build phase (b)") and it is unwired.

2. **The ≥3× re-layout speed gate — the entire point of the migration — is unmeasured and cannot
   yet be met.** The promise is "a font/margin change re-runs only Stage-2 (no ZIP/XML/CSS)."
   Because `content.bin` is not persisted, a settings change today still re-runs the FULL parse
   (ZIP → inflate → Expat → CSS → walk) to rebuild the in-memory transcript. The 3× win requires
   reading a persisted `content.bin` and skipping Stage-1 — which is exactly what is missing.

3. **No device smoke test / device timings** this cycle (host-only environment). Plan names a
   device smoke script (anchor nav, footnotes) for the Functionality gate and device re-pagination
   ms for the Speed gate.

4. **Phase 4** (single `layoutGenerationHash`, partial-cache resume, scheduler collapse) — not
   started.

### Correction to the plan's status line

`compiled-book-pipeline-plan.md` header says "Phase 3 … step 2c (rendererless Stage-1 pass) in
progress." Reality: the Stage-1 producer + Stage-2 consumer are complete and the fused engine is
deleted, but **step 3 (device persistence + read-back) has not started**, so Phase 3 is NOT close
to done. Update the status line accordingly.

## The two device build paths today (for the wiring work)

`Section::createSectionFile` runs three phases (`runBuildSetup` → `runBuildParse` →
`runBuildFinalize`). The parser is created in `runBuildSetup` (Section.cpp ~698) with
`st.visitor->setStage1Sink(stage1Sink_)` (~706).

- **Device / section-cache path** (today): `stage1Sink_ == nullptr` → the parser constructs its
  INTERNAL `LayoutSink` in `setup()` and pages flow through `completePageFn` into the section cache
  (`html_<spine>.bin` + section file). This is a FULL parse every time; settings changes re-parse.
- **ContentSink compile** (today, TEST-ONLY): an external `compiled::ContentSink` is attached via
  `setStage1Sink`; the build is "content-only, needs no pages" and fills a `CompiledContent` in
  memory. Only `test/epub_pipeline` uses this; nothing writes it to disk on device.

## Next action (#1): persist + read `content.bin` on the device path

Goal: turn the two-stage split into an actual cache. First compile writes `content.bin`; a
settings-change reopen reads `content.bin` and drives `LayoutSink` directly, skipping
ZIP/XML/CSS/walk.

### Design sketch (to be refined as we implement)

1. **Write `content.bin` during the normal device build.** After a full Stage-1 walk, serialize the
   book's `CompiledContent` to a per-book `content.bin` (settings-independent → written once per
   book, NOT per settings variant). Path/key: alongside the existing per-book cache, keyed by the
   ZIP central-directory fingerprint (Phase 1), NOT by the layout property hash.
   - Open question: today the device build drives the internal `LayoutSink` (pages), not a
     `ContentSink` (records). To also capture `content.bin` we need EITHER (a) a fan-out so one walk
     feeds both a `ContentSink` and the `LayoutSink`, OR (b) a two-pass: Stage-1 walk → `ContentSink`
     → `content.bin`, then Stage-2 reads it back → `LayoutSink`. The parser currently enforces ONE
     sink at a time (`effectiveSink()`), so (b) is the cleaner first cut and matches the plan's
     "Stage-1 then Stage-2" framing. (a) is a later optimization to avoid the double walk on first
     open.

2. **Add a Stage-2 driver that reads `content.bin` → `LayoutSink`.** A new path in `Section` build
   phase (b): if a valid `content.bin` exists (fingerprint match), skip the parser entirely and
   replay `CompiledContent` blocks through a `LayoutSink` (the same `onBlock`/`onAnchor`/… calls the
   parser makes), producing the section pages. This is the settings-change fast path.

3. **Invalidation keys.** `content.bin` is invalidated ONLY by the content fingerprint (Phase 1),
   never by settings. The Stage-2 page cache keeps the existing layout property hash. A settings
   change drops the page cache but KEEPS `content.bin` → rebuild reads records, no re-parse.

4. **Gate.** Host: add a harness path that (i) compiles `content.bin`, (ii) drives `LayoutSink` from
   the read-back records, and asserts the page dump is byte-identical to the direct-parse dump
   (extends the existing determinism/golden gate). Measure first-compile vs read-back-relayout µs
   and publish to `pipeline-baseline-2026-07-25.md` — this is where the ≥3× number finally gets a
   real value. Device: smoke script (anchor nav + footnotes) once host is green.

### Risks / watch-items

- The `LayoutSink`-from-records driver must make the SAME `onBlock`/`onAnchor`/`onChapter`/
  `onFootnote`/`onPageBreakLabel`/`onXPathAdvance`/`onSpineEnd` sequence the parser makes, in the
  same order, or the page dump diverges. The transcript order is the contract.
- `content.bin` currently has no on-disk framing beyond `writeContentBin` (whole-book blob). The
  device wants per-spine seekability for background/incremental builds — may need a spine index in
  the header. Start whole-book, refine if the incremental scheduler needs it.
- Storage budget: plan caps total artifacts at ≤ 1.5× source EPUB. `content.bin` replaces the role
  of `html_<spine>.bin` eventually (Phase 3 step 6); until then both exist — measure.

## Cross-refs

- `docs/compiled-book-pipeline-plan.md` — Phase 3 (this work), Phase 4 (next).
- `docs/parser-stage1-6e-handover-2026-07-25.md` — the fused-layout deletion + 6d/footnote-preview.
- Memory: `stage1-layoutsink-progress`, `stage1-currenttextblock-load-bearing`,
  `stage1-footnote-preview-settings-split`, `stage1-settings-split-vs-microreader`.
