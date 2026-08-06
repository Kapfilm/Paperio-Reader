#include "MappedInputManager.h"

#include "CrossPointSettings.h"

bool (*MappedInputManager::stripReversedPredicate)() = nullptr;

void MappedInputManager::setStripReversedPredicate(bool (*predicate)()) { stripReversedPredicate = predicate; }

bool MappedInputManager::isVerticalStripReversed() {
  return stripReversedPredicate != nullptr && stripReversedPredicate();
}

MappedInputManager::Button MappedInputManager::applyStripOrder(const Button button) {
  if (!isVerticalStripReversed()) {
    return button;
  }
  // Only the four front buttons sit on the reversed strip. Up/Down (and their
  // PageBack/PageForward aliases) are the side buttons on a different edge — their
  // physical order is unchanged by rotation, so they must not swap.
  switch (button) {
    case Button::Left:
      return Button::Right;
    case Button::Right:
      return Button::Left;
    default:
      return button;
  }
}

bool MappedInputManager::mapButton(const Button button, bool (HalGPIO::*fn)(uint8_t) const) const {
  switch (applyStripOrder(button)) {
    case Button::Back:
      return (gpio.*fn)(SETTINGS.frontButtonBack);
    case Button::Confirm:
      return (gpio.*fn)(SETTINGS.frontButtonConfirm);
    case Button::Left:
      return (gpio.*fn)(SETTINGS.frontButtonLeft);
    case Button::Right:
      return (gpio.*fn)(SETTINGS.frontButtonRight);
    case Button::Up:
      return (gpio.*fn)(HalGPIO::BTN_UP);
    case Button::Down:
      return (gpio.*fn)(HalGPIO::BTN_DOWN);
    case Button::Power:
      return (gpio.*fn)(HalGPIO::BTN_POWER);
    case Button::PageBack:
      return (gpio.*fn)(HalGPIO::BTN_UP);
    case Button::PageForward:
      return (gpio.*fn)(HalGPIO::BTN_DOWN);
  }
  return false;
}

uint8_t MappedInputManager::rawIndex(const Button button) const {
  switch (applyStripOrder(button)) {
    case Button::Back:
      return SETTINGS.frontButtonBack;
    case Button::Confirm:
      return SETTINGS.frontButtonConfirm;
    case Button::Left:
      return SETTINGS.frontButtonLeft;
    case Button::Right:
      return SETTINGS.frontButtonRight;
    case Button::Up:
      return HalGPIO::BTN_UP;
    case Button::Down:
      return HalGPIO::BTN_DOWN;
    case Button::Power:
      return HalGPIO::BTN_POWER;
    case Button::PageBack:
      return HalGPIO::BTN_UP;
    case Button::PageForward:
      return HalGPIO::BTN_DOWN;
  }
  return 0xFF;
}

bool MappedInputManager::wasPressed(const Button button) const { return mapButton(button, &HalGPIO::wasPressed); }

bool MappedInputManager::wasReleased(const Button button) const { return mapButton(button, &HalGPIO::wasReleased); }

bool MappedInputManager::isPressed(const Button button) const { return mapButton(button, &HalGPIO::isPressed); }

bool MappedInputManager::wasAnyPressed() const { return gpio.wasAnyPressed(); }

bool MappedInputManager::wasAnyReleased() const { return gpio.wasAnyReleased(); }

unsigned long MappedInputManager::getHeldTime() const { return gpio.getHeldTime(); }

MappedInputManager::Labels MappedInputManager::mapLabels(const char* back, const char* confirm, const char* previous,
                                                         const char* next) const {
  // Direction is already communicated by the physical button position. Remove the
  // legacy leading « decoration from every mapped hint, regardless of language or
  // which logical action the user assigned to the leftmost hardware button.
  const auto withoutLeadingArrow = [](const char* label) -> const char* {
    if (label == nullptr) return "";
    const auto* bytes = reinterpret_cast<const unsigned char*>(label);
    if (bytes[0] == 0xC2 && bytes[1] == 0xAB) {
      label += 2;
      while (*label == ' ') ++label;
    }
    return label;
  };

  // On a reversed strip the Left/Right roles are swapped (see applyStripOrder), so the
  // hint text has to follow — otherwise the label would contradict what the button does.
  const bool reversed = isVerticalStripReversed();
  const char* previousLabel = reversed ? next : previous;
  const char* nextLabel = reversed ? previous : next;

  // Build the label order based on the configured hardware mapping.
  auto labelForHardware = [&](uint8_t hw) -> const char* {
    // Compare against configured logical roles and return the matching label.
    if (hw == SETTINGS.frontButtonBack) {
      return withoutLeadingArrow(back);
    }
    if (hw == SETTINGS.frontButtonConfirm) {
      return withoutLeadingArrow(confirm);
    }
    if (hw == SETTINGS.frontButtonLeft) {
      return withoutLeadingArrow(previousLabel);
    }
    if (hw == SETTINGS.frontButtonRight) {
      return withoutLeadingArrow(nextLabel);
    }
    return "";
  };

  return {labelForHardware(HalGPIO::BTN_BACK), labelForHardware(HalGPIO::BTN_CONFIRM),
          labelForHardware(HalGPIO::BTN_LEFT), labelForHardware(HalGPIO::BTN_RIGHT)};
}

int MappedInputManager::getPressedFrontButton() const {
  // Scan the raw front buttons in hardware order.
  // This bypasses remapping so the remap activity can capture physical presses.
  if (gpio.wasPressed(HalGPIO::BTN_BACK)) {
    return HalGPIO::BTN_BACK;
  }
  if (gpio.wasPressed(HalGPIO::BTN_CONFIRM)) {
    return HalGPIO::BTN_CONFIRM;
  }
  if (gpio.wasPressed(HalGPIO::BTN_LEFT)) {
    return HalGPIO::BTN_LEFT;
  }
  if (gpio.wasPressed(HalGPIO::BTN_RIGHT)) {
    return HalGPIO::BTN_RIGHT;
  }
  return -1;
}
