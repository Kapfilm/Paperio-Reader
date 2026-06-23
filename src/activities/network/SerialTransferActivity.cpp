#include "SerialTransferActivity.h"

#include <Arduino.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Below this free-heap level at entry we release the secondary framebuffer to
// give the transfer (16 KB coalesce buffer + SD/FAT structures) headroom.
// Threshold is a conservative guess pending on-device validation.
constexpr uint32_t kLowHeapTrimThreshold = 60 * 1024;

// Process at most this many protocol messages per loop() so a flood of small
// commands can't starve the Back-button check.
constexpr int kMaxMessagesPerLoop = 64;
}  // namespace

void SerialTransferActivity::statusThunk(void* ctx, const char* msg) {
  static_cast<SerialTransferActivity*>(ctx)->onStatus(msg);
}

void SerialTransferActivity::onStatus(const char* msg) {
  statusMessage_ = msg ? msg : "";
  // Paint synchronously. This only fires between transfers (before the first
  // chunk / after the last), so there is no concurrent SD or serial traffic and
  // thus no display/SD SPI contention.
  requestUpdateAndWait();
}

void SerialTransferActivity::onEnter() {
  Activity::onEnter();

  // Mute LOG_* on the wire for the whole session so no stray log byte from a
  // background task can corrupt the binary protocol stream. Logs still reach the
  // RTC ring buffer (getLastLogs). (On a boot-into-transfer, setup() already
  // muted; this keeps it muted.)
  setSerialWireMuted(true);

  // Arm the boot target: opening the port resets the C3 (unavoidable hardware
  // reset), so without this the device would land on Home and the host couldn't
  // transfer. Armed here (no reboot) so the reset routes setup() straight back
  // into this activity. Re-armed on every entry (setup() clears the flag).
  armSerialTransferReboot();

  device_.setStatusCallback(&SerialTransferActivity::statusThunk, this);
  statusMessage_ = "Ready - waiting for host";
  requestUpdateAndWait();  // paint the idle screen before any trim

  // Under memory pressure, free the secondary framebuffer (the primary stays so
  // we can still paint status updates). We silentRestart() on exit to rebuild it.
  if (ESP.getFreeHeap() < kLowHeapTrimThreshold) {
    if (auto* cache = renderer.getFontCacheManager()) cache->clearCache();
    if (renderer.hasSecondaryBuffer() && renderer.releaseSecondaryBuffer()) {
      renderer.setSingleBufferFastDiff(true);
      LOG_DBG("SXF", "Released secondary framebuffer under low heap (free=%u)",
              static_cast<unsigned>(ESP.getFreeHeap()));
    }
  }
}

void SerialTransferActivity::onExit() {
  device_.setStatusCallback(nullptr, nullptr);
  setSerialWireMuted(false);
  // Clear the arm first, in case silentRestart() below no-ops (e.g. a deep sleep
  // is already in progress), so we don't wake back into this activity.
  disarmSerialTransferReboot();
  Activity::onExit();

  // Always reboot to Home on exit. The boot-into-transfer path skips SD-font
  // discovery (and may have freed the secondary framebuffer), so a clean boot is
  // the simplest way to restore full state for Home/reader. Matches the web
  // server activity. silentRestart() targets Home and does not return.
  silentRestart();
}

void SerialTransferActivity::loop() {
  // Check Back first so the session can be left between protocol frames.
  // finish() (not onGoHome()) returns control to the File Transfer picker that
  // launched us via startActivityForResult, matching the Calibre branch.
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  // Drive the protocol. Each poll() handles at most one complete message and
  // returns false when the leading bytes aren't a magic or fewer than 4 bytes
  // are buffered. In the former case slide the probe forward a byte to resync.
  for (int i = 0; i < kMaxMessagesPerLoop; ++i) {
    if (device_.poll()) continue;
    if (device_.available() >= 4) {
      device_.dropByte();
      continue;
    }
    break;  // nothing actionable this iteration
  }
}

void SerialTransferActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);

  GUI.drawHeader(renderer, Rect{contentRect.x, metrics.topPadding, contentRect.width, metrics.headerHeight},
                 tr(STR_USB_TRANSFER));

  const int x = contentRect.x + metrics.contentSidePadding;
  const int maxWidth = contentRect.width - 2 * metrics.contentSidePadding;
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 2;

  // Word-wrap both texts so long file names / hints don't overflow the screen.
  auto drawWrapped = [&](const char* text, int maxLines, EpdFontFamily::Style style) {
    for (const auto& line : renderer.wrappedText(UI_10_FONT_ID, text, maxWidth, maxLines, style)) {
      renderer.drawText(UI_10_FONT_ID, x, y, line.c_str(), true, style);
      y += lineH;
    }
  };

  // Instruction chrome (translated).
  drawWrapped(tr(STR_USB_TRANSFER_HINT), 2, EpdFontFamily::REGULAR);
  y += metrics.verticalSpacing * 2;

  // Live status / diagnostic line (dynamic, includes file names; not translated).
  drawWrapped(statusMessage_.c_str(), 3, EpdFontFamily::BOLD);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
