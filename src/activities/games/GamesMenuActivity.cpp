#include "GamesMenuActivity.h"

#include <I18n.h>

#include "activities/games/fifteen/FifteenMenuActivity.h"
#include "activities/games/hangman/HangmanMenuActivity.h"
#include "activities/games/kakuro/KakuroMenuActivity.h"
#include "activities/games/sokoban/SokobanGameActivity.h"
#include "activities/games/sudoku/SudokuMenuActivity.h"
#include "components/UITheme.h"

namespace {
constexpr int kGameCount = 5;
}

void GamesMenuActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  selected = 0;
  requestUpdate();
}

void GamesMenuActivity::loop() {
  buttonNavigator.onNext([this] {
    selected = ButtonNavigator::nextIndex(selected, kGameCount);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selected = ButtonNavigator::previousIndex(selected, kGameCount);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    switch (selected) {
      case 0:
        activityManager.replaceActivity(std::make_unique<SudokuMenuActivity>(renderer, mappedInput));
        break;
      case 1:
        activityManager.replaceActivity(std::make_unique<SokobanGameActivity>(renderer, mappedInput));
        break;
      case 2:
        activityManager.replaceActivity(std::make_unique<KakuroMenuActivity>(renderer, mappedInput));
        break;
      case 3:
        activityManager.replaceActivity(std::make_unique<HangmanMenuActivity>(renderer, mappedInput));
        break;
      case 4:
        activityManager.replaceActivity(std::make_unique<FifteenMenuActivity>(renderer, mappedInput));
        break;
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
      renderer, Rect{0, listY, screenWidth, listH}, kGameCount, selected,
      [](int index) {
        switch (index) {
          case 0:
            return std::string(tr(STR_SUDOKU_TITLE));
          case 1:
            return std::string(tr(STR_SOKOBAN_TITLE));
          case 2:
            return std::string(tr(STR_KAKURO_TITLE));
          case 3:
            return std::string(tr(STR_HANGMAN_TITLE));
          default:
            return std::string(tr(STR_FIFTEEN_TITLE));
        }
      },
      [](int index) {
        switch (index) {
          case 0:
            return std::string(tr(STR_SUDOKU_SUBTITLE));
          case 1:
            return std::string(tr(STR_SOKOBAN_SUBTITLE));
          case 2:
            return std::string(tr(STR_KAKURO_SUBTITLE));
          case 3:
            return std::string(tr(STR_HANGMAN_SUBTITLE));
          default:
            return std::string(tr(STR_FIFTEEN_SUBTITLE));
        }
      });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
