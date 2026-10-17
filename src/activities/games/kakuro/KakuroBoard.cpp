#include "KakuroBoard.h"

#include <cstring>

namespace {

// These compact 3x3 Kakuro components were generated offline and exhaustively
// checked against their row/column sums. Every component has exactly one
// solution. A full puzzle is composed of two, three, or four such components.
constexpr uint8_t kUniqueBlocks[][9] = {
    {9, 6, 8, 8, 1, 2, 6, 3, 1}, {2, 1, 6, 1, 3, 4, 4, 7, 9}, {2, 9, 8, 1, 2, 5, 4, 7, 9}, {8, 2, 1, 6, 1, 3, 9, 4, 7},
    {4, 1, 2, 7, 3, 1, 9, 6, 4}, {9, 6, 8, 7, 5, 9, 4, 2, 1}, {8, 9, 6, 3, 5, 1, 9, 7, 3},
};
constexpr uint8_t kBlockCount = sizeof(kUniqueBlocks) / sizeof(kUniqueBlocks[0]);
constexpr uint8_t kOrigins[4][2] = {{1, 1}, {1, 5}, {5, 1}, {5, 5}};

}  // namespace

bool KakuroBoard::build(Difficulty difficulty, uint8_t variant) {
  memset(playable, 0, sizeof(playable));
  memset(solution, 0, sizeof(solution));
  memset(user, 0, sizeof(user));
  memset(clueRight, 0, sizeof(clueRight));
  memset(clueDown, 0, sizeof(clueDown));

  columns = MAX_SIZE;
  rows = difficulty == Difficulty::Easy ? 4 : MAX_SIZE;
  const uint8_t components = difficulty == Difficulty::Easy ? 2 : (difficulty == Difficulty::Medium ? 3 : 4);

  for (uint8_t component = 0; component < components; component++) {
    const uint8_t originRow = kOrigins[component][0];
    const uint8_t originColumn = kOrigins[component][1];
    const uint8_t blockIndex = static_cast<uint8_t>((variant * 2 + component) % kBlockCount);
    for (uint8_t row = 0; row < 3; row++) {
      for (uint8_t column = 0; column < 3; column++) {
        const uint8_t boardIndex = idx(originRow + row, originColumn + column);
        playable[boardIndex] = 1;
        solution[boardIndex] = kUniqueBlocks[blockIndex][row * 3 + column];
      }
    }
  }

  // Derive all clue cells directly from the stored solution. This prevents a
  // transcription mismatch between clue data and the verified answer.
  for (uint8_t row = 0; row < rows; row++) {
    for (uint8_t column = 0; column < columns; column++) {
      const uint8_t cell = idx(row, column);
      if (playable[cell]) continue;
      if (column + 1 < columns && playable[idx(row, column + 1)]) {
        uint8_t sum = 0;
        for (uint8_t c = column + 1; c < columns && playable[idx(row, c)]; c++) sum += solution[idx(row, c)];
        clueRight[cell] = sum;
      }
      if (row + 1 < rows && playable[idx(row + 1, column)]) {
        uint8_t sum = 0;
        for (uint8_t r = row + 1; r < rows && playable[idx(r, column)]; r++) sum += solution[idx(r, column)];
        clueDown[cell] = sum;
      }
    }
  }

  return validateDefinition();
}

void KakuroBoard::resetUser() { memset(user, 0, sizeof(user)); }

bool KakuroBoard::isPlayable(uint8_t row, uint8_t column) const {
  return row < rows && column < columns && playable[idx(row, column)] != 0;
}

uint8_t KakuroBoard::value(uint8_t row, uint8_t column) const {
  return isPlayable(row, column) ? user[idx(row, column)] : 0;
}

bool KakuroBoard::place(uint8_t row, uint8_t column, uint8_t digit) {
  if (!isPlayable(row, column) || digit > 9) return false;
  user[idx(row, column)] = digit;
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
  if (!isPlayable(row, column)) return false;
  const uint8_t cell = idx(row, column);
  return user[cell] != 0 && user[cell] != solution[cell];
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

bool KakuroBoard::writeUserTo(HalFile& file) const { return file.write(user, sizeof(user)) == sizeof(user); }

bool KakuroBoard::readUserFrom(HalFile& file) {
  if (file.read(user, sizeof(user)) != sizeof(user)) return false;
  for (uint8_t row = 0; row < rows; row++) {
    for (uint8_t column = 0; column < columns; column++) {
      const uint8_t cell = idx(row, column);
      if ((!playable[cell] && user[cell] != 0) || user[cell] > 9) return false;
    }
  }
  return true;
}
