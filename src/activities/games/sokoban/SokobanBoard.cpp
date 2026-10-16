#include "SokobanBoard.h"

#include <cstring>

SokobanBoard::SokobanBoard() : history{} { clear(); }

void SokobanBoard::clear() {
  memset(cells, 0, sizeof(cells));
  rows = 0;
  cols = 0;
  playerR = 0;
  playerC = 0;
  pushes = 0;
  historyCount = 0;
}

bool SokobanBoard::loadFromStrings(const char* const* levelData, int numRows) {
  clear();
  if (numRows <= 0 || numRows > MAX_ROWS) return false;

  rows = numRows;
  bool foundPlayer = false;
  for (int r = 0; r < rows; ++r) {
    const char* line = levelData[r];
    const int width = static_cast<int>(strlen(line));
    if (width > MAX_COLS) return false;
    if (width > cols) cols = width;
    for (int c = 0; c < width; ++c) {
      switch (line[c]) {
        case '#':
          cells[r][c] = WALL;
          break;
        case '.':
          cells[r][c] = TARGET;
          break;
        case '$':
          cells[r][c] = BOX;
          break;
        case '*':
          cells[r][c] = BOX_ON_TARGET;
          break;
        case '@':
          cells[r][c] = PLAYER;
          playerR = r;
          playerC = c;
          foundPlayer = true;
          break;
        case '+':
          cells[r][c] = PLAYER_ON_TARGET;
          playerR = r;
          playerC = c;
          foundPlayer = true;
          break;
        default:
          cells[r][c] = FLOOR;
          break;
      }
    }
  }
  return foundPlayer && cols > 0;
}

void SokobanBoard::pushHistory() {
  if (historyCount == HISTORY_MAX) {
    for (int i = 1; i < HISTORY_MAX; ++i) history[i - 1] = history[i];
    historyCount--;
  }
  HistoryEntry& entry = history[historyCount++];
  memcpy(entry.cells, cells, sizeof(cells));
  entry.playerR = playerR;
  entry.playerC = playerC;
  entry.pushes = pushes;
}

bool SokobanBoard::movePlayer(int dr, int dc) {
  const int nr = playerR + dr;
  const int nc = playerC + dc;
  if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) return false;

  const Cell target = cells[nr][nc];
  if (target == WALL || target == EMPTY) return false;

  int boxR = -1;
  int boxC = -1;
  if (target == BOX || target == BOX_ON_TARGET) {
    boxR = nr + dr;
    boxC = nc + dc;
    if (boxR < 0 || boxR >= rows || boxC < 0 || boxC >= cols) return false;
    const Cell behind = cells[boxR][boxC];
    if (behind != FLOOR && behind != TARGET) return false;
  }

  pushHistory();

  if (boxR >= 0) {
    cells[boxR][boxC] = cells[boxR][boxC] == TARGET ? BOX_ON_TARGET : BOX;
    cells[nr][nc] = target == BOX_ON_TARGET ? TARGET : FLOOR;
    pushes++;
  }

  cells[playerR][playerC] = cells[playerR][playerC] == PLAYER_ON_TARGET ? TARGET : FLOOR;
  cells[nr][nc] = cells[nr][nc] == TARGET ? PLAYER_ON_TARGET : PLAYER;
  playerR = nr;
  playerC = nc;
  return true;
}

bool SokobanBoard::isWin() const {
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      if (cells[r][c] == BOX) return false;
    }
  }
  return true;
}

void SokobanBoard::undo() {
  if (historyCount == 0) return;
  const HistoryEntry& entry = history[--historyCount];
  memcpy(cells, entry.cells, sizeof(cells));
  playerR = entry.playerR;
  playerC = entry.playerC;
  pushes = entry.pushes;
}

bool SokobanBoard::canUndo() const { return historyCount > 0; }
