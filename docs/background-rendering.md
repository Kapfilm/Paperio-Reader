# Background work in the EPUB reader (A / B / C)

How the reader hides latency behind three cooperative background mechanisms. This is the
**current-state reference**; the historical design/handoff lives in
[epubreader-control-flow-refactor.md](epubreader-control-flow-refactor.md) and
[background-b-handoff.md](background-b-handoff.md). All code is in
`src/activities/reader/EpubReaderActivity.cpp`.

## Task model

Two FreeRTOS tasks, serialised by one `RenderLock` (the `renderingMutex`):

- **Render task** (`ActivityManager::renderTaskLoop`) — the *only* task that draws. It is
  notification-driven: it takes the `RenderLock`, calls `Activity::render()`, releases the lock,
  and waits for the next `requestUpdate()`. `render()` dispatches to one pass per call via
  `classifyRenderPass()` (`FinishedBook`, `SectionBuilding`, `PreRender`, `BufferDisplay`,
  `BuildSection`, `Normal`).
- **Loop task** (the main Arduino `loopTask`) — input handling, plus idle-time background work
  via `serviceBackgroundWork()` when no input/page-turn is pending.

`renderContents()` releases the lock *before* the (blocking) e-ink waveform wait, so the loop
task gets the CPU during the ~0.5 s refresh — that window is when background work actually runs.

## The three mechanisms at a glance

| | What it hides | Runs on | Builds with buffer | When |
|---|---|---|---|---|
| **A** — next-page pre-render | per-page-turn render compute (~90 ms) | render task (`PreRender` pass) | resident | next page is text-only & heap ok |
| **B** — next-section pre-build | the "Indexing…" parse when you cross a chapter | loop task | **resident** (can't release — it's displaying) | idle, lookahead window |
| **C** — current-section build | the freeze when you *land* on an uncached section | loop task (build) + render task (draw only) | released or resident, device-dependent | on entry to an uncached section |

A and B are *look-ahead* for a section that's already on screen. C is for the section you just
navigated to and have nothing to read yet — so it has the highest priority.

## Priority — `serviceBackgroundWork()`

```
runDeferredGrayscalePass();                 // 1. AA of the page just shown (visible quality)
if (pendingGrayscale_.active) return;        //    AA still owed → nothing else runs
if (section && section->hasActiveBuild())    // 2. Background C: build the section you're waiting on
  { stepCurrentSectionBuild(); return; }
stepBackgroundSectionBuild();                // 3. Background A re-arm, then Background B
```

Rationale: deferred AA finishes the current page's quality; **C** unblocks reading (you can't
read until it produces pages); **A**/**B** are speculative look-ahead that only matter once the
current page/section is settled.

---

## A — next-page pre-render

Renders the *next* logical page into the inactive framebuffer so a forward turn is a near-instant
`BufferDisplay` instead of a fresh render.

- **Scheduled** in `renderContents()` (`pendingPreRender = true` + `requestUpdate()`) and **re-armed**
  once per `(spine, page)` by `stepBackgroundSectionBuild()` after the deferred-AA frees its memory.
- **Runs** as the `PreRender` pass (`renderPreRenderPass`) on the render task.
- **Gate:** free heap ≥ `PRE_RENDER_MIN_FREE_HEAP_BYTES` (56 KB); **text-only** pages only (image
  pages are excluded — their decode is too heap-hungry and deep).
- **Note on X3:** a page turn is *waveform-bound* (~0.5 s), so A only saves the ~90 ms of
  prewarm+BW compute. Its benefit is modest on X3; the panel, not the CPU, sets page-turn speed.

## B — next-section pre-build (look-ahead)

Builds the **next consecutive** sections' caches during idle so crossing a chapter boundary is a
cache hit, not a blocking parse. `stepBackgroundSectionBuild()`, one bounded slice per idle tick.

- **Lookahead:** up to `BG_BUILD_LOOKAHEAD` (3) spines ahead of the reading position; the cursor
  walks forward as each settles and re-anchors on any navigation.
- **State machine:** `Probe` (cache check) → `WaitHeap` (gates) → `Building` (`BG_BUILD_BUDGET_MS`
  = 40 ms slices) → `Settled`.
- **Builds resident.** B runs while a page is displayed, so it *cannot* release the secondary
  buffer (that would drop the current page's AA / fast-refresh baseline). This is the key
  constraint that shapes its gates.
- **Heap gates (resident):** free ≥ max(`BG_BUILD_PARSE_MIN_FREE_HEAP_BYTES` 48 KB,
  `BG_BUILD_EXTRACT_BASE_HEAP_BYTES` 30 KB + inflate-ring); contig ≥ max(`BG_BUILD_MIN_CONTIG_HEAP_BYTES`
  24 KB, ring + 8 KB).
- **CSS refuse gate:** a CSS section built resident reliably drops below the runtime CSS-resolve
  floor (~40 KB free) mid-parse → styles silently skipped → a *css-degraded* cache the foreground
  must rebuild. So B refuses CSS sections unless free ≥ `BG_BUILD_CSS_MIN_FREE_HEAP_BYTES` (72 KB);
  below that it parks in `WaitHeap` and lets **C** build the section released on arrival. It also
  **early-aborts** (`Section::activeBuildCssDegraded()`) the instant a slice starts skipping, rather
  than finishing a build it will discard.
- **Discards** truncated or css-degraded results (`clearCache()`); those rebuild clean in the
  foreground/C with the buffer released.
- **Adoption:** when you cross into a B section, `buildSection()` adopts `backgroundSection_` — a
  completed build is a cache hit; a still-partial build is finished by C.

## C — current-section build (build-while-you-read)

For the section you just entered that has **no cache** (first open, a jump / TOC / percent target,
a settings/orientation change that invalidated caches, or one B was heap-gated off). Closes the
gap B doesn't cover. The render task **only draws**; the build runs on the loop task.

- **Start:** `buildSection()` detects the cache miss, picks a mode (below), draws the "Indexing"
  popup, kicks off the build so `hasActiveBuild()` is true, and returns — handing off.
- **Build:** `stepCurrentSectionBuild()` on the loop task, 40 ms slices, highest reader-build
  priority. On a newly-written target page it `requestUpdate()`s so the render task draws it.
- **Draw:** the `SectionBuilding` pass (`renderSectionBuildingPass`) shows the requested page from
  the in-progress LUT (`Section::loadPageFromActiveBuild`, text-only) or the "Indexing" popup until
  it's built. Page reads flush the writer first so a second handle sees committed bytes.
- **Navigation during the build:** only for an explicit `Page` target — forward advances
  optimistically (shows the page once C reaches it, popup until then), back works, back past page 0
  leaves the chapter. Paragraph/anchor/percent/last-page targets resolve only at completion.
- **Finalize (`Done`):** the on-disk LUT is written, the nav target is resolved (a Page target's
  running position is kept; past-the-end crosses to the next spine), then `READING` resumes and the
  `Normal` pass renders the page with AA.
- **Failure (failed / truncated / css-degraded):** discard, latch `forceBlockingBuildSpine_`, and
  fall back to the **blocking** path (which builds with the buffer released for ~52 KB headroom).

### Build mode — `chooseSectionBuildMode()`

| Mode | When | Buffer |
|---|---|---|
| `IncrementalReleased` | **X3** (any), or **X4 + CSS** | released; restored after via `recoverSecondaryBufferIfNeeded()` |
| `IncrementalResident` | **X4 + non-CSS** that fits the in-place floors (`IN_PLACE_BUILD_MIN_FREE` 60 KB / `…_CONTIG` 28 KB) | kept resident |
| `Blocking` | `forceBlockingBuildSpine_` latch, no secondary buffer, or a CSS-fallback rebuild | released → rebuilt → realloc'd |

Device rationale (see [contributing/eink-controllers.md](contributing/eink-controllers.md)):

- **X3** keeps the differential baseline in the controller's **DTM1**, so releasing the RAM buffer
  costs no display benefit (fast refresh still works) and frees ~52 KB — and keeps CSS parses above
  the resolve floor. So X3 **always** builds released. Mid-build draws are plain BW off the DTM1
  baseline; AA returns once the buffer is restored.
- **X4** re-seeds its fast-refresh baseline from the RAM buffer, so non-CSS builds keep it resident.
  CSS builds release anyway (resident reliably css-degrades), at the cost of half-refresh mid-build
  draws until restored.

The released path sets `secondaryBufferDegraded_`; `recoverSecondaryBufferIfNeeded()` (top of every
`render()`, guarded to skip while a build is active) reallocates + (X4) reseeds the buffer on the
first render after the build ends. `onExit()` restores it too if the reader is left mid-build.

---

## Diagnostics

- **Per-page** (`DBG`): `Page summary: … refresh=<fast|half|full> mode=0x.. renderMs=… …`. The
  refresh mode/byte are captured at the page's own `triggerDisplay` (before the deferred-AA display
  call would overwrite the renderer's live last-mode), so they reflect the page, not the AA pass.
- **Background work** (`INF`, every ~5 s under `DEBUG_BACKGROUND_WORK`): `BG work: A runs/completes |
  B runs/completes state=<probe|waitheap|building|settled> spine=… css=… | preReady=… buildPct=…
  free=… contig=…`. A CSS book on a tight heap shows B parked in `waitheap` (the refuse gate) rather
  than building-and-discarding.
- **C lifecycle** (`INF`): `Background-C: building spine N … (buffer resident | secondary buffer
  RELEASED …)`, `Background-C spine=N complete: M pages`, and on failure
  `Background-C spine=N … falling back to blocking rebuild` / `… declined … blocking build`.
- **Render-task stack** (`ERR`, always on): `Render task stack LOW: N bytes free` if the high-water
  margin drops below 1536 B — the render task runs the deepest chains (build parse + image decode +
  dither) and its stack abuts the heap, so an overflow corrupts the heap.

## On X3 specifically (the common device)

- B effectively steps aside for CSS books at steady reading heap (~57–65 KB free < 72 KB), so
  forward chapter crosses into CSS sections are handled by **C released** (clean, responsive,
  ~1–1.5 s to page 0) rather than B cache hits.
- Page turns are fast (`refresh=fast`) except the scheduled anti-ghost **half** every
  `getRefreshFrequency()` (15) turns. On X3 a "fast" request is never silently turned into a half —
  it's honoured, or escalated to a *full* only when the differential baseline isn't synced (which
  the clean build/restore paths avoid).
