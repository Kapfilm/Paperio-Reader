#include "BatteryCalibrationLog.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

namespace {
constexpr char ENABLE_PATH[] = "/battery-calibration.enable";
constexpr char LOG_PATH[] = "/battery-calibration.csv";
constexpr char HEADER[] = "uptime_ms,millivolts,raw_percent,estimate_percent,usb\n";
constexpr uint64_t MAX_LOG_BYTES = 8ULL * 1024 * 1024;
constexpr uint32_t SAMPLE_INTERVAL_MS = 15000;
}  // namespace

bool BatteryCalibrationLog::begin() {
  active = false;
  haveRecord = false;
  if (!Storage.ready() || !Storage.exists(ENABLE_PATH)) return false;

  active = true;
  char marker[64];
  const int length =
      std::snprintf(marker, sizeof(marker), "# boot uptime_ms=%lu\n", static_cast<unsigned long>(millis()));
  if (length < 0 || static_cast<size_t>(length) >= sizeof(marker)) {
    disable("cannot format boot marker");
    return false;
  }
  if (!append(marker, static_cast<size_t>(length))) return false;
  LOG_INF("BAT", "Calibration logging enabled: %s (15s samples, USB edges, 8 MiB limit)", LOG_PATH);
  return true;
}

void BatteryCalibrationLog::record(const uint32_t now, const uint16_t millivolts, const uint16_t rawPercent,
                                   const uint16_t estimatePercent, const bool usb) {
  if (!active) return;
  if (!Storage.ready()) {
    disable("SD card unavailable");
    return;
  }
  if (haveRecord && usb == previousUsb && now - lastRecordAt < SAMPLE_INTERVAL_MS) return;

  char line[96];
  const int length = std::snprintf(line, sizeof(line), "%lu,%u,%u,%u,%u\n", static_cast<unsigned long>(now),
                                   static_cast<unsigned>(millivolts), static_cast<unsigned>(rawPercent),
                                   static_cast<unsigned>(estimatePercent), usb ? 1U : 0U);
  if (length < 0 || static_cast<size_t>(length) >= sizeof(line)) {
    disable("cannot format sample");
    return;
  }
  if (!append(line, static_cast<size_t>(length))) return;
  haveRecord = true;
  previousUsb = usb;
  lastRecordAt = now;
}

bool BatteryCalibrationLog::append(const char* text, const size_t length) {
  if (!Storage.ready()) {
    disable("SD card unavailable");
    return false;
  }
  auto file = Storage.open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND);
  if (!file) {
    disable("cannot open CSV");
    return false;
  }

  const uint64_t size = file.fileSize64();
  const size_t headerLength = size == 0 ? std::strlen(HEADER) : 0;
  if (size > MAX_LOG_BYTES || headerLength + length > MAX_LOG_BYTES - size) {
    file.close();
    disable("CSV reached 8 MiB limit; existing log retained");
    return false;
  }
  bool written = true;
  if (headerLength != 0) written = file.write(HEADER, headerLength) == headerLength;
  if (written) written = file.write(text, length) == length;
  const bool closed = file.close();
  if (!written || !closed) {
    disable("CSV write or close failed");
    return false;
  }
  return true;
}

void BatteryCalibrationLog::disable(const char* reason) {
  if (active) LOG_ERR("BAT", "Calibration logging disabled: %s", reason);
  active = false;
}
