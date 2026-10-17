#pragma once

#include <cstdint>
#include <vector>

#include "HangmanStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class HangmanMenuActivity final : public Activity {
 public:
  HangmanMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("HangmanMenu", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool shouldSkipPeriodicUpdate() const override { return true; }

 private:
  enum class ItemKind : uint8_t { Continue, NewGame, TwoPlayer, Difficulty, Language, Category, Stats };

  std::vector<ItemKind> items;
  int selected = 0;
  bool showingStats = false;
  HangmanPreferences preferences;
  HangmanStats stats;
  ButtonNavigator buttonNavigator;

  void buildItems();
  void activateSelected();
  void renderMenu();
  void renderStats();
  const char* languageName() const;
  const char* categoryName() const;
  const char* difficultyName() const;
};
