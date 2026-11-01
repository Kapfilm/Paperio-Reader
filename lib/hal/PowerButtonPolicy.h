#pragma once

#include <cstdint>

#include "QuickWakePolicy.h"

// Persisted wake mode is independent of button assignments and sleep artwork.
namespace power_button {
constexpr bool shortWakeSelected(uint8_t mode) { return mode != 1; }

constexpr bool allowReleasedWake(uint8_t mode, quick_wake::MarkerMode marker, quick_wake::BootCause cause) {
  return shortWakeSelected(mode) && quick_wake::allowReleasedWake(marker, cause);
}

// X3 must not infer a cold button wake from a sleep marker alone: USB can also
// power it on. A press actually sampled before NVS init supplies the missing
// evidence, so the user need not keep holding through those flash reads.
constexpr bool allowEarlyObservedWake(uint8_t mode, quick_wake::MarkerMode marker, quick_wake::BootCause cause,
                                      bool powerWasPressed) {
  return shortWakeSelected(mode) && quick_wake::armed(marker) && powerWasPressed &&
         (cause == quick_wake::BootCause::PowerOn || cause == quick_wake::BootCause::ButtonDeepSleep);
}

constexpr quick_wake::MarkerMode markerForSleep(uint8_t mode, bool knownX4) {
  return quick_wake::markerForSleep(shortWakeSelected(mode), knownX4);
}
// A real press accepted by the early gate remains evidence after slow board
// init, even if a USB-powered X3 later sees the already-released pin as HIGH.
constexpr bool observedPowerWake(bool knownX3, quick_wake::BootCause cause, bool earlyGateAccepted) {
  // X4 may accept a released cold tap by marker alone, so accepted is not
  // sufficient evidence to override the charger guard on that board.
  return knownX3 && cause == quick_wake::BootCause::PowerOn && earlyGateAccepted;
}
}  // namespace power_button
