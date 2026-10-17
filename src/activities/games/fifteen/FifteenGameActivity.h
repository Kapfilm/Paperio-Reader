#pragma once

#include <array>
#include <cstdint>

#include "FifteenStore.h"
#include "activities/Activity.h"

class FifteenGameActivity final : public Activity {
 public:
  FifteenGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, FifteenControlMode controlMode,
                      bool startNew)
      : Activity("Fifteen", renderer, mappedInput), controlMode(controlMode), startNew(startNew) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool shouldSkipPeriodicUpdate() const override { return true; }

 private:
  enum class State : uint8_t { Playing, Won };

  State state = State::Playing;
  std::array<uint8_t, 16> tiles{};
  uint8_t blank = 15;
  uint32_t moves = 0;
  FifteenControlMode controlMode = FifteenControlMode::EmptyCell;
  bool startNew = false;

  void newGame();
  void restoreOrCreate();
  void moveBlank(int dr, int dc);
  void saveGame() const;
  bool isSolved() const;
  void renderPlaying();
  void renderWon();
};
