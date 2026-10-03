#pragma once
#include <esp_partition.h>
#include "activities/Activity.h"

class SwitchFirmwareActivity final : public Activity {
 public:
  SwitchFirmwareActivity(GfxRenderer& renderer, MappedInputManager& input)
      : Activity("SwitchFirmware", renderer, input) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  bool preventAutoSleep() override { return true; }
 private:
  const esp_partition_t* target = nullptr;
  std::string detail;
  std::string error;
  bool ready = false;
  bool inputArmed = false;
  bool busy = true;
};
