#include "DictionarySelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

void DictionarySelectionActivity::onEnter() {
  Activity::onEnter();
  DictionaryRegistry::discover(dictionaries);
  selectedIndex = 0;  // Automatic.
  if (SETTINGS.dictionaryName[0] != '\0') {
    for (size_t i = 0; i < dictionaries.size(); ++i) {
      if (dictionaries[i].name == SETTINGS.dictionaryName) {
        selectedIndex = static_cast<int>(i + 1);
        break;
      }
    }
  }
  requestUpdate();
}

void DictionarySelectionActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedIndex == 0) {
      SETTINGS.dictionaryName[0] = '\0';
    } else {
      const std::string& name = dictionaries[static_cast<size_t>(selectedIndex - 1)].name;
      strncpy(SETTINGS.dictionaryName, name.c_str(), sizeof(SETTINGS.dictionaryName) - 1);
      SETTINGS.dictionaryName[sizeof(SETTINGS.dictionaryName) - 1] = '\0';
    }
    finish();
    return;
  }

  const int count = static_cast<int>(dictionaries.size()) + 1;
  buttonNavigator.onNextList(selectedIndex, count, [this] { requestUpdate(); });
  buttonNavigator.onPreviousList(selectedIndex, count, [this] { requestUpdate(); });
}

void DictionarySelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect content = UITheme::getContentRect(renderer, true, false);
  GUI.drawHeader(renderer, Rect{content.x, metrics.topPadding, content.width, metrics.headerHeight},
                 tr(STR_DICTIONARY));

  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int height = content.height - top - metrics.verticalSpacing;
  const int count = static_cast<int>(dictionaries.size()) + 1;
  const int active = [this] {
    if (SETTINGS.dictionaryName[0] == '\0') return 0;
    for (size_t i = 0; i < dictionaries.size(); ++i) {
      if (dictionaries[i].name == SETTINGS.dictionaryName) return static_cast<int>(i + 1);
    }
    return 0;
  }();

  GUI.drawList(
      renderer, Rect{content.x, top, content.width, height}, count, selectedIndex,
      [this](const int index) {
        return index == 0 ? std::string(tr(STR_DICT_AUTOMATIC)) : dictionaries[static_cast<size_t>(index - 1)].name;
      },
      nullptr, nullptr, [active](const int index) { return index == active ? std::string(tr(STR_SELECTED)) : ""; },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
