#include "SokobanStore.h"

#include <HalStorage.h>

namespace {
constexpr const char* kDir = "/.crosspoint";
constexpr const char* kSavePath = "/.crosspoint/sokoban.bin";
constexpr uint8_t kSaveVersion = 1;
}  // namespace

bool SokobanStore::saveLevel(int level) {
  if (!Storage.exists(kDir) && !Storage.mkdir(kDir)) return false;
  HalFile file;
  if (!Storage.openFileForWrite("SOK", kSavePath, file)) return false;
  if (file.write(&kSaveVersion, 1) != 1) return false;
  if (file.write(reinterpret_cast<const uint8_t*>(&level), sizeof(level)) != sizeof(level)) return false;
  file.flush();
  return true;
}

bool SokobanStore::loadLevel(int& level) {
  if (!Storage.exists(kSavePath)) return false;
  HalFile file;
  if (!Storage.openFileForRead("SOK", kSavePath, file)) return false;
  uint8_t version = 0;
  if (file.read(&version, 1) != 1 || version != kSaveVersion) return false;
  return file.read(reinterpret_cast<uint8_t*>(&level), sizeof(level)) == sizeof(level);
}
