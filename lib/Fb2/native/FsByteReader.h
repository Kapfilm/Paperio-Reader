// HalStorage adapter with no ownership and no allocation.
// Adapted from inkMOD v1.1.8.
#pragma once

#include <HalStorage.h>

#if defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#include "IByteReader.h"

class FsByteReader final : public IByteReader {
 public:
  explicit FsByteReader(HalFile& file) : file_(file) {}

  size_t read(void* dst, size_t len) override {
    const int got = file_.read(dst, len);
    if (got <= 0) return 0;
#if defined(ARDUINO)
    // A plain FB2 can be tens of megabytes and may contain a single very long
    // base64/XML text run. The native scanner deliberately avoids allocations
    // in those runs, but it still has to return to FreeRTOS periodically or the
    // ESP32-C3 task watchdog resets the reader. Keep the interval coarse enough
    // not to turn normal SD reads into a context-switch loop.
    bytesSinceYield_ += static_cast<size_t>(got);
    constexpr size_t kYieldInterval = 256 * 1024;
    if (bytesSinceYield_ >= kYieldInterval) {
      bytesSinceYield_ %= kYieldInterval;
      vTaskDelay(1);
    }
#endif
    return static_cast<size_t>(got);
  }
  bool seek(uint64_t pos) override { return file_.seek64(pos); }
  uint64_t tell() const override { return file_.position(); }
  uint64_t size() const override { return file_.fileSize64(); }

 private:
  HalFile& file_;
#if defined(ARDUINO)
  size_t bytesSinceYield_ = 0;
#endif
};
