# Secondary Frame Buffer Management

## Background

The X3 (UC8179) and X4 (SSD1677) controllers both use a **differential fast-refresh**
scheme: the controller compares a new-frame bitmap against a previous-frame baseline
and only drives pixels that changed. On X3 both bitmaps live in controller RAM (DTM1 /
DTM2); on X4 the previous-frame baseline is held in RED RAM which the host must keep
current.

On X4, `syncRedRamFromFrameBuffer()` writes the host-side previous-frame copy into
RED RAM after every `displayBuffer()` call, keeping the baseline current. On X3 this
call is a no-op — the controller manages its own baseline.

`EInkDisplay` allocates two heap buffers at `begin()` time:

| Name | Role |
|---|---|
| `frameBuffer` | Active write target — glyph rendering, `clearScreen()`, etc. write here |
| `frameBufferActive` | Previous-frame copy — written to RED RAM before each fast refresh |

`swapBuffers()` (called inside `triggerDisplay()`) exchanges the two pointers so that
after a refresh `frameBufferActive` holds the frame just sent to the panel, and
`frameBuffer` points to the now-free slot ready for the next render.

On X4, each buffer is ~48 KB. Together they are the largest single heap consumer in
the system.

---

## Scenario 1 — Temporary release for CPU/heap-intensive work

### When to use

When a render-heavy or heap-hungry operation must run while the reader is still active
and will display more pages afterwards. Examples:

- Section/chapter indexing (CSS parser + image decoder)
- Per-image-page JPEG/PNG warm pass
- OOM recovery after a failed indexing attempt

### Procedure

```
1. Ensure no waveform is in flight (do not call between triggerDisplay and completeDisplay).

2. Call syncRedRamFromFrameBuffer() BEFORE releasing the secondary buffer.
   This writes the current last-displayed frame into RED RAM while frameBufferActive
   is still valid. RED RAM will then retain this baseline independently of the host
   for as long as the controller remains powered.

3. Call releaseSecondaryBuffer().
   frameBufferActive becomes nullptr. frameBuffer (the write target) is untouched.
   ~48 KB is returned to the heap.

4. Perform the intensive work. BW display still works during this window:
   - HALF_REFRESH and FULL_REFRESH work normally (no differential baseline needed).
   - FAST_REFRESH requires opt-in: call setSingleBufferFastDiff(true) before any
     display call. This leaves RED RAM unchanged and diffs against the controller's
     retained copy. Only safe if:
       a) syncRedRamFromFrameBuffer() was called in step 2, AND
       b) No waveform type other than FAST has fired since step 2
          (HALF/FULL overwrites RED RAM with the new frame, invalidating the baseline).

5. When work is complete, call reallocSecondaryBuffer().
   The new buffer is initialised to white (0xFF). frameBufferActive is non-null again.

6. [X4 only] Call syncRedRamFromFrameBuffer() immediately after reallocSecondaryBuffer(),
   before any clearScreen() for the next render. After reallocSecondaryBuffer() the new
   frameBufferActive is white, so this call writes white into RED RAM. On the next
   triggerDisplay(FAST_REFRESH) the controller diffs the new frame against white,
   which drives all text pixels correctly. The first page after the realloc effectively
   refreshes from a white baseline — acceptable for text, and avoids a slow HALF_REFRESH.
   See "Implementation note on RED RAM reseed" below for the tradeoffs.
   On X3 this call is a no-op and can be omitted, but calling it is harmless.

7. If setSingleBufferFastDiff(true) was set in step 4, call setSingleBufferFastDiff(false)
   after reallocSecondaryBuffer() to restore normal double-buffer fast-diff behaviour.
   (X4 only — X3 ignores this flag.)

8. Resume normal rendering. The next triggerDisplay(FAST_REFRESH) will:
   - Write frameBuffer (new frame) to BW RAM
   - Write frameBufferActive (previous-frame copy) to RED RAM
   - Fire the fast differential waveform
```

### Implementation note on RED RAM reseed after realloc

After `reallocSecondaryBuffer()` the new `frameBufferActive` is white (0xFF). Calling
`syncRedRamFromFrameBuffer()` at this point writes white into RED RAM. The subsequent
`triggerDisplay(FAST_REFRESH)` diffs the new frame against white, which correctly
re-drives all text pixels. The first page after the realloc refreshes from a white
baseline — harmless for text pages, and far cheaper (~12 ms SPI vs ~1600 ms HALF
waveform).

The strictly correct alternative is to call `syncRedRamFromFrameBuffer()` **before**
`releaseSecondaryBuffer()`, while `frameBufferActive` still points to the previous
frame. RED RAM would then hold the true last-displayed content throughout the release
window. This is slightly better if a HALF_REFRESH fires during the window (because
HALF does not overwrite RED RAM until the waveform starts), but for the typical
text-page case the white-baseline approach is indistinguishable.

---

## Scenario 2 — Full release when no further display output is needed

### When to use

When the device is about to reboot or enter deep sleep and the display buffers are no
longer needed. Examples:

- Wi-Fi / web server session (large heap required, device reboots on exit)
- Deep sleep entry (display put into hardware sleep, no further host-side rendering)

### Procedure

```
1. Finish all pending display work. Ensure completeDisplay() has been called if
   triggerDisplay() was used.

2. Call display.deepSleep() to put the controller into its low-power state.
   This powers down the panel and sends CMD_DEEP_SLEEP to the SSD1677.
   The controller retains its last-displayed frame in non-volatile RED RAM.

3. Call display.releaseBuffers().
   Both frameBuffer0 and frameBuffer1 are freed. frameBuffer and frameBufferActive
   become nullptr. ~96 KB is returned to the heap.

4. After this point NO display methods may be called. Any attempt to render or
   refresh will crash (null pointer write into frameBuffer).

5. On exit from this mode, the device MUST reboot. begin() re-allocates both
   buffers, re-initialises the controller, and restores a known baseline.
```

### Deep sleep + QuickResume

If the intent is to resume with the screen content preserved (QuickResume):

```
1. Render the sleep screen via displayBuffer() normally.
2. Optionally save frameBufferActive to a persistent sleep-frame file on SD for
   restoration after wakeup.
3. Call display.deepSleep().
4. Call display.releaseBuffers() if additional heap is needed before sleep.
5. On wakeup: re-init the display (begin()), load the saved sleep frame, call
   displayBuffer(HALF_REFRESH) to restore the known-good baseline, then resume
   normal operation.
```

---

## Code patterns

### Pattern 1a — Temporary release, fast differential during window (X4)

```cpp
// Seed RED RAM while frameBufferActive is still valid.
if (!renderer.isX3()) renderer.syncRedRamFromFrameBuffer();

// Free ~48 KB; allow fast differential from the controller's retained RED RAM.
renderer.releaseSecondaryBuffer();
renderer.setSingleBufferFastDiff(true);

// ... heap-intensive work, BW page turns possible ...

// Restore secondary buffer. New frameBufferActive is white.
renderer.reallocSecondaryBuffer();
renderer.setSingleBufferFastDiff(false);

// X4: write white baseline into RED RAM. First post-realloc fast diff will be
// against white, which correctly re-drives all text pixels.
if (!renderer.isX3()) renderer.syncRedRamFromFrameBuffer();
```

### Pattern 1b — Temporary release, HALF_REFRESH only during window

```cpp
// No syncRedRamFromFrameBuffer needed before release when fast diff is not required.
renderer.releaseSecondaryBuffer();

// ... work — display with HALF_REFRESH only ...

renderer.reallocSecondaryBuffer();
if (!renderer.isX3()) renderer.syncRedRamFromFrameBuffer();
```

### Pattern 2 — Full release before reboot

```cpp
// Finish all pending display work before this point.
renderer.deepSleep();      // power down controller, retains panel image
renderer.releaseBuffers(); // frees both buffers (~96 KB); no display after this

// ... network I/O, file operations, etc. ...

esp_restart();             // reboot; begin() re-allocates buffers on next boot
```

---

## X3 vs X4 differences

| Behaviour | X3 (UC8179) | X4 (SSD1677) |
|---|---|---|
| Previous-frame baseline | Lives in controller DTM1 RAM | Held in controller RED RAM, must be re-seeded by host via `syncRedRamFromFrameBuffer()` after every refresh |
| Effect of releasing secondary buffer on fast differential | None — DTM1 is independent | Fast differential degrades to HALF unless `setSingleBufferFastDiff(true)` |
| `syncRedRamFromFrameBuffer()` | No-op (returns immediately) | Writes 48 KB to controller RED RAM over SPI (~12 ms) |
| Grayscale AA without secondary buffer | Not applicable (AA uses different path) | Unavailable — `getEffectiveTextAntiAliasing()` gates on `hasSecondaryBuffer()` |
| `releaseSecondaryBuffer()` size freed | ~52 KB | ~48 KB |

---

## Refresh-mode downgrade contract (while secondary is released)

Releasing the secondary buffer changes what a `FAST_REFRESH` request means. The
downgrade is applied automatically inside `triggerDisplay()` — callers do not need to
sanitise the mode themselves — but the resulting behaviour must be understood:

| State | `FAST_REFRESH` request resolves to |
|---|---|
| Secondary present (normal) | FAST — host reseeds RED RAM from `frameBufferActive` |
| Secondary released, `setSingleBufferFastDiff(false)` (X4) | **Downgraded to HALF** — no host previous-frame copy to diff against |
| Secondary released, `setSingleBufferFastDiff(true)` (X4) | FAST — diffs against the controller's retained RED RAM (only valid if RED RAM was seeded before release and no HALF/FULL fired since; see Scenario 1) |
| Secondary released (X3) | FAST — DTM1 baseline lives in the controller, unaffected by the host release |

The invariant driving the downgrade is derived from the owning objects at refresh time
(`hasSecondaryBuffer()` and `isRedRamSynced()`), **not** from a mirrored HAL-level mode
flag. Do not introduce a shadow copy of this state: after a failed
`reallocSecondaryBuffer()` the two sources would diverge, and the RED-RAM reseed
subtleties (see "Implementation note on RED RAM reseed") make a mirrored flag likely to
be wrong. Query the display, don't cache its state.

---

## API reference

All methods are on `EInkDisplay` (low level) and forwarded through `HalDisplay` and
`GfxRenderer`.

| Method | Layer | Description |
|---|---|---|
| `releaseSecondaryBuffer()` | EInkDisplay / HalDisplay / GfxRenderer | Frees `frameBufferActive`. Returns false if already null. |
| `reallocSecondaryBuffer()` | EInkDisplay / HalDisplay / GfxRenderer | Allocates a new secondary buffer, initialised to 0xFF. Returns false on OOM. |
| `hasSecondaryBuffer()` | EInkDisplay / HalDisplay / GfxRenderer | True when `frameBufferActive != nullptr`. |
| `syncRedRamFromFrameBuffer()` | EInkDisplay / HalDisplay / GfxRenderer | Writes last-displayed frame to RED RAM. X4 only; no-op on X3. Call after realloc to reseed baseline. |
| `setSingleBufferFastDiff(bool)` | EInkDisplay / HalDisplay / GfxRenderer | X4 only. Opt in to fast differential without secondary buffer. Only safe when RED RAM is known-current. No-op on X3 (DTM1 baseline is always in controller RAM). |
| `releaseBuffers()` | EInkDisplay / HalDisplay | Frees BOTH buffers. No display operations after this. Reboot required to restore. |
| `deepSleep()` | EInkDisplay / HalDisplay | Powers down the SSD1677 controller. Call before `releaseBuffers()` for clean shutdown. |

---

## Common mistakes

**Freeing `frameBuffer` instead of `frameBufferActive`**
`swapBuffers()` alternates which of `frameBuffer0`/`frameBuffer1` is the active write
target. After a swap, `frameBuffer` may point to either slot. Always use
`releaseSecondaryBuffer()` — never free `frameBuffer0` or `frameBuffer1` directly,
as the assignment may have changed since the last swap.

**Calling `triggerDisplay(FAST_REFRESH)` without secondary buffer and without `setSingleBufferFastDiff`**
Without the opt-in flag, `triggerDisplay` automatically downgrades to `HALF_REFRESH`
(~1600 ms). Enable `setSingleBufferFastDiff(true)` before the release if fast updates
are needed during the secondary-buffer-free window.

**Leaving `setSingleBufferFastDiff` enabled after realloc**
After `reallocSecondaryBuffer()` the normal double-buffer path is correct and
`setSingleBufferFastDiff` is redundant. Call `setSingleBufferFastDiff(false)` after
realloc to avoid confusion and to ensure the double-buffer RED RAM reseed path runs.

**Calling any display method after `releaseBuffers()`**
`releaseBuffers()` is a terminal operation. `frameBuffer` becomes nullptr; any
subsequent `clearScreen()` or render call will write through a null pointer and crash.
Only use it immediately before reboot or deep sleep.

**Not calling `deepSleep()` before `releaseBuffers()`**
The controller should be put to sleep before the host releases its buffers.
`deepSleep()` sends a final power-down command over SPI. After `releaseBuffers()` there
is no framebuffer to construct a valid SPI payload from, so the order matters.
