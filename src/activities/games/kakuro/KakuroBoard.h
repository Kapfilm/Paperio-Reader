#pragma once

#include <HalStorage.h>

#include <cstdint>
#include <array>
#include <string>
#include <vector>

class KakuroBoard {
 public:
  static constexpr uint8_t MAX_SIZE = 8;
  static constexpr uint8_t PUZZLES_PER_LEVEL = 200;
  static constexpr uint8_t MAX_CELLS = MAX_SIZE * MAX_SIZE;

  enum class Difficulty : uint8_t { Easy = 0, Medium = 1, Hard = 2 };

  uint8_t rows = 0;
  uint8_t columns = 0;
  uint8_t playable[MAX_CELLS] = {};
  uint8_t solution[MAX_CELLS] = {};
  uint8_t user[MAX_CELLS] = {};
  uint8_t hinted[MAX_CELLS] = {};
  uint8_t clueRight[MAX_CELLS] = {};
  uint8_t clueDown[MAX_CELLS] = {};

  static constexpr uint8_t idx(uint8_t row, uint8_t column) { return row * MAX_SIZE + column; }

  // Original banks: 200 connected, uniquely solvable puzzles per difficulty.
  bool build(Difficulty difficulty, uint8_t variant);
  void resetUser();

  bool isPlayable(uint8_t row, uint8_t column) const;
  uint8_t value(uint8_t row, uint8_t column) const;
  bool place(uint8_t row, uint8_t column, uint8_t digit);
  bool isSolved() const;
  struct Run { std::array<uint8_t, 9> cells{}; uint8_t count = 0; uint8_t sum = 0; };
  Run runFor(uint8_t row, uint8_t column, bool across) const;
  static std::vector<std::string> combinations(uint8_t length, uint8_t sum);
  bool hint(uint8_t row, uint8_t column);
  bool isHinted(uint8_t row, uint8_t column) const;
  bool undo();
  bool hasError(uint8_t row, uint8_t column) const;
  bool validateDefinition() const;
  uint8_t countPlayable() const;
  uint8_t countFilled() const;

  bool writeUserTo(HalFile& file) const;
  bool readUserFrom(HalFile& file);
 private:
  struct Move { uint8_t cell, value; };
  std::array<Move, 128> history{};
  uint8_t historyCount = 0;
};
