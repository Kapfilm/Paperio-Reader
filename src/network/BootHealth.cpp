#include "BootHealth.h"
#include "BootHealthPolicy.h"
#include "OtaBootSwitch.h"

#include <Arduino.h>
#include <Logging.h>
#include <atomic>
#include <esp_app_desc.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <nvs.h>

namespace boot_health {
namespace {
Record record;
HealthWindow window;
std::atomic<bool> uiRendered{false};
bool storageReady = false;
bool confirmed = false;
bool recovery = false;
bool started = false;
bool updateStaged = false;
const esp_partition_t* running = nullptr;

bool identity(const esp_partition_t* part, ImageId& id) {
  esp_app_desc_t desc{};
  if (!part || esp_ota_get_partition_description(part, &desc) != ESP_OK) return false;
  id.address = part->address;
  memcpy(id.sha, desc.app_elf_sha256, sizeof(id.sha));
  return true;
}
bool save(const Record& value) {
  nvs_handle_t handle;
  if (nvs_open("boot-health", NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t err = nvs_set_blob(handle, "state", &value, sizeof(value));
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return err == ESP_OK;
}
Record load() {
  Record value;
  nvs_handle_t handle;
  if (nvs_open("boot-health", NVS_READONLY, &handle) != ESP_OK) return value;
  size_t size = sizeof(value);
  if (nvs_get_blob(handle, "state", &value, &size) != ESP_OK || size != sizeof(value) || !valid(value)) value = Record{};
  nvs_close(handle);
  return value;
}
bool wasCrash(esp_reset_reason_t reason) {
  return reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT ||
         reason == ESP_RST_WDT || reason == ESP_RST_CPU_LOCKUP;
}
void rollback() {
  recovery = true;
  const esp_partition_t* previous = esp_ota_get_next_update_partition(running);
  ImageId previousId;
  // Exactly two OTA partitions: the IDF rollback candidate must be the recorded reader.
  if (!record.armed || esp_ota_get_app_partition_count() != 2 || !identity(previous, previousId) ||
      !equal(previousId, record.fallback) || !ota_boot::rollbackCandidateMatches(running, previous) ||
      !esp_ota_check_rollback_is_possible()) {
    LOG_ERR("BOOT", "No verified previous reader; entering SD recovery");
    return;
  }
  // Consume the attempt durably before touching otadata. Never bounce between bad images.
  record.blocked = 1;
  if (!save(record)) {
    LOG_ERR("BOOT", "Cannot persist rollback latch; entering SD recovery");
    return;
  }
  const esp_err_t err = esp_ota_mark_app_invalid_rollback();
  if (err != ESP_OK) {
    LOG_ERR("BOOT", "Rollback rejected: %s; entering SD recovery", esp_err_to_name(err));
    return;
  }
  LOG_ERR("BOOT", "Rolling back after %u crashes to %s", static_cast<unsigned>(record.failures), previous->label);
  esp_restart();
}
}
void begin() {
  running = esp_ota_get_running_partition();
  ImageId image;
  if (!identity(running, image)) return;
  record = onBoot(load(), image, wasCrash(esp_reset_reason()));
  storageReady = save(record);
  started = true;
  LOG_INF("BOOT", "Health: crashes=%u trusted=%u fallback=%u nvs=%d", static_cast<unsigned>(record.failures),
          static_cast<unsigned>(record.trusted), static_cast<unsigned>(record.armed), storageReady);
  if (record.blocked) recovery = true;
  else if (record.failures >= kCrashLimit) {
    if (storageReady) rollback();
    else recovery = true;
  }
}
void rendered() { uiRendered.store(true, std::memory_order_release); }
bool recoveryRequired() { return recovery; }
void tick(bool renderBusy) {
  if (!started || !storageReady || confirmed || recovery || updateStaged ||
      !window.tick(millis(), uiRendered.load(std::memory_order_acquire), renderBusy)) return;
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_err_t stateError = esp_ota_get_state_partition(running, &state);
  // A directly flashed app0 can run with empty otadata. It may still become a
  // known healthy source for the next update; there is no pending state to confirm.
  if (stateError != ESP_OK && stateError != ESP_ERR_NOT_FOUND) return;
  if (state == ESP_OTA_IMG_PENDING_VERIFY || state == ESP_OTA_IMG_NEW) {
    const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
      LOG_ERR("BOOT", "Cannot confirm firmware: %s", esp_err_to_name(err));
      storageReady = false;  // No repeated flash writes on a failed confirmation.
      return;
    }
  } else if (state != ESP_OTA_IMG_VALID && state != ESP_OTA_IMG_UNDEFINED) return;
  Record healthy = record;
  healthy.failures = 0;
  healthy.trusted = 1;
  if (!save(healthy)) {
    LOG_ERR("BOOT", "Cannot save healthy startup");
    storageReady = false;
    return;
  }
  record = healthy;
  confirmed = true;
  LOG_INF("BOOT", "Firmware confirmed after healthy UI/main-loop interval");
}
bool stageUpdate(const esp_partition_t* target) {
  ImageId image;
  if (!started || target == running || !identity(target, image)) return false;
  const Record next = stageUpdate(record, image);
  if (!save(next)) return false;
  updateStaged = true;  // Do not overwrite the next image's record before reboot.
  return true;
}
void preparePlannedReset() {
  if (!started || !running) return;
  // Never undo an updater's partition selection or a rollback's INVALID state.
  if (esp_ota_get_boot_partition() != running) return;
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY &&
      !ota_boot::rearmPending(running)) {
    LOG_ERR("BOOT", "Could not preserve probation for planned reset; native rollback remains active");
  }
}
}

extern "C" {
void __real_esp_restart(void) __attribute__((noreturn));
void __real_esp_deep_sleep_start(void) __attribute__((noreturn));
void __wrap_esp_restart(void) {
  boot_health::preparePlannedReset();
  __real_esp_restart();
}
void __wrap_esp_deep_sleep_start(void) {
  boot_health::preparePlannedReset();
  __real_esp_deep_sleep_start();
}
}
