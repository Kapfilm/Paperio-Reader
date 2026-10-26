#include <cstdlib>
#include <iostream>

#include "QuickWakePolicy.h"

#define CHECK(value)                             \
  do {                                           \
    if (!(value)) {                              \
      std::cerr << __LINE__ << ": " #value "\n"; \
      std::exit(1);                              \
    }                                            \
  } while (false)

using quick_wake::BootCause;
using quick_wake::MarkerMode;

void normalDeepSleepNeverArmsQuickWake() {
  // Includes a manually requested deep sleep while timeout quick-resume is
  // enabled: the configured preference alone cannot classify that transition.
  CHECK(!quick_wake::isQuickSleep(false, false, false));
  CHECK(!quick_wake::isQuickSleep(false, true, false));
  CHECK(!quick_wake::isQuickSleep(false, false, true));
}

void actualQuickSleepModesArm() {
  CHECK(quick_wake::isQuickSleep(true, false, false));
  CHECK(quick_wake::isQuickSleep(true, true, false));
  CHECK(quick_wake::isQuickSleep(true, false, true));
  CHECK(quick_wake::isQuickSleep(true, true, true));
  CHECK(quick_wake::isQuickSleep(false, true, true));
}

void noMarkerPreservesTheNormalGestureGate() {
  for (auto cause : {BootCause::PowerOn, BootCause::ButtonDeepSleep, BootCause::Other}) {
    CHECK(!quick_wake::allowShortWake(MarkerMode::None, cause));
  }
}

void powerCutAndGpioQuickWakeAcceptShortPress() {
  CHECK(quick_wake::allowShortWake(MarkerMode::PressRequired, BootCause::PowerOn));
  CHECK(quick_wake::allowShortWake(MarkerMode::PressRequired, BootCause::ButtonDeepSleep));
}

void staleMarkerCannotShortWakeSoftwareOrCrashReset() {
  // Software restart, watchdog/panic, brownout and non-button wake sources are
  // classified Other by the adapter in setup(). They cannot borrow a stale arm.
  CHECK(!quick_wake::allowShortWake(MarkerMode::PressRequired, BootCause::Other));
}

void releasedTapRequiresBothShortPolicyAndTrustedQuickProvenance() {
  CHECK(quick_wake::acceptReleasedTap(true, true));
  CHECK(!quick_wake::acceptReleasedTap(false, true));
  CHECK(!quick_wake::acceptReleasedTap(true, false));
  CHECK(!quick_wake::acceptReleasedTap(false, false));
}

void ordinaryDeepSleepReplacesAnEarlierQuickMarker() {
  auto marker = quick_wake::markerForSleep(quick_wake::isQuickSleep(true, false, false), true);
  CHECK(quick_wake::allowShortWake(marker, BootCause::PowerOn));
  marker = quick_wake::markerForSleep(quick_wake::isQuickSleep(false, false, false), true);
  CHECK(!quick_wake::allowShortWake(marker, BootCause::PowerOn));
  CHECK(!quick_wake::acceptReleasedTap(false, false));
}

void x3ColdUsbWithoutHeldButtonCannotBecomeAQuickWake() {
  const auto mode = quick_wake::markerForSleep(true, false);
  CHECK(mode == MarkerMode::PressRequired);
  CHECK(quick_wake::allowShortWake(mode, BootCause::PowerOn));
  const bool trustedReleased = quick_wake::allowReleasedWake(mode, BootCause::PowerOn);
  CHECK(!trustedReleased);
  CHECK(!quick_wake::acceptReleasedTap(true, trustedReleased));
  // The same active-low button press is still short when observed held.
  // A real GPIO deep-sleep interrupt supplies independent button evidence.
  CHECK(quick_wake::allowReleasedWake(mode, BootCause::ButtonDeepSleep));
}

void knownX4ColdQuickWakeCanAcceptAnAlreadyReleasedTap() {
  const auto mode = quick_wake::markerForSleep(true, true);
  CHECK(mode == MarkerMode::X4ColdReleasedTap);
  CHECK(quick_wake::allowShortWake(mode, BootCause::PowerOn));
  CHECK(quick_wake::allowReleasedWake(mode, BootCause::PowerOn));
  CHECK(quick_wake::allowReleasedWake(mode, BootCause::ButtonDeepSleep));
  CHECK(!quick_wake::allowReleasedWake(mode, BootCause::Other));
  CHECK(quick_wake::markerForSleep(false, true) == MarkerMode::None);
  CHECK(quick_wake::markerForSleep(false, false) == MarkerMode::None);
}

void oldBoolAndInvalidStoredModesFailClosedForReleasedColdBoot() {
  CHECK(quick_wake::decodeMarker(0) == MarkerMode::None);
  CHECK(quick_wake::decodeMarker(1) == MarkerMode::PressRequired);
  CHECK(quick_wake::decodeMarker(2) == MarkerMode::X4ColdReleasedTap);
  CHECK(!quick_wake::allowReleasedWake(quick_wake::decodeMarker(1), BootCause::PowerOn));
  for (unsigned value = 3; value <= UINT8_MAX; ++value) {
    const auto mode = quick_wake::decodeMarker(static_cast<uint8_t>(value));
    CHECK(mode == MarkerMode::None);
    CHECK(!quick_wake::armed(mode));
    for (auto cause : {BootCause::PowerOn, BootCause::ButtonDeepSleep, BootCause::Other}) {
      CHECK(!quick_wake::allowShortWake(mode, cause));
      CHECK(!quick_wake::allowReleasedWake(mode, cause));
      CHECK(!quick_wake::allowShortWake(static_cast<MarkerMode>(value), cause));
      CHECK(!quick_wake::allowReleasedWake(static_cast<MarkerMode>(value), cause));
    }
  }
}

int main() {
  normalDeepSleepNeverArmsQuickWake();
  actualQuickSleepModesArm();
  noMarkerPreservesTheNormalGestureGate();
  powerCutAndGpioQuickWakeAcceptShortPress();
  staleMarkerCannotShortWakeSoftwareOrCrashReset();
  releasedTapRequiresBothShortPolicyAndTrustedQuickProvenance();
  ordinaryDeepSleepReplacesAnEarlierQuickMarker();
  x3ColdUsbWithoutHeldButtonCannotBecomeAQuickWake();
  knownX4ColdQuickWakeCanAcceptAnAlreadyReleasedTap();
  oldBoolAndInvalidStoredModesFailClosedForReleasedColdBoot();
  std::cout << "Quick wake: 10 policy scenarios passed\n";
}
