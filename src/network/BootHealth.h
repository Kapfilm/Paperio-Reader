#pragma once
#include <esp_partition.h>

namespace boot_health {
void begin();
void rendered();
void tick(bool renderBusy);
bool recoveryRequired();
// Called only after a validated update has been fully written, before selecting it.
bool stageUpdate(const esp_partition_t* target);
// Explicit user-requested restart/sleep only; never called from panic paths.
void preparePlannedReset();
}
