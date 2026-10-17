#pragma once

#include <cstdint>

#include "KakuroBoard.h"

struct KakuroStats {
  uint16_t bestTimeSec[3] = {0, 0, 0};
  uint16_t completedCount[3] = {0, 0, 0};
  uint16_t startedCount[3] = {0, 0, 0};
};

struct KakuroSaveSlot {
  KakuroBoard board;
  KakuroBoard::Difficulty difficulty = KakuroBoard::Difficulty::Easy;
  uint8_t variant = 0;
  uint16_t elapsedSec = 0;
  uint8_t cursorRow = 1;
  uint8_t cursorColumn = 1;
  uint8_t mistakes = 0;
  uint8_t hintsLeft = 3;
  bool showErrors = false;
};

class KakuroStore {
 public:
  static bool hasInProgress();
  static bool load(KakuroSaveSlot& out);
  static bool save(const KakuroSaveSlot& in);
  static bool clear();

  static KakuroStats loadStats();
  static bool saveStats(const KakuroStats& stats);
  static void recordStart(KakuroBoard::Difficulty difficulty);
  static void recordCompletion(KakuroBoard::Difficulty difficulty, uint16_t timeSec);
};
