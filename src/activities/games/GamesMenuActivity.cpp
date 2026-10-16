#include "GamesMenuActivity.h"

#include <I18n.h>

#include "activities/games/sokoban/SokobanGameActivity.h"
#include "activities/games/sudoku/SudokuMenuActivity.h"
#include "components/UITheme.h"

void GamesMenuActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  selected = 0;
  requestUpdate();
}

void GamesMenuActivity::loop() {
  buttonNavigator.onNext([this] {
    selected = ButtonNavigator::nextIndex(selected, 2);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selected = ButtonNavigator::previousIndex(selected, 2);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selected == 0) {
      activityManager.replaceActivity(std::make_unique<SudokuMenuActivity>(renderer, mappedInput));
    } else {
      activityManager.replaceActivity(std::make_unique<SokobanGameActivity>(renderer, mappedInput));
    }
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.returnFromChild();
  }
}

void GamesMenuActivity::render(RenderLock&&) {
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, screenWidth, metrics.headerHeight}, tr(STR_GAMES));
  const int listY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listH = screenHeight - listY - metrics.buttonHintsHeight - metrics.verticalSpacing;
  GUI.drawList(
      renderer, Rect{0, listY, screenWidth, listH}, 2, selected,
      [](int index) { return std::string(index == 0 ? tr(STR_SUDOKU_TITLE) : tr(STR_SOKOBAN_TITLE)); },
      [](int index) { return std::string(index == 0 ? tr(STR_SUDOKU_SUBTITLE) : tr(STR_SOKOBAN_SUBTITLE)); });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
