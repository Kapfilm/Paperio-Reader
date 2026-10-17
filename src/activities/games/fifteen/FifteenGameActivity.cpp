#include "FifteenGameActivity.h"

#include <Arduino.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <memory>

#include "FifteenStore.h"
#include "FifteenMenuActivity.h"
#include "activities/games/GameUi.h"
#include "components/UITheme.h"
#include "fontIds.h"

void FifteenGameActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  if (startNew) {
    newGame();
  } else {
    restoreOrCreate();
  }
  requestUpdate();
}

void FifteenGameActivity::onExit() {
  if (state == State::Playing) saveGame();
  Activity::onExit();
}

void FifteenGameActivity::restoreOrCreate() {
  FifteenSaveSlot slot;
  if (!FifteenStore::load(slot)) {
    newGame();
    return;
  }
  tiles = slot.tiles;
  moves = slot.moves;
  const auto it = std::find(tiles.begin(), tiles.end(), 0);
  if (it == tiles.end()) {
    newGame();
    return;
  }
  blank = static_cast<uint8_t>(std::distance(tiles.begin(), it));
  state = State::Playing;
}

void FifteenGameActivity::newGame() {
  for (uint8_t i = 0; i < 15; ++i) tiles[i] = static_cast<uint8_t>(i + 1);
  tiles[15] = 0;
  blank = 15;

  int previousBlank = -1;
  for (int step = 0; step < 320; ++step) {
    const int row = blank / 4;
    const int column = blank % 4;
    uint8_t candidates[4] = {};
    uint8_t count = 0;
    if (row > 0 && blank - 4 != previousBlank) candidates[count++] = static_cast<uint8_t>(blank - 4);
    if (row < 3 && blank + 4 != previousBlank) candidates[count++] = static_cast<uint8_t>(blank + 4);
    if (column > 0 && blank - 1 != previousBlank) candidates[count++] = static_cast<uint8_t>(blank - 1);
    if (column < 3 && blank + 1 != previousBlank) candidates[count++] = static_cast<uint8_t>(blank + 1);
    if (count == 0) {
      previousBlank = -1;
      --step;
      continue;
    }
    const uint8_t next = candidates[esp_random() % count];
    previousBlank = blank;
    std::swap(tiles[blank], tiles[next]);
    blank = next;
  }
  if (isSolved()) {
    const uint8_t next = blank >= 4 ? static_cast<uint8_t>(blank - 4) : static_cast<uint8_t>(blank + 4);
    std::swap(tiles[blank], tiles[next]);
    blank = next;
  }
  moves = 0;
  state = State::Playing;
  saveGame();
  requestUpdate();
}

void FifteenGameActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.replaceActivity(std::make_unique<FifteenMenuActivity>(renderer, mappedInput));
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    newGame();
    return;
  }
  if (state != State::Playing) return;

  const int direction = controlMode == FifteenControlMode::Tiles ? -1 : 1;
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    moveBlank(-direction, 0);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    moveBlank(direction, 0);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    moveBlank(0, -direction);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    moveBlank(0, direction);
  }
}

void FifteenGameActivity::moveBlank(const int dr, const int dc) {
  const int row = blank / 4;
  const int column = blank % 4;
  const int nextRow = row + dr;
  const int nextColumn = column + dc;
  if (nextRow < 0 || nextRow >= 4 || nextColumn < 0 || nextColumn >= 4) return;
  const uint8_t next = static_cast<uint8_t>(nextRow * 4 + nextColumn);
  std::swap(tiles[blank], tiles[next]);
  blank = next;
  if (moves != UINT32_MAX) ++moves;
  if (isSolved()) {
    state = State::Won;
    FifteenStore::clear();
  } else {
    saveGame();
  }
  requestUpdate();
}

void FifteenGameActivity::saveGame() const { FifteenStore::save({tiles, moves}); }

bool FifteenGameActivity::isSolved() const {
  for (uint8_t i = 0; i < 15; ++i) {
    if (tiles[i] != i + 1) return false;
  }
  return tiles[15] == 0;
}

void FifteenGameActivity::render(RenderLock&&) {
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.clearScreen();
  if (state == State::Won) {
    renderWon();
  } else {
    renderPlaying();
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void FifteenGameActivity::renderPlaying() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_FIFTEEN_TITLE));

  char status[96];
  snprintf(status, sizeof(status), "%s: %lu · %s", tr(STR_FIFTEEN_MOVES), static_cast<unsigned long>(moves),
           controlMode == FifteenControlMode::Tiles ? tr(STR_FIFTEEN_CONTROL_TILES)
                                                    : tr(STR_FIFTEEN_CONTROL_EMPTY));
  const int subY = metrics.topPadding + metrics.headerHeight;
  GUI.drawSubHeader(renderer, Rect{0, subY, width, metrics.tabBarHeight}, status);

  constexpr int side = 24;
  constexpr int gap = 6;
  const int cell = (width - side * 2 - gap * 3) / 4;
  const int gridSize = cell * 4 + gap * 3;
  const int availableTop = subY + metrics.tabBarHeight + 18;
  const int availableBottom = height - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int gridY = gameCenteredBlockY(availableTop, availableBottom, gridSize);
  const int numberH = renderer.getTextHeight(NOTOSANS_16_FONT_ID);

  for (int index = 0; index < 16; ++index) {
    const uint8_t value = tiles[index];
    if (value == 0) continue;
    const int row = index / 4;
    const int column = index % 4;
    const int x = side + column * (cell + gap);
    const int y = gridY + row * (cell + gap);
    renderer.drawRect(x, y, cell, cell, 2, true);
    char label[4];
    snprintf(label, sizeof(label), "%u", static_cast<unsigned>(value));
    const int numberW = renderer.getTextWidth(NOTOSANS_16_FONT_ID, label, EpdFontFamily::BOLD);
    renderer.drawText(NOTOSANS_16_FONT_ID, x + (cell - numberW) / 2, y + gameCenterY(cell, numberH), label, true,
                      EpdFontFamily::BOLD);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_GAME_NEW_GAME), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void FifteenGameActivity::renderWon() {
  const int height = renderer.getScreenHeight();
  const int titleH = renderer.getTextHeight(NOTOSANS_16_FONT_ID);
  const int bodyH = renderer.getTextHeight(UI_12_FONT_ID);
  const int top = gameCenteredBlockY(0, height, titleH + bodyH + 26);
  renderer.drawCenteredText(NOTOSANS_16_FONT_ID, top, tr(STR_FIFTEEN_SOLVED), true, EpdFontFamily::BOLD);
  char status[64];
  snprintf(status, sizeof(status), "%s: %lu", tr(STR_FIFTEEN_MOVES), static_cast<unsigned long>(moves));
  renderer.drawCenteredText(UI_12_FONT_ID, top + titleH + 26, status);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_GAME_NEW_GAME), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
