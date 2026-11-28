#include "KakuroBoard.h"
#include "KakuroData.h"
#include <cstring>
#include <algorithm>

void KakuroBoard::resetUser() { memset(user, 0, sizeof(user)); memset(hinted, 0, sizeof(hinted)); historyCount = 0; }

bool KakuroBoard::isPlayable(uint8_t row, uint8_t column) const {
  return row < rows && column < columns && playable[idx(row, column)] != 0;
}

uint8_t KakuroBoard::value(uint8_t row, uint8_t column) const {
  return isPlayable(row, column) ? user[idx(row, column)] : 0;
}

bool KakuroBoard::place(uint8_t row, uint8_t column, uint8_t digit) {
  if (!isPlayable(row, column) || digit > 9 || isHinted(row, column)) return false;
  const auto cell = idx(row, column);
  if (user[cell] == digit) return false;
  if (historyCount == history.size()) {
    std::move(history.begin()+1, history.end(), history.begin()); --historyCount;
  }
  history[historyCount++] = {cell, user[cell]};
  user[cell] = digit;
  return true;
}

bool KakuroBoard::isSolved() const {
  for (uint8_t row = 0; row < rows; row++) {
    for (uint8_t column = 0; column < columns; column++) {
      const uint8_t cell = idx(row, column);
      if (playable[cell] && user[cell] != solution[cell]) return false;
    }
  }
  return true;
}

bool KakuroBoard::hasError(uint8_t row, uint8_t column) const {
  if (!isPlayable(row, column) || !value(row, column)) return false;
  for (const bool across : {true, false}) {
    const auto run = runFor(row, column, across);
    int sum = 0; bool full = true; int repeats = 0;
    for (int i = 0; i < run.count; ++i) {
      const auto digit = user[run.cells[i]];
      sum += digit; full &= digit != 0;
      repeats += digit == value(row, column);
    }
    if (repeats > 1 || sum > run.sum || (full && sum != run.sum)) return true;
  }
  return false;
}

bool KakuroBoard::validateDefinition() const {
  if (rows == 0 || rows > MAX_SIZE || columns == 0 || columns > MAX_SIZE) return false;
  for (uint8_t row = 0; row < rows; row++) {
    for (uint8_t column = 0; column < columns; column++) {
      const uint8_t cell = idx(row, column);
      if (!playable[cell]) continue;
      if (solution[cell] < 1 || solution[cell] > 9) return false;
    }
  }

  // Check every horizontal and vertical answer against its clue and the Kakuro
  // no-repeated-digits rule.
  for (uint8_t row = 0; row < rows; row++) {
    for (uint8_t column = 0; column < columns; column++) {
      const uint8_t clueCell = idx(row, column);
      if (clueRight[clueCell] != 0) {
        uint16_t seen = 0;
        uint8_t sum = 0;
        uint8_t count = 0;
        for (uint8_t c = column + 1; c < columns && playable[idx(row, c)]; c++) {
          const uint8_t digit = solution[idx(row, c)];
          if ((seen & (1u << digit)) != 0) return false;
          seen |= static_cast<uint16_t>(1u << digit);
          sum += digit;
          count++;
        }
        if (count < 2 || sum != clueRight[clueCell]) return false;
      }
      if (clueDown[clueCell] != 0) {
        uint16_t seen = 0;
        uint8_t sum = 0;
        uint8_t count = 0;
        for (uint8_t r = row + 1; r < rows && playable[idx(r, column)]; r++) {
          const uint8_t digit = solution[idx(r, column)];
          if ((seen & (1u << digit)) != 0) return false;
          seen |= static_cast<uint16_t>(1u << digit);
          sum += digit;
          count++;
        }
        if (count < 2 || sum != clueDown[clueCell]) return false;
      }
    }
  }
  return true;
}

uint8_t KakuroBoard::countPlayable() const {
  uint8_t count = 0;
  for (uint8_t row = 0; row < rows; row++) {
    for (uint8_t column = 0; column < columns; column++) count += playable[idx(row, column)] != 0;
  }
  return count;
}

uint8_t KakuroBoard::countFilled() const {
  uint8_t count = 0;
  for (uint8_t row = 0; row < rows; row++) {
    for (uint8_t column = 0; column < columns; column++) {
      const uint8_t cell = idx(row, column);
      count += playable[cell] && user[cell] != 0;
    }
  }
  return count;
}

bool KakuroBoard::writeUserTo(HalFile& file) const {
  return file.write(user, sizeof(user)) == sizeof(user) && file.write(hinted, sizeof(hinted)) == sizeof(hinted);
}

bool KakuroBoard::readUserFrom(HalFile& file) {
  uint8_t nextUser[MAX_CELLS], nextHints[MAX_CELLS];
  if (file.read(nextUser, sizeof(nextUser)) != sizeof(nextUser) ||
      file.read(nextHints, sizeof(nextHints)) != sizeof(nextHints)) return false;
  for (uint8_t cell = 0; cell < MAX_CELLS; ++cell) {
    if ((!playable[cell] && (nextUser[cell] || nextHints[cell])) || nextUser[cell] > 9 || nextHints[cell] > 1 ||
        (nextHints[cell] && nextUser[cell] != solution[cell])) return false;
  }
  memcpy(user, nextUser, sizeof(user)); memcpy(hinted, nextHints, sizeof(hinted)); historyCount = 0;
  return true;
}

bool KakuroBoard::build(Difficulty difficulty, uint8_t variant) {
  if (static_cast<unsigned>(difficulty) > 2 || variant >= PUZZLES_PER_LEVEL) return false;
  memset(playable, 0, sizeof(playable)); memset(solution, 0, sizeof(solution));
  memset(clueRight, 0, sizeof(clueRight)); memset(clueDown, 0, sizeof(clueDown)); resetUser();
  const int level = static_cast<int>(difficulty); rows = columns = 6 + level;
  const auto* packed = level == 0 ? kakuro_data::bank0[variant] : level == 1 ? kakuro_data::bank1[variant] : kakuro_data::bank2[variant];
  for (int r = 0; r < rows; ++r) for (int c = 0; c < columns; ++c) {
    const int linear = r * columns + c;
    const auto digit = (linear & 1) ? packed[linear / 2] & 15 : packed[linear / 2] >> 4;
    solution[idx(r,c)] = digit; playable[idx(r,c)] = digit != 0;
  }
  for (int r = 0; r < rows; ++r) for (int c = 0; c < columns; ++c) {
    if (isPlayable(r,c)) continue;
    for (int x = c + 1; x < columns && isPlayable(r,x); ++x) clueRight[idx(r,c)] += solution[idx(r,x)];
    for (int y = r + 1; y < rows && isPlayable(y,c); ++y) clueDown[idx(r,c)] += solution[idx(y,c)];
  }
  return validateDefinition();
}

KakuroBoard::Run KakuroBoard::runFor(uint8_t row, uint8_t column, bool across) const {
  Run out;
  if (!isPlayable(row, column)) return out;
  int r = row, c = column;
  if (across) { while (c > 0 && isPlayable(r, c - 1)) --c; }
  else { while (r > 0 && isPlayable(r - 1, c)) --r; }
  while (isPlayable(r,c) && out.count < out.cells.size()) {
    const auto cell = idx(r,c); out.cells[out.count++] = cell; out.sum += solution[cell];
    if (across) ++c; else ++r;
  }
  return out;
}

std::vector<std::string> KakuroBoard::combinations(uint8_t length, uint8_t sum) {
  std::vector<std::string> out;
  for (unsigned mask = 1; mask < 512; ++mask) {
    int count = 0, total = 0; std::string digits;
    for (int digit = 1; digit <= 9; ++digit) if (mask & (1u << (digit - 1))) {
      ++count; total += digit; digits += char('0' + digit);
    }
    if (count == length && total == sum) out.push_back(std::move(digits));
  }
  std::sort(out.begin(), out.end());
  return out;
}

bool KakuroBoard::isHinted(uint8_t row, uint8_t column) const {
  return isPlayable(row,column) && hinted[idx(row,column)] != 0;
}
bool KakuroBoard::hint(uint8_t row, uint8_t column) {
  int cell = isPlayable(row,column) && !isHinted(row,column) && value(row,column) != solution[idx(row,column)]
      ? idx(row,column) : -1;
  if (cell < 0) for (int i = 0; i < MAX_CELLS; ++i) if (playable[i] && !hinted[i] && user[i] != solution[i]) { cell = i; break; }
  if (cell < 0) return false;
  user[cell] = solution[cell]; hinted[cell] = 1; return true;
}
bool KakuroBoard::undo() {
  while (historyCount) {
    const auto move = history[--historyCount];
    if (hinted[move.cell]) continue;
    user[move.cell] = move.value; return true;
  }
  return false;
}
