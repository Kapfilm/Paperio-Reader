#pragma once

#include <cstdint>

#include "SokobanBoard.h"
#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"
#include "levels_data.h"
#include "util/ButtonNavigator.h"

class SokobanGameActivity final : public Activity {
 public:
  SokobanGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Sokoban", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State : uint8_t { Playing, Won, LevelSelect };

  State state = State::Playing;
  SokobanBoard board;
  int currentLevel = 0;
  int selectedLevel = 0;
  int moves = 0;
  ButtonNavigator buttonNavigator;
  static constexpr int MAX_LEVELS = 160;
  int totalLevels = 0;
  uint32_t levelOffsets[MAX_LEVELS] = {};

  void loadLevel(int index);
  void move(int dr, int dc);
  void undo();
  void drawPlaying();
  void drawBoard(const Rect& content);
  void drawWon();
  void drawLevelSelect();
};
