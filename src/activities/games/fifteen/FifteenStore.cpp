#include "FifteenStore.h"

#include <HalStorage.h>

namespace {

constexpr const char* kDir = "/.crosspoint";
constexpr const char* kSavePath = "/.crosspoint/fifteen.bin";
constexpr const char* kPreferencesPath = "/.crosspoint/fifteen_prefs.bin";
constexpr uint8_t kSaveVersion = 1;
constexpr uint8_t kPreferencesVersion = 1;

bool validTiles(const std::array<uint8_t, 16>& tiles) {
  uint16_t seen = 0;
  for (const uint8_t tile : tiles) {
    if (tile > 15) return false;
    const uint16_t bit = static_cast<uint16_t>(1u << tile);
    if ((seen & bit) != 0) return false;
    seen |= bit;
  }
  return seen == UINT16_MAX;
}

}  // namespace

bool FifteenStore::hasInProgress() {
  FifteenSaveSlot slot;
  return load(slot);
}

bool FifteenStore::load(FifteenSaveSlot& out) {
  HalFile file;
  uint8_t version = 0;
  if (!Storage.openFileForRead("F15", kSavePath, file) || file.read(&version, sizeof(version)) != sizeof(version) ||
      version != kSaveVersion ||
      file.read(out.tiles.data(), out.tiles.size()) != static_cast<int>(out.tiles.size()) ||
      file.read(reinterpret_cast<uint8_t*>(&out.moves), sizeof(out.moves)) != static_cast<int>(sizeof(out.moves))) {
    return false;
  }
  return validTiles(out.tiles);
}

bool FifteenStore::save(const FifteenSaveSlot& slot) {
  if (!validTiles(slot.tiles) || (!Storage.exists(kDir) && !Storage.mkdir(kDir))) return false;
  HalFile file;
  if (!Storage.openFileForWrite("F15", kSavePath, file) || file.write(&kSaveVersion, sizeof(kSaveVersion)) != 1 ||
      file.write(slot.tiles.data(), slot.tiles.size()) != slot.tiles.size() ||
      file.write(reinterpret_cast<const uint8_t*>(&slot.moves), sizeof(slot.moves)) != sizeof(slot.moves)) {
    return false;
  }
  file.flush();
  return true;
}

bool FifteenStore::clear() { return !Storage.exists(kSavePath) || Storage.remove(kSavePath); }

FifteenControlMode FifteenStore::loadControlMode() {
  HalFile file;
  uint8_t version = 0;
  uint8_t mode = 0;
  if (!Storage.openFileForRead("F15", kPreferencesPath, file) ||
      file.read(&version, sizeof(version)) != sizeof(version) || version != kPreferencesVersion ||
      file.read(&mode, sizeof(mode)) != sizeof(mode) || mode > static_cast<uint8_t>(FifteenControlMode::Tiles)) {
    return FifteenControlMode::EmptyCell;
  }
  return static_cast<FifteenControlMode>(mode);
}

bool FifteenStore::saveControlMode(const FifteenControlMode mode) {
  if (!Storage.exists(kDir) && !Storage.mkdir(kDir)) return false;
  HalFile file;
  const uint8_t value = static_cast<uint8_t>(mode);
  if (!Storage.openFileForWrite("F15", kPreferencesPath, file) ||
      file.write(&kPreferencesVersion, sizeof(kPreferencesVersion)) != sizeof(kPreferencesVersion) ||
      file.write(&value, sizeof(value)) != sizeof(value)) {
    return false;
  }
  file.flush();
  return true;
}
