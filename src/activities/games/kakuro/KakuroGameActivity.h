#pragma once
#include <cstdint>
#include <string>
#include "KakuroBoard.h"
#include "activities/Activity.h"
#include "activities/games/GameSaveDebouncer.h"
#include "util/ButtonNavigator.h"
class KakuroGameActivity final:public Activity {
 public:
 KakuroGameActivity(GfxRenderer&,MappedInputManager&,KakuroBoard::Difficulty,bool resume=false);
 void onEnter()override;void onExit()override;void loop()override;void render(RenderLock&&)override;
 static const char*difficultyName(KakuroBoard::Difficulty);
 private:
 enum class State:uint8_t{Playing,GameMenu,Won,Levels,Rules,Combinations,ConfirmRestart};
 State state=State::Playing;bool palette=false,resumeRequested=false,saveFailed=false;
 KakuroBoard board;KakuroBoard::Difficulty difficulty;uint8_t variant=0,cursorRow=1,cursorColumn=1,paletteIndex=0;
 int selected=0,scroll=0;uint32_t elapsedMs=0,lastTickMs=0;GameSaveDebouncer saveDebouncer;ButtonNavigator navigator;
 uint16_t solvedCounts[3]={};
 static constexpr int MenuCount=10;
 void firstCell();void moveCursor(int,int);void openPuzzle(int);void useHint();void solve();bool flushSave();void scheduleSave();void resume();void activate();
 void renderPlaying();void drawGrid();void drawPalette();void drawHeader();void renderList();void renderText();void renderWon();
 std::string runText(bool across)const;std::string helpText()const;void buttons(const char*,const char*,const char*,const char*);
};
