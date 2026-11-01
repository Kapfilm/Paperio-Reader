#include "EnumSelectionActivity.h"

#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

uint16_t EnumSelectionActivity::optionCount() const {
  return overrideCount > 0 ? overrideCount : setting.getEnumOptionCount();
}

std::string EnumSelectionActivity::optionLabel(uint8_t index) const {
  if (labelOverride) {
    std::string label = labelOverride(index);
    if (!label.empty()) return label;
  }
  return setting.getEnumOptionLabel(index);
}

void EnumSelectionActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = static_cast<int>(setting.getEnumSelectedIndex());
  const int count = static_cast<int>(optionCount());
  if (selectedIndex >= count) selectedIndex = 0;  // clamp stale/out-of-range persisted value
  requestUpdate();
}

void EnumSelectionActivity::onExit() { Activity::onExit(); }

void EnumSelectionActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  const int count = static_cast<int>(optionCount());
  buttonNavigator.onNextList(selectedIndex, count, [this] { requestUpdate(); });
  buttonNavigator.onPreviousList(selectedIndex, count, [this] { requestUpdate(); });
}

void EnumSelectionActivity::handleSelection() {
  setting.setEnumSelectedIndex(static_cast<uint8_t>(selectedIndex));
  finish();
}

void EnumSelectionActivity::render(RenderLock&&) {
  const int panelTop = overlayPanel ? renderer.getScreenHeight() / 4 : 0;
  if (overlayPanel) {
    renderer.syncWriteBufferFromDisplayed();
    renderer.fillRoundedRect(0, panelTop, renderer.getScreenWidth(), renderer.getScreenHeight() - panelTop, 12, true,
                             true, false, false, Color::White);
    renderer.drawRoundedRect(0, panelTop, renderer.getScreenWidth(), renderer.getScreenHeight() - panelTop, 2, 12, true,
                             true, false, false, true);
  } else {
    renderer.clearScreen();
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  Rect contentRect = UITheme::getContentRect(renderer, true, false);
  if (overlayPanel) {
    const int contentBottom = contentRect.y + contentRect.height;
    contentRect.y = std::max(contentRect.y, panelTop + 2);
    contentRect.height = std::max(0, contentBottom - contentRect.y);
  }

  GUI.drawHeader(renderer,
                 Rect{contentRect.x, contentRect.y + metrics.topPadding, contentRect.width, metrics.headerHeight},
                 I18N.get(setting.nameId));

  const int contentTop = contentRect.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  int contentHeight = contentRect.height - (contentTop - contentRect.y) - metrics.verticalSpacing;

  // Keep the warning on the same screen as the candidate action. It is advisory:
  // no assignment is blocked and selecting the action still needs one Confirm.
  const bool warnSleepRemap =
      setting.valuePtr == &CrossPointSettings::btnLongPower &&
      CrossPointSettings::resolvedPowerAction(static_cast<uint8_t>(selectedIndex)) != CrossPointSettings::BTN_SLEEP;
  if (warnSleepRemap) {
    const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID) + 4;
    const auto warning = renderer.wrappedText(SMALL_FONT_ID, tr(STR_POWER_SLEEP_REMAP_WARNING),
                                              contentRect.width - 2 * metrics.contentSidePadding, 4);
    const int warningHeight = static_cast<int>(warning.size()) * lineHeight + metrics.verticalSpacing;
    contentHeight = std::max(0, contentHeight - warningHeight);
    int y = contentTop + contentHeight + metrics.verticalSpacing;
    for (const auto& line : warning) {
      renderer.drawText(SMALL_FONT_ID, contentRect.x + metrics.contentSidePadding, y, line.c_str());
      y += lineHeight;
    }
  }

  const int count = static_cast<int>(optionCount());
  const uint8_t activeIndex = setting.getEnumSelectedIndex();
  GUI.drawList(
      renderer, Rect{contentRect.x, contentTop, contentRect.width, contentHeight}, count, selectedIndex,
      [this](int index) { return optionLabel(static_cast<uint8_t>(index)); }, nullptr, nullptr,
      [activeIndex](int index) -> std::string {
        return index == static_cast<int>(activeIndex) ? tr(STR_SELECTED) : "";
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (overlayPanel) {
    renderer.displayWindow(0, panelTop, renderer.getScreenWidth(), renderer.getScreenHeight() - panelTop);
  } else {
    renderer.displayBuffer();
  }
}
