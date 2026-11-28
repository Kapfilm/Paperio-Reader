#pragma once
#include <cstdint>
#include "KakuroBoard.h"
struct KakuroStats {
 uint16_t bestTimeSec[3]={}; uint16_t completedCount[3]={}; uint16_t startedCount[3]={};
 uint8_t positions[3]={}; uint8_t completed[3][25]={};
};
struct KakuroSaveSlot {
 KakuroBoard board; KakuroBoard::Difficulty difficulty=KakuroBoard::Difficulty::Easy;
 uint8_t variant=0; uint16_t elapsedSec=0; uint8_t cursorRow=1,cursorColumn=1;
};
class KakuroStore {
 public:
 static bool hasInProgress(); static bool load(KakuroSaveSlot&); static bool save(const KakuroSaveSlot&); static bool clear();
 static KakuroStats loadStats(); static bool saveStats(const KakuroStats&);
 static void recordStart(KakuroBoard::Difficulty);
 static void recordCompletion(KakuroBoard::Difficulty,uint16_t,uint8_t);
};
