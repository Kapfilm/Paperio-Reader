#pragma once

#include <array>
#include <cstdint>

enum class FifteenControlMode : uint8_t { EmptyCell = 0, Tiles = 1 };

struct FifteenSaveSlot {
  std::array<uint8_t, 16> tiles{};
  uint32_t moves = 0;
};

class FifteenStore {
 public:
  static bool hasInProgress();
  static bool load(FifteenSaveSlot& out);
  static bool save(const FifteenSaveSlot& slot);
  static bool clear();
  static FifteenControlMode loadControlMode();
  static bool saveControlMode(FifteenControlMode mode);
};
