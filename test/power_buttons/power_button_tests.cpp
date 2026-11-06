#include <cstdlib>
#include <iostream>

#include "ButtonEventManager.h"
#include "CrossPointSettings.h"
#include "PowerButtonPolicy.h"

#define CHECK(value)                             \
  do {                                           \
    if (!(value)) {                              \
      std::cerr << __LINE__ << ": " #value "\n"; \
      std::exit(1);                              \
    }                                            \
  } while (false)

CrossPointSettings CrossPointSettings::instance;
uint8_t MappedInputManager::rawIndex(Button b) const {
  if (b == Button::PageBack) b = Button::Up;
  if (b == Button::PageForward) b = Button::Down;
  return static_cast<uint8_t>(b);
}
bool MappedInputManager::isPressed(Button b) const { return gpio.held[rawIndex(b)]; }

using B = MappedInputManager::Button;
using T = ButtonEventManager::PressType;
using C = quick_wake::BootCause;
using M = quick_wake::MarkerMode;

struct Rig {
  HalGPIO gpio;
  MappedInputManager input{gpio};
  ButtonEventManager events{input};
  void edge(B b, bool down, unsigned long t) {
    testMillisValue = t;
    gpio.edge(input.rawIndex(b), down, t);
  }
  void tick(unsigned long t, bool fastBackExit = false) {
    testMillisValue = t;
    events.update(fastBackExit);
  }
  void expect(B b, T type) {
    ButtonEventManager::ButtonEvent e;
    CHECK(events.consumeEvent(e));
    CHECK(e.button == b);
    CHECK(e.type == type);
  }
  void empty() {
    ButtonEventManager::ButtonEvent e;
    CHECK(!events.consumeEvent(e));
  }
};

void defaultAndCustomPowerActions() {
  CHECK(CrossPointSettings::resolvedPowerAction(CrossPointSettings::BTN_DEFAULT) == CrossPointSettings::BTN_SLEEP);
  for (uint8_t a = 1; a < CrossPointSettings::BUTTON_ACTION_COUNT; ++a)
    CHECK(CrossPointSettings::resolvedPowerAction(a) == a);
  CHECK(CrossPointSettings::resolvedPowerAction(CrossPointSettings::BTN_IGNORE) == CrossPointSettings::BTN_IGNORE);
}

void queuedPowerPressUses400msAndOnlyOneAction() {
  Rig r;
  r.edge(B::Power, true, 100);
  r.tick(499);
  r.empty();
  r.tick(500);
  r.expect(B::Power, T::Long);
  r.empty();
  r.tick(1500);
  r.empty();
  r.edge(B::Power, false, 1600);
  r.tick(1600);
  r.empty();
  // A held press/release captured while loop was busy is still Long, not Short.
  r.edge(B::Power, true, 2000);
  r.edge(B::Power, false, 2450);
  r.tick(2500);
  r.expect(B::Power, T::Long);
  r.empty();
}

void shortPowerAndOtherButtonThresholds() {
  Rig r;
  r.edge(B::Power, true, 100);
  r.edge(B::Power, false, 200);
  r.tick(200);
  r.expect(B::Power, T::Short);
  r.empty();
  r.edge(B::Confirm, true, 1000);
  r.tick(1400);
  r.empty();
  r.tick(2000);
  r.expect(B::Confirm, T::Long);
  r.empty();
}

void browserBackUsesShorterHoldAndDoesNotRepeat() {
  Rig r;
  r.edge(B::Back, true, 100);
  r.tick(599, true);
  r.empty();
  r.tick(600, true);
  r.expect(B::Back, T::Long);
  r.edge(B::Back, false, 650);
  r.tick(650, true);
  r.empty();

  // A whole gesture can be queued while a browser operation is running.
  r.edge(B::Back, true, 1000);
  r.edge(B::Back, false, 1499);
  r.tick(1800, true);
  r.expect(B::Back, T::Short);
  r.edge(B::Back, true, 2000);
  r.edge(B::Back, false, 2500);
  r.tick(2700, true);
  r.expect(B::Back, T::Long);
  r.empty();

  r.edge(B::Back, true, 3000);
  r.tick(3500);
  r.empty();
  r.tick(4000);
  r.expect(B::Back, T::Long);
  r.empty();
}

void wakePressIsDiscardedUntilFreshPress() {
  Rig r;
  // main's stable-release service drains the same FSM after update. A wake
  // press may already have matured; neither Sleep nor a custom action escapes.
  r.edge(B::Power, true, 100);
  r.tick(1000);
  r.events.drain();
  r.empty();
  r.tick(3000);
  r.empty();
  r.edge(B::Power, false, 3100);
  r.tick(3100);
  r.events.drain();
  r.empty();
  r.edge(B::Power, true, 3300);
  r.tick(3700);
  r.expect(B::Power, T::Long);
  r.empty();
}

void screenshotDrainAndDoublePressRemainDistinct() {
  Rig r;
  r.edge(B::Power, true, 100);
  r.edge(B::Down, true, 110);
  r.tick(120);
  r.events.drain();  // POWER+DOWN screenshot consumes both buttons.
  r.edge(B::Power, false, 600);
  r.edge(B::Down, false, 610);
  r.tick(610);
  r.empty();
  SETTINGS.btnDoublePower = CrossPointSettings::BTN_GO_HOME;
  r.edge(B::Power, true, 1000);
  r.edge(B::Power, false, 1100);
  r.tick(1100);
  r.empty();
  CHECK(r.events.isShortPending(B::Power));  // background gauge work stays deferred in this window
  r.edge(B::Power, true, 1200);
  r.edge(B::Power, false, 1300);
  r.tick(1300);
  r.expect(B::Power, T::Double);
  r.empty();
  CHECK(!r.events.isShortPending(B::Power));
  SETTINGS.btnDoublePower = CrossPointSettings::BTN_DEFAULT;
}

void independentWakeCannotBeBypassedByQuickArtwork() {
  for (auto marker : {M::None, M::PressRequired, M::X4ColdReleasedTap}) {
    for (auto cause : {C::PowerOn, C::ButtonDeepSleep, C::Other})
      CHECK(!power_button::allowReleasedWake(CrossPointSettings::POWER_WAKE_LONG, marker, cause));
  }
  CHECK(power_button::shortWakeSelected(CrossPointSettings::POWER_WAKE_SHORT));
  CHECK(!power_button::shortWakeSelected(CrossPointSettings::POWER_WAKE_LONG));
  CHECK(power_button::markerForSleep(CrossPointSettings::POWER_WAKE_SHORT, true) == M::X4ColdReleasedTap);
  CHECK(power_button::markerForSleep(CrossPointSettings::POWER_WAKE_SHORT, false) == M::PressRequired);
  CHECK(power_button::markerForSleep(CrossPointSettings::POWER_WAKE_LONG, true) == M::None);
  CHECK(!power_button::allowReleasedWake(CrossPointSettings::POWER_WAKE_SHORT, M::PressRequired, C::PowerOn));
  CHECK(power_button::allowReleasedWake(CrossPointSettings::POWER_WAKE_SHORT, M::PressRequired, C::ButtonDeepSleep));
  CHECK(power_button::allowReleasedWake(CrossPointSettings::POWER_WAKE_SHORT, M::X4ColdReleasedTap, C::PowerOn));
}

void chargerGuardNeedsRealX3GateEvidence() {
  CHECK(power_button::observedPowerWake(true, C::PowerOn, true));
  CHECK(!power_button::observedPowerWake(true, C::PowerOn, false));
  CHECK(!power_button::observedPowerWake(true, C::Other, true));
  // An accepted X4 gate can be marker-only. Do not override charger filtering.
  CHECK(!power_button::observedPowerWake(false, C::PowerOn, true));
}

void earlyShortTapDoesNotHaveToSurviveNvsReads() {
  // The user releases while startup reads settings. Reuse the observed press,
  // not the current HIGH level, as evidence for the X3 PressRequired marker.
  constexpr bool observedBeforeNvs = true;
  constexpr bool stillPressedAfterNvs = false;
  const bool trusted = power_button::allowEarlyObservedWake(CrossPointSettings::POWER_WAKE_SHORT, M::PressRequired,
                                                            C::PowerOn, observedBeforeNvs);
  CHECK(!stillPressedAfterNvs && trusted);
  CHECK(quick_wake::acceptReleasedTap(true, trusted));
  for (auto marker : {M::None, M::PressRequired, M::X4ColdReleasedTap}) {
    for (auto cause : {C::PowerOn, C::ButtonDeepSleep, C::Other}) {
      // An explicit long gesture is never bypassed by a short physical tap.
      CHECK(!power_button::allowEarlyObservedWake(CrossPointSettings::POWER_WAKE_LONG, marker, cause, true));
      // A USB-only startup supplies no sampled physical press.
      CHECK(!power_button::allowEarlyObservedWake(CrossPointSettings::POWER_WAKE_SHORT, marker, cause, false));
    }
  }
  CHECK(!power_button::allowEarlyObservedWake(CrossPointSettings::POWER_WAKE_SHORT, M::None, C::PowerOn, true));
  CHECK(!power_button::allowEarlyObservedWake(CrossPointSettings::POWER_WAKE_SHORT, M::PressRequired, C::Other, true));
  CHECK(power_button::allowEarlyObservedWake(CrossPointSettings::POWER_WAKE_SHORT, M::PressRequired, C::ButtonDeepSleep,
                                             true));
}

int main() {
  testMillisStep = 0;
  defaultAndCustomPowerActions();
  queuedPowerPressUses400msAndOnlyOneAction();
  shortPowerAndOtherButtonThresholds();
  browserBackUsesShorterHoldAndDoesNotRepeat();
  wakePressIsDiscardedUntilFreshPress();
  screenshotDrainAndDoublePressRemainDistinct();
  independentWakeCannotBeBypassedByQuickArtwork();
  chargerGuardNeedsRealX3GateEvidence();
  earlyShortTapDoesNotHaveToSurviveNvsReads();
  std::cout << "9 button scenarios passed\n";
}
