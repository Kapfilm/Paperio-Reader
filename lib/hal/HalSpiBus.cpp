#include "HalSpiBus.h"

#include <Logging.h>

namespace {
// A single SPI transaction is short (a panel refresh is driven in chunks, an SD
// transfer is block-sized), so anything past this means a lock-order mistake or
// a wedged transfer. Waiting forever would turn that into a silent hang; time
// out and log once, but keep waiting. Callers cannot skip their SPI operation:
// proceeding unlocked would race the live transaction and may release another
// task's Arduino SPI mutex, causing a FreeRTOS priority-disinherit assertion.
constexpr TickType_t SPI_LOCK_TIMEOUT_TICKS = pdMS_TO_TICKS(5000);
}  // namespace

HalSpiBus::HalSpiBus() {
  mutex = xSemaphoreCreateRecursiveMutex();
  if (mutex == nullptr) {
    LOG_ERR("SPI", "Failed to create SPI bus mutex - display/SD access is unserialized");
  }
}

HalSpiBus& HalSpiBus::getInstance() {
  static HalSpiBus spiBus;
  return spiBus;
}

void HalSpiBus::begin() { (void)getInstance(); }

HalSpiBus::Lock::Lock() {
  auto& bus = HalSpiBus::getInstance();
  if (bus.mutex == nullptr) {
    LOG_ERR("SPI", "SPI bus mutex not initialized, proceeding unlocked");
    return;
  }
  bool warned = false;
  while (xSemaphoreTakeRecursive(bus.mutex, SPI_LOCK_TIMEOUT_TICKS) != pdTRUE) {
    if (!warned) {
      LOG_ERR("SPI", "Timed out acquiring SPI bus mutex - still waiting (possible lock-order bug)");
      warned = true;
    }
  }
  acquired = true;
}

HalSpiBus::Lock::~Lock() {
  if (!acquired) return;
  xSemaphoreGiveRecursive(HalSpiBus::getInstance().mutex);
}
