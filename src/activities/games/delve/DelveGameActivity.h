#pragma once
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "DelveModel.h"
#include <vector>

class DelveGameActivity final : public Activity {
 public:
  DelveGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Delve", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool shouldSkipPeriodicUpdate() const override { return true; }
 private:
  enum class Screen { Title, Playing, Pause, Bag, BagAction, Settings, Help, ConfirmNew, Inspect, End };
  delve::Game game;
  Screen screen = Screen::Title, helpReturn = Screen::Title;
  ButtonNavigator navigator;
  Screen settingsReturn = Screen::Title;
  bool largeView = false, viewSaved = true;
  void loadView();
  void saveView();
  bool haveRun = false, savedAvailable = false, newDaily = false, saveOk = true;
  uint32_t sequence = 0, runs = 0, wins = 0, deepest = 0;
  int selected = 0, helpPage = 0, bagIndex = 0;
  void load();
  void save();
  void newGame(bool daily);
  void changed();
  void activate();
  void drawGame();
  void drawListScreen(const char* title, const std::vector<std::string>& items, const char* subtitle = nullptr);
  void drawHelp();
  void drawInspect();
  void hints(const char* back, const char* confirm, const char* up, const char* down);
  std::string itemName(const delve::Item& item) const;
  std::string messageText(const delve::Message& message) const;
  const char* monsterName(delve::Kind kind) const;
};
