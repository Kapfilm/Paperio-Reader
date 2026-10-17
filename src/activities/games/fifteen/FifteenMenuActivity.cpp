#include "FifteenMenuActivity.h"

#include <I18n.h>

#include <memory>

#include "FifteenGameActivity.h"
#include "components/UITheme.h"

void FifteenMenuActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  controlMode = FifteenStore::loadControlMode();
  selected = 0;
  buildItems();
  requestUpdate();
}

void FifteenMenuActivity::buildItems() {
  items.clear();
  if (FifteenStore::hasInProgress()) items.push_back(ItemKind::Continue);
  items.push_back(ItemKind::NewGame);
  items.push_back(ItemKind::Controls);
}

void FifteenMenuActivity::loop() {
  const int count = static_cast<int>(items.size());
  buttonNavigator.onNext([this, count] {
    selected = ButtonNavigator::nextIndex(selected, count);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, count] {
    selected = ButtonNavigator::previousIndex(selected, count);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelected();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToGames();
  }
}

void FifteenMenuActivity::activateSelected() {
  if (selected < 0 || selected >= static_cast<int>(items.size())) return;
  switch (items[selected]) {
    case ItemKind::Continue:
      activityManager.replaceActivity(
          std::make_unique<FifteenGameActivity>(renderer, mappedInput, controlMode, false));
      return;
    case ItemKind::NewGame:
      FifteenStore::clear();
      activityManager.replaceActivity(std::make_unique<FifteenGameActivity>(renderer, mappedInput, controlMode, true));
      return;
    case ItemKind::Controls:
      controlMode = controlMode == FifteenControlMode::EmptyCell ? FifteenControlMode::Tiles
                                                                 : FifteenControlMode::EmptyCell;
      FifteenStore::saveControlMode(controlMode);
      requestUpdate();
      return;
  }
}

const char* FifteenMenuActivity::controlModeName() const {
  return controlMode == FifteenControlMode::Tiles ? tr(STR_FIFTEEN_CONTROL_TILES)
                                                   : tr(STR_FIFTEEN_CONTROL_EMPTY);
}

const char* FifteenMenuActivity::controlModeDescription() const {
  return controlMode == FifteenControlMode::Tiles ? tr(STR_FIFTEEN_CONTROL_TILES_DESC)
                                                   : tr(STR_FIFTEEN_CONTROL_EMPTY_DESC);
}

void FifteenMenuActivity::render(RenderLock&&) {
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_FIFTEEN_TITLE));
  const int listY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listH = height - listY - metrics.buttonHintsHeight - metrics.verticalSpacing;

  GUI.drawList(
      renderer, Rect{0, listY, width, listH}, static_cast<int>(items.size()), selected,
      [this](const int index) {
        if (index < 0 || index >= static_cast<int>(items.size())) return std::string();
        switch (items[index]) {
          case ItemKind::Continue:
            return std::string(tr(STR_GAME_CONTINUE));
          case ItemKind::NewGame:
            return std::string(tr(STR_GAME_NEW_GAME));
          case ItemKind::Controls:
            return std::string(tr(STR_FIFTEEN_CONTROLS));
        }
        return std::string();
      },
      [this](const int index) {
        if (index < 0 || index >= static_cast<int>(items.size())) return std::string();
        switch (items[index]) {
          case ItemKind::Continue:
            return std::string(controlModeName());
          case ItemKind::NewGame:
            return std::string(tr(STR_FIFTEEN_NEW_GAME_DESC));
          case ItemKind::Controls:
            return std::string(controlModeDescription());
        }
        return std::string();
      });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
