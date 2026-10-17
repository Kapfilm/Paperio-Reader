#pragma once

#include <cstdint>

#include "HangmanWords.h"

struct HangmanSaveSlot {
  HangmanLanguage language = HangmanLanguage::English;
  HangmanCategory category = HangmanCategory::Animals;
  HangmanDifficulty difficulty = HangmanDifficulty::Easy;
  uint8_t wordIndex = 0;
  uint8_t misses = 0;
  uint8_t cursor = 0;
  bool hintUsed = false;
  uint64_t guessedMask = 0;
};

struct HangmanStats {
  uint32_t wins = 0;
  uint32_t losses = 0;
};

struct HangmanPreferences {
  HangmanLanguage language = HangmanLanguage::English;
  HangmanCategory category = HangmanCategory::Animals;
  HangmanDifficulty difficulty = HangmanDifficulty::Easy;
};

class HangmanStore {
 public:
  static bool hasInProgress();
  static bool load(HangmanSaveSlot& out);
  static bool save(const HangmanSaveSlot& in);
  static bool clear();

  static HangmanStats loadStats();
  static bool saveStats(const HangmanStats& stats);
  static void recordWin();
  static void recordLoss();

  static HangmanPreferences loadPreferences();
  static bool savePreferences(const HangmanPreferences& preferences);
};
