#include "SliderPickerActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int kSmallStep = 1;
constexpr int kLargeStep = 10;
}  // namespace

void SliderPickerActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void SliderPickerActivity::onExit() { Activity::onExit(); }

void SliderPickerActivity::adjustValue(const int delta) {
  if (cfg.firstNumericValue > cfg.minValue) {
    if (value == cfg.minValue && delta > 0) {
      value = cfg.firstNumericValue;
      requestUpdate();
      return;
    }
    if (value == cfg.firstNumericValue && delta < 0) {
      value = cfg.minValue;
      requestUpdate();
      return;
    }
  }
  value += delta;
  if (value < cfg.minValue) value = cfg.minValue;
  if (value > cfg.maxValue) value = cfg.maxValue;
  requestUpdate();
}

void SliderPickerActivity::loop() {
  ButtonEventManager::ButtonEvent ev;
  while (buttonEvents.consumeEvent(ev)) {
    if (ev.button == MappedInputManager::Button::Back && ev.type == ButtonEventManager::PressType::Short) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
      return;
    }

    if (ev.button == MappedInputManager::Button::Confirm && ev.type == ButtonEventManager::PressType::Short) {
      setResult(PercentResult{value});
      finish();
      return;
    }

    if (ev.button == MappedInputManager::Button::Left && ev.type == ButtonEventManager::PressType::Short) {
      adjustValue(-kSmallStep);
      return;
    }

    if (ev.button == MappedInputManager::Button::Right && ev.type == ButtonEventManager::PressType::Short) {
      adjustValue(kSmallStep);
      return;
    }

    // The side buttons are represented twice by ButtonEventManager (Up/PageBack and
    // Down/PageForward). Handle only Up/Down so one physical tap applies one large step.
    if (ev.button == MappedInputManager::Button::Up && ev.type == ButtonEventManager::PressType::Short) {
      adjustValue(kLargeStep);
      return;
    }
    if (ev.button == MappedInputManager::Button::Down && ev.type == ButtonEventManager::PressType::Short) {
      adjustValue(-kLargeStep);
      return;
    }
  }

  buttonNavigator.onContinuous({MappedInputManager::Button::Up}, [this] { adjustValue(kLargeStep); });
  buttonNavigator.onContinuous({MappedInputManager::Button::Down}, [this] { adjustValue(-kLargeStep); });
}

void SliderPickerActivity::render(RenderLock&&) {
  renderer.clearScreen();

  renderer.drawCenteredText(UI_12_FONT_ID, 15, I18N.get(cfg.titleId), true, EpdFontFamily::BOLD);

  std::string valueText;
  if (!cfg.zeroLabel.empty() && value == cfg.minValue) {
    valueText = cfg.zeroLabel;
  } else {
    valueText = std::to_string(value) + cfg.suffix;
  }
  renderer.drawCenteredText(UI_12_FONT_ID, 90, valueText.c_str(), true, EpdFontFamily::BOLD);

  const Rect contentRect = UITheme::getContentRect(renderer, true, false);
  constexpr int barWidth = 360;
  constexpr int barHeight = 16;
  const int barX = contentRect.x + (contentRect.width - barWidth) / 2;
  constexpr int barY = 140;

  renderer.drawRect(barX, barY, barWidth, barHeight);

  const int range = cfg.maxValue - cfg.minValue;
  const int fillWidth = range > 0 ? (barWidth - 4) * (value - cfg.minValue) / range : 0;
  if (fillWidth > 0) {
    renderer.fillRect(barX + 2, barY + 2, fillWidth, barHeight - 4);
  }

  const int knobX = barX + 2 + fillWidth - 2;
  renderer.fillRect(knobX, barY - 4, 4, barHeight + 8, true);

  if (cfg.showButtonStepHints) {
    char hint[64];
    snprintf(hint, sizeof(hint), "%s: %d%%", tr(STR_STEP_HINT_FRONT), kSmallStep);
    renderer.drawCenteredText(SMALL_FONT_ID, barY + 30, hint, true);
    snprintf(hint, sizeof(hint), "%s: %d%%", tr(STR_STEP_HINT_SIDE), kLargeStep);
    renderer.drawCenteredText(SMALL_FONT_ID, barY + 52, hint, true);
  } else {
    renderer.drawCenteredText(SMALL_FONT_ID, barY + 30, I18N.get(cfg.hintId), true);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "-", "+");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
