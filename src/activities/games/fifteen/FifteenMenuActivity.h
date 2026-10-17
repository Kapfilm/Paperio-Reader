#pragma once

#include <cstdint>
#include <vector>

#include "FifteenStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class FifteenMenuActivity final : public Activity {
 public:
  FifteenMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FifteenMenu", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool shouldSkipPeriodicUpdate() const override { return true; }

 private:
  enum class ItemKind : uint8_t { Continue, NewGame, Controls };

  std::vector<ItemKind> items;
  int selected = 0;
  FifteenControlMode controlMode = FifteenControlMode::EmptyCell;
  ButtonNavigator buttonNavigator;

  void buildItems();
  void activateSelected();
  const char* controlModeName() const;
  const char* controlModeDescription() const;
};
