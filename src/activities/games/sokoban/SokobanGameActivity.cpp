#include "SokobanGameActivity.h"

#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "SokobanStore.h"
#include "activities/games/GameUi.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int kMinCellSize = 8;
constexpr int kMaxCellSize = 48;

struct BoardLayout {
  int cellSize = 0;
  int x = 0;
  int y = 0;
  bool valid() const { return cellSize >= kMinCellSize; }
};

BoardLayout fitBoard(const Rect& rect, int rows, int cols) {
  if (rows <= 0 || cols <= 0 || rect.width <= 0 || rect.height <= 0) return {};
  int cell = std::min(rect.width / cols, rect.height / rows);
  cell = std::min(cell, kMaxCellSize);
  if (cell < kMinCellSize) return {};
  return {cell, rect.x + (rect.width - cols * cell) / 2, rect.y + (rect.height - rows * cell) / 2};
}
}  // namespace

void SokobanGameActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  size_t offset = 0;
  totalLevels = 0;
  while (offset + 2 <= kLevelsBinSize && totalLevels < MAX_LEVELS) {
    const size_t levelStart = offset;
    const uint8_t rows = kLevelsBin[offset++];
    const uint8_t cols = kLevelsBin[offset++];
    const size_t dataSize = static_cast<size_t>(rows) * cols;
    if (rows == 0 || cols == 0 || rows > SokobanBoard::MAX_ROWS || cols > SokobanBoard::MAX_COLS ||
        offset + dataSize > kLevelsBinSize) {
      break;
    }
    levelOffsets[totalLevels++] = levelStart;
    offset += dataSize;
  }
  if (totalLevels == 0) {
    LOG_ERR("SOK", "No valid built-in levels");
    activityManager.goToGames();
    return;
  }

  int savedLevel = 0;
  if (SokobanStore::loadLevel(savedLevel) && savedLevel >= 0 && savedLevel < totalLevels) {
    currentLevel = savedLevel;
  }
  loadLevel(currentLevel);
  requestUpdate();
}

void SokobanGameActivity::onExit() {
  SokobanStore::saveLevel(currentLevel);
  Activity::onExit();
}

void SokobanGameActivity::loadLevel(int index) {
  if (index < 0 || index >= totalLevels) index = 0;
  const size_t offset = levelOffsets[index];
  const uint8_t rows = kLevelsBin[offset];
  const uint8_t cols = kLevelsBin[offset + 1];
  const size_t dataSize = static_cast<size_t>(rows) * cols;
  if (offset + 2 + dataSize > kLevelsBinSize) {
    LOG_ERR("SOK", "Invalid level %d", index + 1);
    activityManager.goToGames();
    return;
  }
  board.clear();
  board.rows = rows;
  board.cols = cols;
  const uint8_t* data = kLevelsBin + offset + 2;
  bool foundPlayer = false;
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      board.cells[r][c] = static_cast<SokobanBoard::Cell>(data[r * cols + c]);
      if (board.cells[r][c] == SokobanBoard::PLAYER || board.cells[r][c] == SokobanBoard::PLAYER_ON_TARGET) {
        board.playerR = r;
        board.playerC = c;
        foundPlayer = true;
      }
    }
  }
  if (!foundPlayer) {
    LOG_ERR("SOK", "Level %d has no player", index + 1);
    activityManager.goToGames();
    return;
  }
  currentLevel = index;
  selectedLevel = index;
  moves = 0;
  state = State::Playing;
  SokobanStore::saveLevel(currentLevel);
}

void SokobanGameActivity::loop() {
  if (state == State::Won) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      loadLevel((currentLevel + 1) % totalLevels);
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      activityManager.goToGames();
    }
    return;
  }

  if (state == State::LevelSelect) {
    buttonNavigator.onNext([this] {
      selectedLevel = ButtonNavigator::nextIndex(selectedLevel, totalLevels);
      requestUpdate();
    });
    buttonNavigator.onPrevious([this] {
      selectedLevel = ButtonNavigator::previousIndex(selectedLevel, totalLevels);
      requestUpdate();
    });
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      loadLevel(selectedLevel);
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = State::Playing;
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    move(-1, 0);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    move(1, 0);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    move(0, -1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    move(0, 1);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Power)) {
    undo();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    selectedLevel = currentLevel;
    state = State::LevelSelect;
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToGames();
  }
}

void SokobanGameActivity::move(int dr, int dc) {
  if (!board.movePlayer(dr, dc)) return;
  moves++;
  if (board.isWin()) state = State::Won;
  requestUpdate();
}

void SokobanGameActivity::undo() {
  if (!board.canUndo()) return;
  board.undo();
  if (moves > 0) moves--;
  requestUpdate();
}

void SokobanGameActivity::render(RenderLock&&) {
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.clearScreen();
  if (state == State::Won) {
    drawWon();
  } else if (state == State::LevelSelect) {
    drawLevelSelect();
  } else {
    drawPlaying();
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void SokobanGameActivity::drawPlaying() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_SOKOBAN_TITLE));

  char status[96];
  snprintf(status, sizeof(status), "%s %d/%d · %s: %d · %s: %d", tr(STR_SOKOBAN_LEVEL), currentLevel + 1, totalLevels,
           tr(STR_SOKOBAN_MOVES), moves, tr(STR_SOKOBAN_PUSHES), board.pushes);
  const int subY = metrics.topPadding + metrics.headerHeight;
  GUI.drawSubHeader(renderer, Rect{0, subY, width, metrics.tabBarHeight}, status);

  const Rect contentRect = UITheme::getContentRect(renderer, true, false);
  const int boardTop = subY + metrics.tabBarHeight + metrics.verticalSpacing;
  const int boardBottom = contentRect.y + contentRect.height - metrics.verticalSpacing;
  drawBoard(Rect{metrics.contentSidePadding, boardTop, width - metrics.contentSidePadding * 2,
                 std::max(0, boardBottom - boardTop)});

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SOKOBAN_LEVEL), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void SokobanGameActivity::drawBoard(const Rect& content) {
  const BoardLayout layout = fitBoard(content, board.rows, board.cols);
  if (!layout.valid()) return;

  const int cell = layout.cellSize;
  for (int r = 0; r < board.rows; ++r) {
    for (int c = 0; c < board.cols; ++c) {
      const int x = layout.x + c * cell;
      const int y = layout.y + r * cell;
      switch (board.cells[r][c]) {
        case SokobanBoard::EMPTY:
        case SokobanBoard::FLOOR:
          break;
        case SokobanBoard::WALL:
          renderer.drawRect(x, y, cell, cell, cell >= 16 ? 2 : 1, true);
          break;
        case SokobanBoard::TARGET:
          renderer.drawRect(x + cell / 3, y + cell / 3, std::max(2, cell / 3), std::max(2, cell / 3), 1, true);
          break;
        case SokobanBoard::BOX:
          renderer.drawRect(x + 2, y + 2, cell - 4, cell - 4, cell >= 16 ? 2 : 1, true);
          renderer.drawLine(x + 3, y + 3, x + cell - 4, y + cell - 4, true);
          renderer.drawLine(x + cell - 4, y + 3, x + 3, y + cell - 4, true);
          break;
        case SokobanBoard::BOX_ON_TARGET:
          renderer.fillRect(x + 2, y + 2, cell - 4, cell - 4, true);
          renderer.drawRect(x + cell / 3, y + cell / 3, std::max(2, cell / 3), std::max(2, cell / 3), 1, false);
          break;
        case SokobanBoard::PLAYER:
          renderer.fillRect(x + cell / 4, y + cell / 4, std::max(2, cell / 2), std::max(2, cell / 2), true);
          break;
        case SokobanBoard::PLAYER_ON_TARGET:
          renderer.fillRect(x + cell / 4, y + cell / 4, std::max(2, cell / 2), std::max(2, cell / 2), true);
          renderer.drawRect(x + cell / 3, y + cell / 3, std::max(2, cell / 3), std::max(2, cell / 3), 1, false);
          break;
      }
    }
  }
}

void SokobanGameActivity::drawWon() {
  const int height = renderer.getScreenHeight();
  const int titleH = renderer.getTextHeight(NOTOSANS_14_FONT_ID);
  const int bodyH = renderer.getTextHeight(UI_12_FONT_ID);
  constexpr int gap = 14;
  const int y = gameCenteredBlockY(0, height, titleH + bodyH * 2 + gap * 2);

  char result[80];
  snprintf(result, sizeof(result), "%s: %d · %s: %d", tr(STR_SOKOBAN_MOVES), moves, tr(STR_SOKOBAN_PUSHES),
           board.pushes);
  renderer.drawCenteredText(NOTOSANS_14_FONT_ID, y, tr(STR_SOKOBAN_WIN));
  renderer.drawCenteredText(UI_12_FONT_ID, y + titleH + gap, result);
  renderer.drawCenteredText(UI_12_FONT_ID, y + titleH + bodyH + gap * 2, tr(STR_SOKOBAN_WIN_HINT));

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void SokobanGameActivity::drawLevelSelect() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_SOKOBAN_LEVEL));

  const int listY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listH = height - listY - metrics.buttonHintsHeight - metrics.verticalSpacing;
  GUI.drawList(renderer, Rect{0, listY, width, listH}, totalLevels, selectedLevel, [](int index) {
    char label[32];
    snprintf(label, sizeof(label), "%s %d", tr(STR_SOKOBAN_LEVEL), index + 1);
    return std::string(label);
  });
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
