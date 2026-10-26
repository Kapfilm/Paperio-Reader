#pragma once

#include <cstdint>

// The early wake gate runs before SD is mounted. Its quick-sleep evidence is
// a one-shot NVS marker written only when entering that actual sleep mode;
// merely having Quick Resume selected in settings must not bypass the gate.
namespace quick_wake {
enum class BootCause : uint8_t { PowerOn, ButtonDeepSleep, Other };
// Value 1 is compatible with the earlier bool marker, but deliberately does
// not authorize a released cold-boot tap. X3 cannot distinguish full-charge
// USB power-on from a released power-button boot by VBUS/current alone.
enum class MarkerMode : uint8_t { None = 0, PressRequired = 1, X4ColdReleasedTap = 2 };

constexpr MarkerMode decodeMarker(uint8_t stored) {
  return stored == 1 ? MarkerMode::PressRequired : stored == 2 ? MarkerMode::X4ColdReleasedTap : MarkerMode::None;
}

constexpr bool armed(MarkerMode mode) {
  return mode == MarkerMode::PressRequired || mode == MarkerMode::X4ColdReleasedTap;
}

constexpr bool isQuickSleep(bool quickScreen, bool fromTimeout, bool timeoutQuick) {
  return quickScreen || (fromTimeout && timeoutQuick);
}

constexpr MarkerMode markerForSleep(bool quickSleep, bool knownX4) {
  return !quickSleep ? MarkerMode::None : knownX4 ? MarkerMode::X4ColdReleasedTap : MarkerMode::PressRequired;
}

constexpr bool allowShortWake(MarkerMode mode, BootCause cause) {
  return armed(mode) && (cause == BootCause::PowerOn || cause == BootCause::ButtonDeepSleep);
}

constexpr bool allowReleasedWake(MarkerMode mode, BootCause cause) {
  return armed(mode) && (cause == BootCause::ButtonDeepSleep ||
                         (cause == BootCause::PowerOn && mode == MarkerMode::X4ColdReleasedTap));
}

// A very short wake tap may already have been released in ROM/bootloader.
// Accept that only with board-specific quick-sleep provenance and an eligible
// reset. X3 cold boots still require seeing the button held; GPIO deep-sleep
// wake has independent evidence that the button triggered it on either board.
constexpr bool acceptReleasedTap(bool shortAllowed, bool trustedQuickWake) { return shortAllowed && trustedQuickWake; }
}  // namespace quick_wake
