#include "GamesMenuActivity.h"

#include <I18n.h>
#include <algorithm>
#include <cstdio>

#include "activities/games/fifteen/FifteenMenuActivity.h"
#include "activities/games/pegsolitaire/PegSolitaireActivity.h"
#include "activities/games/delve/DelveGameActivity.h"
#include "activities/games/lamplighter/LamplighterActivity.h"
#include "activities/games/casefiles/CaseFilesActivity.h"
#include "activities/games/hangman/HangmanMenuActivity.h"
#include "activities/games/kakuro/KakuroMenuActivity.h"
#include "activities/games/sokoban/SokobanGameActivity.h"
#include "activities/games/sudoku/SudokuMenuActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "UiFonts.h"
#include "images/GameMenuIcons.h"

namespace {
constexpr int kGameCount = 9;
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
      case 5:
        activityManager.replaceActivity(std::make_unique<PegSolitaireActivity>(renderer, mappedInput));
        break;
      case 6:
        activityManager.replaceActivity(std::make_unique<DelveGameActivity>(renderer, mappedInput));
        break;
      case 7:
        activityManager.replaceActivity(std::make_unique<LamplighterActivity>(renderer, mappedInput));
        break;
      case 8:
        activityManager.replaceActivity(std::make_unique<CaseFilesActivity>(renderer, mappedInput));
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
  const int listY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listH = screenHeight - listY - metrics.buttonHintsHeight - metrics.verticalSpacing;
  constexpr StrId titles[kGameCount] = {StrId::STR_SUDOKU_TITLE, StrId::STR_SOKOBAN_TITLE, StrId::STR_KAKURO_TITLE,
                                        StrId::STR_HANGMAN_TITLE, StrId::STR_FIFTEEN_TITLE,
                                        StrId::STR_PEG_TITLE, StrId::STR_DELVE_TITLE,
                                        StrId::STR_LAMP_TITLE, StrId::STR_CASE_TITLE};
  constexpr StrId descriptions[kGameCount] = {
      StrId::STR_GAME_SUDOKU_DESCRIPTION, StrId::STR_GAME_SOKOBAN_DESCRIPTION,
      StrId::STR_GAME_KAKURO_DESCRIPTION, StrId::STR_GAME_HANGMAN_DESCRIPTION,
      StrId::STR_GAME_FIFTEEN_DESCRIPTION, StrId::STR_GAME_PEG_DESCRIPTION,
      StrId::STR_GAME_DELVE_DESCRIPTION, StrId::STR_GAME_LAMP_DESCRIPTION,
      StrId::STR_GAME_CASE_DESCRIPTION};
  constexpr int rowHeight = GAME_ICON_SIZE + 18;
  const int pageItems = std::max(1, listH / rowHeight);
  const int pageStart = (selected / pageItems) * pageItems;
  char pageLabel[20];
  snprintf(pageLabel, sizeof(pageLabel), "%d/%d", selected / pageItems + 1,
           (kGameCount + pageItems - 1) / pageItems);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, screenWidth, metrics.headerHeight}, tr(STR_GAMES), pageLabel);
  const int side = metrics.contentSidePadding;
  for (int i = pageStart; i < kGameCount && i < pageStart + pageItems; ++i) {
    const int y = listY + (i - pageStart) * rowHeight;
    const bool focused = i == selected;
    if (focused) renderer.fillRoundedRect(side, y, screenWidth - side * 2, rowHeight - 4, 10,
                                          Color::Black);
    const int iconX = side + 8;
    const int iconY = y + 7;
    // Use logical pixels so icons follow the reader orientation exactly.
    for (int iy = 0; iy < GAME_ICON_SIZE; ++iy) {
      for (int ix = 0; ix < GAME_ICON_SIZE; ++ix) {
        const auto bits = GAME_MENU_ICONS[i][iy * (GAME_ICON_SIZE / 8) + ix / 8];
        const bool ink = (bits & (0x80 >> (ix % 8))) == 0;
        // Match the row background, including the rounded icon corners.
        renderer.drawPixel(iconX + ix, iconY + iy, focused ? !ink : ink);
      }
    }
    const int textX = iconX + GAME_ICON_SIZE + 20;
    const int textWidth = screenWidth - textX - side - 12;
    const auto title = renderer.truncatedText(UI_12_FONT_ID, I18N.get(titles[i]), textWidth);
    renderer.drawText(UI_12_FONT_ID, textX, iconY, title.c_str(), !focused);
    int descriptionY = iconY + renderer.getTextHeight(UI_12_FONT_ID) + 5;
    const int lineHeight = renderer.getLineHeight(UI_HINT_FONT_ID);
    const int maxLines = std::max(0, (iconY + GAME_ICON_SIZE - descriptionY) / lineHeight);
    const auto description = renderer.wrappedText(UI_HINT_FONT_ID, I18N.get(descriptions[i]), textWidth, maxLines);
    for (const auto& line : description) {
      renderer.drawText(UI_HINT_FONT_ID, textX, descriptionY, line.c_str(), !focused);
      descriptionY += lineHeight;
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, /*equalWidths=*/true);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
