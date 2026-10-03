#include "OtaBootSwitch.h"

#include <Logging.h>
#include <esp_ota_ops.h>
#include <cstddef>
#include <esp_rom_crc.h>
#include <spi_flash_mmap.h>
#include <string.h>

namespace ota_boot {

uint32_t computeSeqCrc(uint32_t seq) {
  return esp_rom_crc32_le(UINT32_MAX, reinterpret_cast<const uint8_t*>(&seq), kOtaSeqCrcLen);
}

static bool selectPartition(const esp_partition_t* dest, uint32_t imageState) {
  if (!dest || dest->type != ESP_PARTITION_TYPE_APP || esp_ota_get_app_partition_count() != 2 ||
      dest->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_0 || dest->subtype > ESP_PARTITION_SUBTYPE_APP_OTA_1) return false;

  const esp_partition_t* otadata =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  if (!otadata) {
    LOG_ERR("BOOT", "otadata partition not found");
    return false;
  }
  if (otadata->size < 2 * SPI_FLASH_SEC_SIZE) {
    LOG_ERR("BOOT", "otadata too small: %u", static_cast<unsigned>(otadata->size));
    return false;
  }

  SelectEntry slots[2] = {};
  if (esp_partition_read(otadata, 0, &slots[0], sizeof(SelectEntry)) != ESP_OK ||
      esp_partition_read(otadata, SPI_FLASH_SEC_SIZE, &slots[1], sizeof(SelectEntry)) != ESP_OK) {
    LOG_ERR("BOOT", "otadata read failed");
    return false;
  }

  // Pick the slot with valid CRC and highest seq, ignoring INVALID/ABORTED.
  int activeIdx = -1;
  uint32_t activeSeq = 0;
  for (int i = 0; i < 2; ++i) {
    if (slots[i].ota_seq == 0 || slots[i].ota_seq == 0xFFFFFFFFu) continue;
    if (slots[i].crc != computeSeqCrc(slots[i].ota_seq)) continue;
    if (slots[i].ota_state == kOtaImgInvalid || slots[i].ota_state == kOtaImgAborted) continue;
    if (activeIdx < 0 || slots[i].ota_seq > activeSeq) {
      activeIdx = i;
      activeSeq = slots[i].ota_seq;
    }
  }
  LOG_INF("BOOT", "otadata: active slot=%d seq=%u", activeIdx, static_cast<unsigned>(activeSeq));

  // ota_seq encoding: (seq - 1) % NUM_OTA_PARTITIONS picks the partition.
  const uint32_t destOtaIdx =
      static_cast<uint32_t>(dest->subtype) - static_cast<uint32_t>(ESP_PARTITION_SUBTYPE_APP_OTA_0);
  if (destOtaIdx > 15) {
    LOG_ERR("BOOT", "dest is not an OTA app partition (subtype=0x%02X)", dest->subtype);
    return false;
  }

  // Find smallest seq > activeSeq such that (seq-1) % 2 == destOtaIdx,
  // assuming 2 OTA partitions (matches our partitions.csv with ota_0 + ota_1).
  if (activeSeq > UINT32_MAX - 3) return false;  // Never wrap into an invalid or older sequence.
  uint32_t newSeq = activeSeq + 1;
  while (((newSeq - 1u) % 2u) != (destOtaIdx % 2u)) ++newSeq;

  SelectEntry next = {};
  next.ota_seq = newSeq;
  memset(next.seq_label, 0xFF, sizeof(next.seq_label));
  next.ota_state = imageState;
  next.crc = computeSeqCrc(next.ota_seq);

  // Write to the OTHER slot (so the bootloader sees a higher seq there).
  const int targetSlot = (activeIdx == 0) ? 1 : 0;
  const size_t targetOff = static_cast<size_t>(targetSlot) * SPI_FLASH_SEC_SIZE;

  if (esp_partition_erase_range(otadata, targetOff, SPI_FLASH_SEC_SIZE) != ESP_OK) {
    LOG_ERR("BOOT", "otadata erase failed (slot=%d)", targetSlot);
    return false;
  }
  if (esp_partition_write(otadata, targetOff, &next, sizeof(next)) != ESP_OK) {
    LOG_ERR("BOOT", "otadata write failed (slot=%d)", targetSlot);
    return false;
  }

  LOG_INF("BOOT", "otadata: wrote slot=%d seq=%u crc=0x%08x -> %s", targetSlot, static_cast<unsigned>(newSeq),
          static_cast<unsigned>(next.crc), dest->label);
  return true;
}

bool switchTo(const esp_partition_t* dest) {
  return selectPartition(dest, kOtaImgNew);
}

bool switchToInstalled(const esp_partition_t* dest) {
  const auto* running = esp_ota_get_running_partition();
  if (!running || !dest || dest->address == running->address) return false;
  // Explicitly selecting an installed, validated image is not an OTA trial.
  // Other firmware may not implement IDF's PENDING_VERIFY confirmation.
  return selectPartition(dest, ESP_OTA_IMG_VALID);
}

bool rollbackCandidateMatches(const esp_partition_t* running, const esp_partition_t* previous) {
  if (!running || !previous || running == previous || esp_ota_get_app_partition_count() != 2) return false;
  const esp_partition_t* data =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  if (!data || data->size < 2 * SPI_FLASH_SEC_SIZE) return false;
  SelectEntry entries[2] = {};
  for (int i = 0; i < 2; ++i) {
    if (esp_partition_read(data, i * SPI_FLASH_SEC_SIZE, &entries[i], sizeof(SelectEntry)) != ESP_OK) return false;
    const auto& e = entries[i];
    if (!e.ota_seq || e.ota_seq == UINT32_MAX || e.crc != computeSeqCrc(e.ota_seq) ||
        e.ota_state == kOtaImgInvalid || e.ota_state == kOtaImgAborted) return false;
  }
  if (entries[0].ota_seq == entries[1].ota_seq) return false;
  const int active = entries[0].ota_seq > entries[1].ota_seq ? 0 : 1;
  return (entries[active].ota_seq - 1) % 2 + ESP_PARTITION_SUBTYPE_APP_OTA_0 == running->subtype &&
         (entries[1 - active].ota_seq - 1) % 2 + ESP_PARTITION_SUBTYPE_APP_OTA_0 == previous->subtype;
}

bool rearmPending(const esp_partition_t* running) {
  if (!running || esp_ota_get_app_partition_count() != 2 ||
      running->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_0 || running->subtype > ESP_PARTITION_SUBTYPE_APP_OTA_1) return false;
  const esp_partition_t* data =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  if (!data || data->encrypted || data->size < 2 * SPI_FLASH_SEC_SIZE) return false;
  SelectEntry slots[2] = {};
  int selected = -1;
  for (int i = 0; i < 2; ++i) {
    if (esp_partition_read(data, i * SPI_FLASH_SEC_SIZE, &slots[i], sizeof(SelectEntry)) != ESP_OK) return false;
    const auto& entry = slots[i];
    if (!entry.ota_seq || entry.ota_seq == UINT32_MAX || entry.crc != computeSeqCrc(entry.ota_seq)) continue;
    if ((entry.ota_seq - 1) % 2 != static_cast<uint32_t>(running->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_0)) continue;
    if (selected < 0 || entry.ota_seq > slots[selected].ota_seq) selected = i;
  }
  if (selected < 0 || slots[selected].ota_state != ESP_OTA_IMG_PENDING_VERIFY) return false;
  const uint32_t state = kOtaImgNew;
  const size_t offset = selected * SPI_FLASH_SEC_SIZE + offsetof(SelectEntry, ota_state);
  if (esp_partition_write(data, offset, &state, sizeof(state)) != ESP_OK) return false;
  uint32_t readback = UINT32_MAX;
  return esp_partition_read(data, offset, &readback, sizeof(readback)) == ESP_OK && readback == state;
}

}  // namespace ota_boot
