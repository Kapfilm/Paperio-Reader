#pragma once
#include <memory>

#include "SettingInfo.h"

class Activity;
class GfxRenderer;
class MappedInputManager;

// Creates the sub-activity corresponding to the given SettingAction.
// Returns nullptr for None, Submenu, or unknown actions (caller handles those).
std::unique_ptr<Activity> createActivityForAction(SettingAction action, GfxRenderer& renderer,
                                                  MappedInputManager& mappedInput);

// Creates the full-screen selector activity for a setting flagged with
// withSelectorActivity(). Returns the SD-card-aware FontSelectionActivity for the
// reader/TXT font-family settings, and the generic EnumSelectionActivity for any
// other ENUM setting. Callers own persistence in their result handler. Returns
// nullptr if the setting is not an ENUM (defensive; the flag is only set on enums).
std::unique_ptr<Activity> createSelectorActivity(const SettingInfo& setting, GfxRenderer& renderer,
                                                 MappedInputManager& mappedInput);
