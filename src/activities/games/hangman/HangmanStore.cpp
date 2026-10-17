#include "HangmanStore.h"

#include <HalStorage.h>
#include <Logging.h>

namespace {

constexpr const char* kDir = "/.crosspoint";
constexpr const char* kSavePath = "/.crosspoint/hangman.bin";
constexpr const char* kStatsPath = "/.crosspoint/hangman_stats.bin";
constexpr const char* kPreferencesPath = "/.crosspoint/hangman_prefs.bin";
constexpr uint8_t kSaveVersion = 2;
constexpr uint8_t kStatsVersion = 1;
constexpr uint8_t kPreferencesVersion = 2;

bool ensureDir() { return Storage.exists(kDir) || Storage.mkdir(kDir); }

bool validLanguage(const uint8_t value) { return value <= static_cast<uint8_t>(HangmanLanguage::Russian); }
bool validCategory(const uint8_t value) { return value <= static_cast<uint8_t>(HangmanCategory::Objects); }
bool validDifficulty(const uint8_t value) { return value <= static_cast<uint8_t>(HangmanDifficulty::Hard); }

bool writeBytes(HalFile& file, const void* value, const size_t size) {
  return file.write(reinterpret_cast<const uint8_t*>(value), size) == size;
}

bool readBytes(HalFile& file, void* value, const size_t size) {
  return file.read(reinterpret_cast<uint8_t*>(value), size) == static_cast<int>(size);
}

}  // namespace

bool HangmanStore::hasInProgress() { return Storage.exists(kSavePath); }

bool HangmanStore::load(HangmanSaveSlot& out) {
  HalFile file;
  if (!Storage.openFileForRead("HNG", kSavePath, file)) return false;
  uint8_t version = 0;
  uint8_t language = 0;
  uint8_t category = 0;
  uint8_t difficulty = static_cast<uint8_t>(HangmanDifficulty::Easy);
  uint8_t hintUsed = 0;
  if (!readBytes(file, &version, sizeof(version)) || (version != 1 && version != kSaveVersion) ||
      !readBytes(file, &language, sizeof(language)) || !readBytes(file, &category, sizeof(category)) ||
      (version >= 2 && !readBytes(file, &difficulty, sizeof(difficulty))) ||
      !readBytes(file, &out.wordIndex, sizeof(out.wordIndex)) || !readBytes(file, &out.misses, sizeof(out.misses)) ||
      !readBytes(file, &out.cursor, sizeof(out.cursor)) || !readBytes(file, &hintUsed, sizeof(hintUsed)) ||
      !readBytes(file, &out.guessedMask, sizeof(out.guessedMask))) {
    return false;
  }
  if (!validLanguage(language) || !validCategory(category) || !validDifficulty(difficulty) || out.misses > 6) {
    return false;
  }
  out.language = static_cast<HangmanLanguage>(language);
  out.category = static_cast<HangmanCategory>(category);
  out.difficulty = static_cast<HangmanDifficulty>(difficulty);
  if (out.difficulty == HangmanDifficulty::Hard && out.misses > 5) return false;
  if (out.wordIndex >= hangmanWordCount(out.language, out.category, out.difficulty)) return false;
  out.hintUsed = hintUsed != 0;
  const size_t itemCount =
      hangmanAlphabetSize(out.language) + (out.difficulty == HangmanDifficulty::Easy ? 1 : 0);
  if (out.cursor >= itemCount) out.cursor = 0;
  return true;
}

bool HangmanStore::save(const HangmanSaveSlot& in) {
  if (!ensureDir()) return false;
  HalFile file;
  if (!Storage.openFileForWrite("HNG", kSavePath, file)) return false;
  const uint8_t language = static_cast<uint8_t>(in.language);
  const uint8_t category = static_cast<uint8_t>(in.category);
  const uint8_t difficulty = static_cast<uint8_t>(in.difficulty);
  const uint8_t hintUsed = in.hintUsed ? 1 : 0;
  if (!writeBytes(file, &kSaveVersion, sizeof(kSaveVersion)) || !writeBytes(file, &language, sizeof(language)) ||
      !writeBytes(file, &category, sizeof(category)) || !writeBytes(file, &difficulty, sizeof(difficulty)) ||
      !writeBytes(file, &in.wordIndex, sizeof(in.wordIndex)) ||
      !writeBytes(file, &in.misses, sizeof(in.misses)) || !writeBytes(file, &in.cursor, sizeof(in.cursor)) ||
      !writeBytes(file, &hintUsed, sizeof(hintUsed)) || !writeBytes(file, &in.guessedMask, sizeof(in.guessedMask))) {
    return false;
  }
  file.flush();
  return true;
}

bool HangmanStore::clear() { return !Storage.exists(kSavePath) || Storage.remove(kSavePath); }

HangmanStats HangmanStore::loadStats() {
  HangmanStats stats;
  if (!Storage.exists(kStatsPath)) return stats;
  HalFile file;
  uint8_t version = 0;
  if (!Storage.openFileForRead("HNG", kStatsPath, file) || !readBytes(file, &version, sizeof(version)) ||
      version != kStatsVersion || !readBytes(file, &stats.wins, sizeof(stats.wins)) ||
      !readBytes(file, &stats.losses, sizeof(stats.losses))) {
    return {};
  }
  return stats;
}

bool HangmanStore::saveStats(const HangmanStats& stats) {
  if (!ensureDir()) return false;
  HalFile file;
  if (!Storage.openFileForWrite("HNG", kStatsPath, file)) return false;
  if (!writeBytes(file, &kStatsVersion, sizeof(kStatsVersion)) || !writeBytes(file, &stats.wins, sizeof(stats.wins)) ||
      !writeBytes(file, &stats.losses, sizeof(stats.losses))) {
    return false;
  }
  file.flush();
  return true;
}

void HangmanStore::recordWin() {
  HangmanStats stats = loadStats();
  if (stats.wins != UINT32_MAX) ++stats.wins;
  if (!saveStats(stats)) LOG_ERR("HNG", "Failed to save win statistics");
}

void HangmanStore::recordLoss() {
  HangmanStats stats = loadStats();
  if (stats.losses != UINT32_MAX) ++stats.losses;
  if (!saveStats(stats)) LOG_ERR("HNG", "Failed to save loss statistics");
}

HangmanPreferences HangmanStore::loadPreferences() {
  HangmanPreferences preferences;
  if (!Storage.exists(kPreferencesPath)) return preferences;
  HalFile file;
  uint8_t version = 0;
  uint8_t language = 0;
  uint8_t category = 0;
  uint8_t difficulty = static_cast<uint8_t>(HangmanDifficulty::Easy);
  if (!Storage.openFileForRead("HNG", kPreferencesPath, file) || !readBytes(file, &version, sizeof(version)) ||
      (version != 1 && version != kPreferencesVersion) || !readBytes(file, &language, sizeof(language)) ||
      !readBytes(file, &category, sizeof(category)) ||
      (version >= 2 && !readBytes(file, &difficulty, sizeof(difficulty))) || !validLanguage(language) ||
      !validCategory(category) || !validDifficulty(difficulty)) {
    return preferences;
  }
  preferences.language = static_cast<HangmanLanguage>(language);
  preferences.category = static_cast<HangmanCategory>(category);
  preferences.difficulty = static_cast<HangmanDifficulty>(difficulty);
  return preferences;
}

bool HangmanStore::savePreferences(const HangmanPreferences& preferences) {
  if (!ensureDir()) return false;
  HalFile file;
  if (!Storage.openFileForWrite("HNG", kPreferencesPath, file)) return false;
  const uint8_t language = static_cast<uint8_t>(preferences.language);
  const uint8_t category = static_cast<uint8_t>(preferences.category);
  const uint8_t difficulty = static_cast<uint8_t>(preferences.difficulty);
  if (!writeBytes(file, &kPreferencesVersion, sizeof(kPreferencesVersion)) ||
      !writeBytes(file, &language, sizeof(language)) || !writeBytes(file, &category, sizeof(category)) ||
      !writeBytes(file, &difficulty, sizeof(difficulty))) {
    return false;
  }
  file.flush();
  return true;
}
