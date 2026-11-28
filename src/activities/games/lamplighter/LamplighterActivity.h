#pragma once
#include "LamplighterModel.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
class LamplighterActivity final : public Activity {
 public:
  LamplighterActivity(GfxRenderer& renderer,MappedInputManager& input):Activity("Lamplighter",renderer,input){}
  void onEnter() override;void onExit() override;void loop() override;void render(RenderLock&&) override;
  bool shouldSkipPeriodicUpdate()const override{return true;}
 private:
  enum class Screen{Menu,Playing,Help,ConfirmRestart};Screen screen=Screen::Menu;
  lamplighter::Model game;ButtonNavigator navigator;int cursor=0,selected=0,helpOffset=0;
  bool lockMode=false,saveFailed=false;
  bool save();void load();void activate();void drawGame();void drawMenu();void drawHelp();
  void hints(const char*,const char*,const char*,const char*);
  void changePuzzle(int delta);void move(int dr,int dc);const char* levelName()const;
};
