#pragma once

#include <string>

#include "activities/MenuListActivity.h"

class ButtonSettingsBackupActivity final : public MenuListActivity {
 public:
  ButtonSettingsBackupActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void render(RenderLock&&) override;

 private:
  void onActionSelected(int index) override;
  void restoreFromFile();

  std::string status;
};
