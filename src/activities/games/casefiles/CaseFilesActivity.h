#pragma once
#include "CaseFilesModel.h"
#include "CaseFilesControls.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
class CaseFilesActivity final:public Activity{
 public:CaseFilesActivity(GfxRenderer&r,MappedInputManager&i):Activity("CaseFiles",r,i){}
 void onEnter()override;void onExit()override;void loop()override;void render(RenderLock&&)override;
 bool shouldSkipPeriodicUpdate()const override{return true;}
 private:
 enum class Screen{Playing,Menu,Clues,Tables,Levels,Rules,ConfirmRestart};Screen screen=Screen::Playing;
 enum class Focus{Clues,Tabs,Grid};Focus focus=Focus::Grid;
 casefiles::HoldButton confirmHold,rightHold;bool cluePopup=false;int tabCursor=0,popupLine=0,victoryChoice=0;bool victoryPopup=false,victoryAnnounced=false;
 struct Layout{int themeY,storyY,goalY,clueY,clueH,tabsY,labelsY,gridY,gridX,cell,bottom,tabW;};
 struct CluePage{int first,last;};
 Layout layout()const;std::string goalText()const;std::vector<CluePage> cluePages()const;
 int cluePageIndex(const std::vector<CluePage>& pages)const;
 void playingLoop();void cycleFocus();void drawCluePopup();
 struct PopupLine{std::string text;int clue;};
 std::vector<PopupLine> popupLines()const;int popupRows()const;
 std::string gameTitle()const;void checkVictory();void drawVictoryPopup();
 casefiles::Model game;ButtonNavigator nav;int selected=0,clue=0,helpLine=0;bool saveFailed=false;std::string message;
 bool save();void load();void activate();void drawGrid();void drawClue();void drawRules();void hints(const char*b,const char*c,const char*u,const char*d);
};
