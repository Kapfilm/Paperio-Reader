#pragma once

#include <cstdint>
#include <vector>

#include "KakuroBoard.h"
#include "KakuroStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class KakuroMenuActivity final : public Activity {
 public:
  KakuroMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~KakuroMenuActivity() override = default;

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class ItemKind : uint8_t { Continue, NewGame, Stats };
  struct Item {
    ItemKind kind;
    KakuroBoard::Difficulty difficulty;
  };

  ButtonNavigator buttonNavigator;
  std::vector<Item> items;
  int selected = 0;
  bool showingStats = false;
  KakuroStats stats;
  uint16_t resumeElapsedSec = 0;
  uint8_t resumeProgressPercent = 0;
  KakuroBoard::Difficulty resumeDifficulty = KakuroBoard::Difficulty::Easy;

  void buildItems();
  void selectItem();
  void renderList();
  void renderStats();
};
