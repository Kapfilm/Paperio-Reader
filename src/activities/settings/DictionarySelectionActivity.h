#pragma once

#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/DictionaryRegistry.h"

class DictionarySelectionActivity final : public Activity {
 public:
  DictionarySelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("DictionarySelection", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::vector<DictionaryEntry> dictionaries;
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
};
