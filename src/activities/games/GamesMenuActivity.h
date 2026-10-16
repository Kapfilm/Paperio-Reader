#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class GamesMenuActivity final : public Activity {
 public:
  GamesMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("GamesMenu", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  int selected = 0;
  ButtonNavigator buttonNavigator;
};
