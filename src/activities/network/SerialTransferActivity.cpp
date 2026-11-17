#include "SerialTransferActivity.h"

#include <Arduino.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#if FREEINK_CAP_USB_MSC
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#endif

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

// Raise HWCDC's TX timeout for the session from its 100ms default. At 100ms, a
// brief ring-drain timing race at a chunk boundary flips the link to
// "disconnected" and silently drops TX, stalling a download. 1000ms gives the
// drain ISR ample slack while staying well under the 5s task watchdog (logs are
// muted during the session, so nothing else blocks on it).
constexpr uint32_t kSerialTxTimeoutMs = 1000;
}  // namespace

void SerialTransferActivity::statusThunk(void* ctx, const char* msg) {
  static_cast<SerialTransferActivity*>(ctx)->onStatus(msg);
}

void SerialTransferActivity::onStatus(const char* msg) {
  // Only record the message here — do NOT paint. This is called from inside the
  // protocol (mid-transfer); a blocking e-ink refresh in that path delays
  // protocol bytes and intermittently corrupted transfers. loop() paints the
  // latest message between protocol messages, when no SD/serial is in flight.
  statusMessage_ = msg ? msg : "";
  statusDirty_ = true;
}

void SerialTransferActivity::onEnter() {
  Activity::onEnter();
#if FREEINK_CAP_USB_MSC
  if (!resumeSerial_) {
    requestUpdate();
    return;
  }
#endif
  beginSerial();
}

void SerialTransferActivity::beginSerial() {
  mode_ = Mode::Protocol;
  // Mute LOG_* on the wire for the whole session so no stray log byte from a
  // background task can corrupt the binary protocol stream. Logs still reach the
  // RTC ring buffer (getLastLogs). (On a boot-into-transfer, setup() already
  // muted; this keeps it muted.)
  setSerialWireMuted(true);
  logSerial.setTxTimeoutMs(kSerialTxTimeoutMs);  // see constant; restored in onExit

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
  if (mode_ == Mode::Select) {
    Activity::onExit();
    return;
  }
#if FREEINK_CAP_USB_MSC
  if (mode_ == Mode::Drive) {
    Storage.endUsbDrive();
    ESP.restart();
    return;
  }
#endif
  device_.setStatusCallback(nullptr, nullptr);
  setSerialWireMuted(false);
  logSerial.setTxTimeoutMs(100);  // restore HWCDC default (exit reboot also resets it)
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
#if FREEINK_CAP_USB_MSC
  using Button = MappedInputManager::Button;
  if (mode_ == Mode::Select) {
    if (mappedInput.wasPressed(Button::Back)) {
      finish();
    } else if (mappedInput.wasPressed(Button::Confirm)) {
      if (driveSelected_) beginDrive();
      else beginSerial();
    } else if (mappedInput.wasPressed(Button::Up) || mappedInput.wasPressed(Button::Down) ||
               mappedInput.wasPressed(Button::Left) || mappedInput.wasPressed(Button::Right)) {
      {
        RenderLock lock(*this);
        driveSelected_ = !driveSelected_;
      }
      requestUpdate();
    }
    return;
  }
  if (mode_ == Mode::Drive) {
    loopDrive();
    return;
  }
#endif
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

  // Paint the latest status now that the protocol is idle this iteration — no SD
  // or serial in flight, so the blocking e-ink refresh is SPI-safe and can't
  // delay a transfer.
  if (statusDirty_) {
    statusDirty_ = false;
    requestUpdateAndWait();
  }
}

#if FREEINK_CAP_USB_MSC
void SerialTransferActivity::beginDrive() {
  if (esp_efuse_read_field_bit(ESP_EFUSE_DIS_USB_OTG)) {
    {
      RenderLock lock(*this);
      statusMessage_ = tr(STR_USB_DRIVE_UNAVAILABLE);
    }
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    mode_ = Mode::Drive;
    statusMessage_ = tr(STR_USB_DRIVE_WAITING);
  }
  powerManager.setPowerSaving(false);
  requestUpdateAndWait();
  setSerialWireMuted(true);
  logSerial.end();
  const bool started = Storage.beginUsbDrive();
  driveStartedAt_ = millis();
  if (!started) {
    {
      RenderLock lock(*this);
      driveFailed_ = true;
      statusMessage_ = tr(STR_USB_DRIVE_UNAVAILABLE);
    }
    requestUpdate();
  }
}

void SerialTransferActivity::loopDrive() {
  using State = UsbDriveState;
  const auto state = Storage.usbDriveState();
  const auto now = millis();
  const bool connected = state == State::Connected;
  if (connected && !driveSeen_) {
    {
      RenderLock lock(*this);
      driveSeen_ = true;
      statusMessage_ = tr(STR_USB_DRIVE_READY);
    }
    requestUpdate();
  }
  if (state == State::IoError && !driveFailed_) {
    {
      RenderLock lock(*this);
      driveFailed_ = true;
      statusMessage_ = tr(STR_USB_DRIVE_ERROR);
    }
    requestUpdateAndWait();
    Storage.endUsbDrive();
    ESP.restart();
    return;
  }

  if (connected && Storage.usbDriveSuspended()) {
    if (suspendedAt_ == 0) suspendedAt_ = now;
  } else {
    suspendedAt_ = 0;
  }
  const bool cancel = !driveSeen_ && mappedInput.wasPressed(MappedInputManager::Button::Back);
  const bool noHost = !driveSeen_ && now - driveStartedAt_ >= 300000;
  const bool suspended = suspendedAt_ != 0 && now - suspendedAt_ >= 5000;
  if (cancel || noHost || suspended || state == State::Disconnected || state == State::Ejected) {
    // The host must no longer own the card before the next boot mounts FAT.
    Storage.endUsbDrive();
    ESP.restart();
  }
}
#endif

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

#if FREEINK_CAP_USB_MSC
  if (mode_ == Mode::Select) {
    GUI.drawList(renderer, Rect{x, y, maxWidth, metrics.listRowHeight * 2}, 2, driveSelected_ ? 0 : 1,
                 [](int index) { return index == 0 ? tr(STR_USB_DRIVE_MODE) : tr(STR_USB_SERIAL_MODE); });
    y += metrics.listRowHeight * 2 + metrics.verticalSpacing;
    if (!statusMessage_.empty()) drawWrapped(statusMessage_.c_str(), 3, EpdFontFamily::REGULAR);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (mode_ == Mode::Drive) {
    drawWrapped(statusMessage_.c_str(), 5, EpdFontFamily::BOLD);
    if (!driveFailed_) drawWrapped(tr(STR_USB_DRIVE_EJECT_HINT), 5, EpdFontFamily::REGULAR);
    if (!driveSeen_) {
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }
  } else
#endif
  {
    drawWrapped(tr(STR_USB_TRANSFER_HINT), 5, EpdFontFamily::REGULAR);
    y += metrics.verticalSpacing * 2;
    drawWrapped(statusMessage_.c_str(), 3, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
