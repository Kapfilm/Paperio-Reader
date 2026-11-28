#pragma once

#include "PegSolitaireModel.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class PegSolitaireActivity final : public Activity {
 public:
  PegSolitaireActivity(GfxRenderer& renderer, MappedInputManager& input)
      : Activity("PegSolitaire", renderer, input) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool shouldSkipPeriodicUpdate() const override { return true; }
 private:
  enum class Screen { Menu, Playing, Help, ConfirmRestart };
  Screen screen = Screen::Menu;
  PegSolitaireModel board;
  ButtonNavigator navigator;
  int cursor = 0;
  int picked = -1;
  int menuIndex = 0;
  int helpLine = 0;
  bool saveFailed = false;
  uint8_t best[2] = {33, 15};
  void choose();
  void undo();
  void move(int dr, int dc);
  bool load(PegSolitaireModel::Board type);
  bool save();
  void savePreferences();
  void drawPlaying();
  const char* boardName() const;
};
