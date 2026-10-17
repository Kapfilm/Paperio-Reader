#pragma once

#include <HalStorage.h>

#include <cstdint>

class KakuroBoard {
 public:
  static constexpr uint8_t MAX_SIZE = 8;
  static constexpr uint8_t MAX_CELLS = MAX_SIZE * MAX_SIZE;

  enum class Difficulty : uint8_t { Easy = 0, Medium = 1, Hard = 2 };

  uint8_t rows = 0;
  uint8_t columns = 0;
  uint8_t playable[MAX_CELLS] = {};
  uint8_t solution[MAX_CELLS] = {};
  uint8_t user[MAX_CELLS] = {};
  uint8_t clueRight[MAX_CELLS] = {};
  uint8_t clueDown[MAX_CELLS] = {};

  static constexpr uint8_t idx(uint8_t row, uint8_t column) { return row * MAX_SIZE + column; }

  // Builds one of three deterministic variants. Each active 3x3 component has
  // a separately verified unique solution; combining them preserves uniqueness.
  bool build(Difficulty difficulty, uint8_t variant);
  void resetUser();

  bool isPlayable(uint8_t row, uint8_t column) const;
  uint8_t value(uint8_t row, uint8_t column) const;
  bool place(uint8_t row, uint8_t column, uint8_t digit);
  bool isSolved() const;
  bool hasError(uint8_t row, uint8_t column) const;
  bool validateDefinition() const;
  uint8_t countPlayable() const;
  uint8_t countFilled() const;

  bool writeUserTo(HalFile& file) const;
  bool readUserFrom(HalFile& file);
};
