#include "KakuroGameActivity.h"

#include <Arduino.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <memory>

#include "KakuroMenuActivity.h"
#include "KakuroStore.h"
#include "activities/games/GameUi.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr int kDigitFont = NOTOSANS_16_FONT_ID;
constexpr int kClueFont = UI_10_FONT_ID;
constexpr int kStatusFont = UI_12_FONT_ID;
constexpr int kMetaFont = UI_10_FONT_ID;

}  // namespace

KakuroGameActivity::KakuroGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       KakuroBoard::Difficulty difficulty, bool resume)
    : Activity("Kakuro", renderer, mappedInput), difficulty(difficulty), resumeRequested(resume) {}

void KakuroGameActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  state = State::Playing;
  focus = Focus::Grid;
  mistakes = 0;
  hintsLeft = 3;
  showErrors = false;
  elapsedMs = 0;
  saveDebouncer.clear();

  bool resumed = false;
  if (resumeRequested) {
    KakuroSaveSlot slot;
    if (KakuroStore::load(slot)) {
      board = slot.board;
      difficulty = slot.difficulty;
      variant = slot.variant;
      cursorRow = slot.cursorRow;
      cursorColumn = slot.cursorColumn;
      elapsedMs = static_cast<uint32_t>(slot.elapsedSec) * 1000u;
      mistakes = slot.mistakes;
      hintsLeft = slot.hintsLeft;
      showErrors = slot.showErrors;
      resumed = true;
    }
  }

  if (!resumed) {
    variant = static_cast<uint8_t>(esp_random() % 3u);
    if (!board.build(difficulty, variant)) LOG_ERR("KAK", "Invalid built-in puzzle");
    cursorRow = 1;
    cursorColumn = 1;
    KakuroStore::recordStart(difficulty);
  }
  lastTickMs = millis();
  requestUpdate();
}

void KakuroGameActivity::onExit() {
  flushSave();
  Activity::onExit();
}

void KakuroGameActivity::loop() {
  if (state == State::Playing) {
    const uint32_t now = millis();
    if (now > lastTickMs) elapsedMs += now - lastTickMs;
    lastTickMs = now;
    if (saveDebouncer.consumeIfDue(now)) flushSave();
    handlePlayingInput();
  } else if (state == State::GameMenu) {
    handleGameMenuInput();
  } else {
    handleWonInput();
  }
}

void KakuroGameActivity::handlePlayingInput() {
  if (focus == Focus::Grid) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      moveCursor(-1, 0);
      requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      moveCursor(1, 0);
      requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      moveCursor(0, -1);
      requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      moveCursor(0, 1);
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      enterPalette();
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = State::GameMenu;
      gameMenuIndex = 0;
      requestUpdate();
    }
  } else {
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      movePalette(-1, 0);
      requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      movePalette(1, 0);
      requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      movePalette(0, -1);
      requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      movePalette(0, 1);
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      leavePalette(true);
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      leavePalette(false);
      requestUpdate();
    }
  }
}

void KakuroGameActivity::handleGameMenuInput() {
  buttonNavigator.onPrevious([this] {
    gameMenuIndex = static_cast<uint8_t>((gameMenuIndex + MENU_ITEM_COUNT - 1) % MENU_ITEM_COUNT);
    requestUpdate();
  });
  buttonNavigator.onNext([this] {
    gameMenuIndex = static_cast<uint8_t>((gameMenuIndex + 1) % MENU_ITEM_COUNT);
    requestUpdate();
  });
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    runMenuItem(gameMenuIndex);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    resumeFromMenu();
    requestUpdate();
  }
}

void KakuroGameActivity::handleWonInput() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activityManager.replaceActivity(std::make_unique<KakuroGameActivity>(renderer, mappedInput, difficulty, false));
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.replaceActivity(std::make_unique<KakuroMenuActivity>(renderer, mappedInput));
  }
}

void KakuroGameActivity::moveCursor(int rowDelta, int columnDelta) {
  int row = cursorRow;
  int column = cursorColumn;
  for (uint8_t attempt = 0; attempt < KakuroBoard::MAX_CELLS; attempt++) {
    row = (row + rowDelta + board.rows) % board.rows;
    column = (column + columnDelta + board.columns) % board.columns;
    if (board.isPlayable(row, column)) {
      cursorRow = static_cast<uint8_t>(row);
      cursorColumn = static_cast<uint8_t>(column);
      return;
    }
  }
}

void KakuroGameActivity::movePalette(int rowDelta, int columnDelta) {
  int row = paletteIndex / 5;
  int column = paletteIndex % 5;
  row = (row + rowDelta + 2) % 2;
  column = (column + columnDelta + 5) % 5;
  paletteIndex = static_cast<uint8_t>(row * 5 + column);
}

void KakuroGameActivity::enterPalette() {
  focus = Focus::Palette;
  paletteIndex = board.value(cursorRow, cursorColumn) == 0 ? static_cast<uint8_t>(lastPlacedDigit - 1) : 9;
}

void KakuroGameActivity::leavePalette(bool place) {
  if (place) placeDigit(paletteIndex == 9 ? 0 : static_cast<uint8_t>(paletteIndex + 1));
  focus = Focus::Grid;
}

void KakuroGameActivity::placeDigit(uint8_t digit) {
  if (!board.place(cursorRow, cursorColumn, digit)) return;
  if (digit != 0) {
    lastPlacedDigit = digit;
    if (board.hasError(cursorRow, cursorColumn) && mistakes < 255) mistakes++;
  }
  scheduleSave();
  if (board.isSolved()) solve();
}

void KakuroGameActivity::useHint() {
  if (hintsLeft == 0) return;
  uint8_t targetRow = cursorRow;
  uint8_t targetColumn = cursorColumn;
  if (board.value(targetRow, targetColumn) != 0) {
    bool found = false;
    for (uint8_t row = 0; row < board.rows && !found; row++) {
      for (uint8_t column = 0; column < board.columns; column++) {
        if (board.isPlayable(row, column) && board.value(row, column) == 0) {
          targetRow = row;
          targetColumn = column;
          found = true;
          break;
        }
      }
    }
    if (!found) return;
  }
  const uint8_t digit = board.solution[KakuroBoard::idx(targetRow, targetColumn)];
  board.place(targetRow, targetColumn, digit);
  cursorRow = targetRow;
  cursorColumn = targetColumn;
  hintsLeft--;
  scheduleSave();
  if (board.isSolved()) solve();
}

void KakuroGameActivity::restart() {
  board.resetUser();
  cursorRow = 1;
  cursorColumn = 1;
  mistakes = 0;
  hintsLeft = 3;
  showErrors = false;
  elapsedMs = 0;
  focus = Focus::Grid;
  scheduleSave();
}

void KakuroGameActivity::solve() {
  const uint32_t seconds = elapsedMs / 1000u;
  KakuroStore::recordCompletion(difficulty, static_cast<uint16_t>(seconds > 0xFFFF ? 0xFFFF : seconds));
  KakuroStore::clear();
  saveDebouncer.clear();
  state = State::Won;
}

void KakuroGameActivity::runMenuItem(uint8_t index) {
  switch (index) {
    case 0:
      resumeFromMenu();
      break;
    case 1:
      useHint();
      if (state != State::Won) resumeFromMenu();
      break;
    case 2:
      showErrors = true;
      scheduleSave();
      resumeFromMenu();
      break;
    case 3:
      restart();
      resumeFromMenu();
      break;
    case 4:
      KakuroStore::clear();
      activityManager.replaceActivity(std::make_unique<KakuroGameActivity>(renderer, mappedInput, difficulty, false));
      break;
    case 5:
      flushSave();
      activityManager.replaceActivity(std::make_unique<KakuroMenuActivity>(renderer, mappedInput));
      break;
  }
}

void KakuroGameActivity::resumeFromMenu() {
  state = State::Playing;
  lastTickMs = millis();
}

void KakuroGameActivity::scheduleSave() { saveDebouncer.schedule(millis()); }

void KakuroGameActivity::flushSave() {
  if (state != State::Playing && state != State::GameMenu) return;
  saveDebouncer.clear();
  KakuroSaveSlot slot;
  slot.board = board;
  slot.difficulty = difficulty;
  slot.variant = variant;
  slot.elapsedSec = static_cast<uint16_t>((elapsedMs / 1000u) > 0xFFFF ? 0xFFFF : (elapsedMs / 1000u));
  slot.cursorRow = cursorRow;
  slot.cursorColumn = cursorColumn;
  slot.mistakes = mistakes;
  slot.hintsLeft = hintsLeft;
  slot.showErrors = showErrors;
  KakuroStore::save(slot);
}

const char* KakuroGameActivity::difficultyName(KakuroBoard::Difficulty difficulty) {
  if (difficulty == KakuroBoard::Difficulty::Medium) return tr(STR_SUDOKU_DIFFICULTY_MEDIUM);
  if (difficulty == KakuroBoard::Difficulty::Hard) return tr(STR_SUDOKU_DIFFICULTY_HARD);
  return tr(STR_SUDOKU_DIFFICULTY_EASY);
}

void KakuroGameActivity::render(RenderLock&&) {
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.clearScreen();
  if (state == State::Playing) {
    renderPlaying();
  } else if (state == State::GameMenu) {
    renderGameMenu();
  } else {
    renderWon();
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void KakuroGameActivity::renderPlaying() {
  drawTitleBar();
  drawGrid();
  drawPalette();
  drawFooter();
}

void KakuroGameActivity::drawTitleBar() {
  const int width = renderer.getScreenWidth();
  renderer.fillRect(0, TITLE_HEIGHT - 3, width, 3, true);
  char title[64];
  snprintf(title, sizeof(title), "%s · %s", tr(STR_KAKURO_TITLE), difficultyName(difficulty));
  renderer.drawText(kStatusFont, 12, 13, title, true, EpdFontFamily::BOLD);
  if (state != State::Won) {
    char elapsed[8];
    gameFormatElapsed(elapsedMs, elapsed, sizeof(elapsed));
    char meta[48];
    snprintf(meta, sizeof(meta), "%s · %s %u", elapsed, tr(STR_KAKURO_ERRORS), static_cast<unsigned>(mistakes));
    const int textWidth = renderer.getTextWidth(kMetaFont, meta);
    const int y = 13 + (renderer.getTextHeight(kStatusFont) - renderer.getTextHeight(kMetaFont)) / 2;
    renderer.drawText(kMetaFont, width - textWidth - 12, y, meta);
  }
}

void KakuroGameActivity::drawGrid() {
  const int maxWidth = 432;
  const int availableHeight = GRID_AREA_BOTTOM - TITLE_HEIGHT;
  int cellSize = maxWidth / board.columns;
  if (cellSize > availableHeight / board.rows) cellSize = availableHeight / board.rows;
  if (cellSize > 56) cellSize = 56;
  const int gridWidth = cellSize * board.columns;
  const int gridHeight = cellSize * board.rows;
  const int originX = (renderer.getScreenWidth() - gridWidth) / 2;
  const int originY = TITLE_HEIGHT + (availableHeight - gridHeight) / 2;
  const int digitHeight = renderer.getTextHeight(kDigitFont);

  for (uint8_t row = 0; row < board.rows; row++) {
    for (uint8_t column = 0; column < board.columns; column++) {
      const int x = originX + column * cellSize;
      const int y = originY + row * cellSize;
      const uint8_t cell = KakuroBoard::idx(row, column);
      if (!board.playable[cell]) {
        drawClueCell(x, y, cellSize, board.clueRight[cell], board.clueDown[cell]);
        continue;
      }

      const bool selected = focus == Focus::Grid && row == cursorRow && column == cursorColumn;
      if (selected) renderer.fillRect(x, y, cellSize, cellSize, true);
      renderer.drawRect(x, y, cellSize, cellSize, true);
      const uint8_t digit = board.user[cell];
      if (digit != 0) {
        const char text[2] = {static_cast<char>('0' + digit), 0};
        const int textWidth = renderer.getTextWidth(kDigitFont, text);
        renderer.drawText(kDigitFont, x + (cellSize - textWidth) / 2, y + gameCenterY(cellSize, digitHeight), text,
                          !selected, EpdFontFamily::ITALIC);
        if (showErrors && board.hasError(row, column)) {
          renderer.drawLine(x + cellSize - 10, y + cellSize - 9, x + cellSize - 4, y + cellSize - 3, !selected);
          renderer.drawLine(x + cellSize - 4, y + cellSize - 9, x + cellSize - 10, y + cellSize - 3, !selected);
        }
      }
    }
  }
  if (focus == Focus::Palette) {
    renderer.drawRect(originX + cursorColumn * cellSize + 2, originY + cursorRow * cellSize + 2, cellSize - 4,
                      cellSize - 4, 2, true);
  }
}

void KakuroGameActivity::drawClueCell(int x, int y, int size, uint8_t right, uint8_t down) {
  if (right == 0 && down == 0) {
    renderer.fillRect(x, y, size, size, true);
    return;
  }
  renderer.drawRect(x, y, size, size, true);
  renderer.drawLine(x + 2, y + size - 3, x + size - 3, y + 2, true);
  char text[4];
  if (down != 0) {
    snprintf(text, sizeof(text), "%u", static_cast<unsigned>(down));
    const int width = renderer.getTextWidth(kClueFont, text);
    renderer.drawText(kClueFont, x + size - width - 3, y + 2, text);
  }
  if (right != 0) {
    snprintf(text, sizeof(text), "%u", static_cast<unsigned>(right));
    renderer.drawText(kClueFont, x + 3, y + size - renderer.getTextHeight(kClueFont) - 2, text);
  }
}

void KakuroGameActivity::drawPalette() {
  const int originX = (renderer.getScreenWidth() - PALETTE_WIDTH) / 2;
  if (focus == Focus::Palette) {
    renderer.drawRect(originX - 4, PALETTE_Y - 4, PALETTE_WIDTH + 8, PALETTE_HEIGHT + 8, 2, true);
  }
  const int textHeight = renderer.getTextHeight(kDigitFont);
  for (uint8_t index = 0; index < 10; index++) {
    const int row = index / 5;
    const int column = index % 5;
    const int x = originX + column * (PALETTE_CELL_WIDTH + PALETTE_GAP);
    const int y = PALETTE_Y + row * (PALETTE_CELL_HEIGHT + PALETTE_GAP);
    const bool selected = focus == Focus::Palette && paletteIndex == index;
    if (selected) renderer.fillRect(x, y, PALETTE_CELL_WIDTH, PALETTE_CELL_HEIGHT, true);
    renderer.drawRect(x, y, PALETTE_CELL_WIDTH, PALETTE_CELL_HEIGHT, true);
    const char text[2] = {index < 9 ? static_cast<char>('1' + index) : 'X', 0};
    const int textWidth = renderer.getTextWidth(kDigitFont, text);
    renderer.drawText(kDigitFont, x + (PALETTE_CELL_WIDTH - textWidth) / 2,
                      y + gameCenterY(PALETTE_CELL_HEIGHT, textHeight), text, !selected);
  }
}

void KakuroGameActivity::drawFooter() {
  const char* back = focus == Focus::Grid ? tr(STR_KAKURO_MENU) : tr(STR_KAKURO_CANCEL);
  const char* confirm = focus == Focus::Grid ? tr(STR_KAKURO_PICK_NUMBER) : tr(STR_KAKURO_PLACE);
  const auto labels = mappedInput.mapLabels(back, confirm, tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void KakuroGameActivity::renderGameMenu() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_GAME_GAME_MENU));
  const int listY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listHeight = height - listY - metrics.buttonHintsHeight - metrics.verticalSpacing;
  constexpr StrId items[MENU_ITEM_COUNT] = {StrId::STR_GAME_RESUME,         StrId::STR_KAKURO_USE_HINT,
                                            StrId::STR_KAKURO_CHECK_ERRORS, StrId::STR_KAKURO_RESTART,
                                            StrId::STR_GAME_NEW_GAME,       StrId::STR_GAME_EXIT};
  GUI.drawList(renderer, Rect{0, listY, width, listHeight}, MENU_ITEM_COUNT, gameMenuIndex,
               [&](int index) { return std::string(I18N.get(items[index])); });
  const auto labels = mappedInput.mapLabels(tr(STR_GAME_RESUME), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void KakuroGameActivity::renderWon() {
  drawTitleBar();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int availableBottom = renderer.getScreenHeight() - metrics.buttonHintsHeight;
  char elapsed[8];
  gameFormatElapsed(elapsedMs, elapsed, sizeof(elapsed));
  const int difficultyIndex = static_cast<int>(difficulty);
  const KakuroStats stats = KakuroStore::loadStats();
  char subtitle[72];
  snprintf(subtitle, sizeof(subtitle), "%s · %s · %s %u", difficultyName(difficulty), elapsed, tr(STR_KAKURO_ERRORS),
           static_cast<unsigned>(mistakes));
  const int blockTop = gameCenteredBlockY(TITLE_HEIGHT, availableBottom, 250);
  renderer.drawCenteredText(NOTOSANS_16_FONT_ID, blockTop, tr(STR_KAKURO_SOLVED), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(kStatusFont, blockTop + 44, subtitle);
  renderer.drawLine(24, blockTop + 98, width - 24, blockTop + 98, true);

  char best[16];
  const uint16_t bestSeconds = stats.bestTimeSec[difficultyIndex];
  if (bestSeconds == 0) {
    snprintf(best, sizeof(best), "--");
  } else {
    snprintf(best, sizeof(best), "%02u:%02u", static_cast<unsigned>(bestSeconds / 60),
             static_cast<unsigned>(bestSeconds % 60));
  }
  char completed[16];
  snprintf(completed, sizeof(completed), "%u", static_cast<unsigned>(stats.completedCount[difficultyIndex]));
  renderer.drawCenteredText(kStatusFont, blockTop + 122, tr(STR_GAME_TIME));
  renderer.drawCenteredText(NOTOSANS_16_FONT_ID, blockTop + 150, elapsed);
  renderer.drawText(kStatusFont, 48, blockTop + 196, tr(STR_GAME_BEST_TIME));
  renderer.drawText(NOTOSANS_16_FONT_ID, 48, blockTop + 224, best);
  const int completedWidth = renderer.getTextWidth(kStatusFont, tr(STR_KAKURO_COMPLETED));
  renderer.drawText(kStatusFont, width - 48 - completedWidth, blockTop + 196, tr(STR_KAKURO_COMPLETED));
  const int totalWidth = renderer.getTextWidth(NOTOSANS_16_FONT_ID, completed);
  renderer.drawText(NOTOSANS_16_FONT_ID, width - 48 - totalWidth, blockTop + 224, completed);

  const auto labels = mappedInput.mapLabels(tr(STR_GAME_HOME), tr(STR_KAKURO_PLAY_AGAIN), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
