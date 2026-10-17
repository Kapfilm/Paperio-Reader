#include "KakuroStore.h"

#include <HalStorage.h>
#include <Logging.h>

namespace {

constexpr const char* kDirectory = "/.crosspoint";
constexpr const char* kSavePath = "/.crosspoint/kakuro.bin";
constexpr const char* kStatsPath = "/.crosspoint/kakuro_stats.bin";
constexpr uint8_t kSaveVersion = 1;
constexpr uint8_t kStatsVersion = 1;

bool ensureDirectory() {
  if (Storage.exists(kDirectory)) return true;
  return Storage.mkdir(kDirectory);
}

}  // namespace

bool KakuroStore::hasInProgress() { return Storage.exists(kSavePath); }

bool KakuroStore::load(KakuroSaveSlot& out) {
  HalFile file;
  if (!Storage.openFileForRead("KAK", kSavePath, file)) return false;

  uint8_t version = 0;
  uint8_t difficulty = 0;
  uint8_t flags = 0;
  if (file.read(&version, 1) != 1 || version != kSaveVersion || file.read(&difficulty, 1) != 1 || difficulty > 2 ||
      file.read(&out.variant, 1) != 1 || out.variant > 2 ||
      file.read(reinterpret_cast<uint8_t*>(&out.elapsedSec), sizeof(out.elapsedSec)) != sizeof(out.elapsedSec) ||
      file.read(&out.cursorRow, 1) != 1 || file.read(&out.cursorColumn, 1) != 1 || file.read(&out.mistakes, 1) != 1 ||
      file.read(&out.hintsLeft, 1) != 1 || file.read(&flags, 1) != 1) {
    LOG_ERR("KAK", "Invalid or truncated save");
    return false;
  }

  out.difficulty = static_cast<KakuroBoard::Difficulty>(difficulty);
  out.showErrors = (flags & 1u) != 0;
  if (!out.board.build(out.difficulty, out.variant) || !out.board.readUserFrom(file)) return false;
  if (!out.board.isPlayable(out.cursorRow, out.cursorColumn)) {
    out.cursorRow = 1;
    out.cursorColumn = 1;
  }
  return true;
}

bool KakuroStore::save(const KakuroSaveSlot& in) {
  if (!ensureDirectory()) return false;
  HalFile file;
  if (!Storage.openFileForWrite("KAK", kSavePath, file)) return false;

  const uint8_t version = kSaveVersion;
  const uint8_t difficulty = static_cast<uint8_t>(in.difficulty);
  const uint8_t flags = in.showErrors ? 1 : 0;
  if (file.write(&version, 1) != 1 || file.write(&difficulty, 1) != 1 || file.write(&in.variant, 1) != 1 ||
      file.write(reinterpret_cast<const uint8_t*>(&in.elapsedSec), sizeof(in.elapsedSec)) != sizeof(in.elapsedSec) ||
      file.write(&in.cursorRow, 1) != 1 || file.write(&in.cursorColumn, 1) != 1 || file.write(&in.mistakes, 1) != 1 ||
      file.write(&in.hintsLeft, 1) != 1 || file.write(&flags, 1) != 1 || !in.board.writeUserTo(file)) {
    LOG_ERR("KAK", "Failed to write save");
    return false;
  }
  file.flush();
  return true;
}

bool KakuroStore::clear() {
  if (!Storage.exists(kSavePath)) return true;
  return Storage.remove(kSavePath);
}

KakuroStats KakuroStore::loadStats() {
  KakuroStats stats;
  if (!Storage.exists(kStatsPath)) return stats;
  HalFile file;
  if (!Storage.openFileForRead("KAK", kStatsPath, file)) return stats;
  uint8_t version = 0;
  if (file.read(&version, 1) != 1 || version != kStatsVersion) return KakuroStats{};
  if (file.read(reinterpret_cast<uint8_t*>(stats.bestTimeSec), sizeof(stats.bestTimeSec)) !=
          sizeof(stats.bestTimeSec) ||
      file.read(reinterpret_cast<uint8_t*>(stats.completedCount), sizeof(stats.completedCount)) !=
          sizeof(stats.completedCount) ||
      file.read(reinterpret_cast<uint8_t*>(stats.startedCount), sizeof(stats.startedCount)) !=
          sizeof(stats.startedCount)) {
    return KakuroStats{};
  }
  return stats;
}

bool KakuroStore::saveStats(const KakuroStats& stats) {
  if (!ensureDirectory()) return false;
  HalFile file;
  if (!Storage.openFileForWrite("KAK", kStatsPath, file)) return false;
  const uint8_t version = kStatsVersion;
  if (file.write(&version, 1) != 1 ||
      file.write(reinterpret_cast<const uint8_t*>(stats.bestTimeSec), sizeof(stats.bestTimeSec)) !=
          sizeof(stats.bestTimeSec) ||
      file.write(reinterpret_cast<const uint8_t*>(stats.completedCount), sizeof(stats.completedCount)) !=
          sizeof(stats.completedCount) ||
      file.write(reinterpret_cast<const uint8_t*>(stats.startedCount), sizeof(stats.startedCount)) !=
          sizeof(stats.startedCount)) {
    return false;
  }
  file.flush();
  return true;
}

void KakuroStore::recordStart(KakuroBoard::Difficulty difficulty) {
  const uint8_t index = static_cast<uint8_t>(difficulty);
  if (index > 2) return;
  KakuroStats stats = loadStats();
  if (stats.startedCount[index] < 0xFFFF) stats.startedCount[index]++;
  saveStats(stats);
}

void KakuroStore::recordCompletion(KakuroBoard::Difficulty difficulty, uint16_t timeSec) {
  const uint8_t index = static_cast<uint8_t>(difficulty);
  if (index > 2) return;
  KakuroStats stats = loadStats();
  if (stats.completedCount[index] < 0xFFFF) stats.completedCount[index]++;
  if (stats.bestTimeSec[index] == 0 || timeSec < stats.bestTimeSec[index]) stats.bestTimeSec[index] = timeSec;
  saveStats(stats);
}
