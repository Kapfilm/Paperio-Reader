#pragma once

#include <array>
#include <cstdint>
#include <vector>

// Rules/layout adapted from Omer Faruk Inel's MIT-licensed Peg Solitaire.
// See LICENSE.txt. Input, persistence and C++ model are specific to Paperio.
class PegSolitaireModel {
 public:
  enum class Board : uint8_t { English = 0, Triangle = 1 };
  struct Cell { int row; int col; };
  struct Jump { uint8_t from; uint8_t to; };
  explicit PegSolitaireModel(Board b = Board::English) { reset(b); }
  void reset(Board b) {
    board_ = b; size_ = 0; history_.clear();
    for (int row = 0; row < (b == Board::English ? 7 : 5); ++row)
      for (int col = 0; col < (b == Board::English ? 7 : row + 1); ++col)
        if (b == Board::Triangle || (row >= 2 && row <= 4) || (col >= 2 && col <= 4))
          cells_[size_++] = {row, col};
    pegs_ = (uint64_t{1} << size_) - 1;
    pegs_ &= ~(uint64_t{1} << initialHole());
  }
  Board board() const { return board_; }
  int size() const { return size_; }
  const Cell& cell(int i) const { return cells_[i]; }
  int initialHole() const { return board_ == Board::English ? index(3, 3) : index(0, 0); }
  int index(int row, int col) const {
    for (int i = 0; i < size_; ++i) if (cells_[i].row == row && cells_[i].col == col) return i;
    return -1;
  }
  bool occupied(int i) const { return i >= 0 && i < size_ && (pegs_ & (uint64_t{1} << i)); }
  int count() const {
    int n = 0; for (int i = 0; i < size_; ++i) n += occupied(i); return n;
  }
  int jumpedCell(int from, int to) const {
    if (from < 0 || from >= size_ || to < 0 || to >= size_) return -1;
    int dr = cells_[to].row - cells_[from].row, dc = cells_[to].col - cells_[from].col;
    bool direction = (dr == 0 && (dc == 2 || dc == -2)) ||
                     (dc == 0 && (dr == 2 || dr == -2));
    if (board_ == Board::Triangle) direction = direction || (dr == dc && (dr == 2 || dr == -2));
    if (!direction) return -1;
    return index(cells_[from].row + dr / 2, cells_[from].col + dc / 2);
  }
  bool legal(int from, int to) const {
    int over = jumpedCell(from, to);
    return over >= 0 && occupied(from) && occupied(over) && !occupied(to);
  }
  bool canJump(int from) const {
    for (int to = 0; to < size_; ++to) if (legal(from, to)) return true; return false;
  }
  bool anyMove() const {
    for (int from = 0; from < size_; ++from) if (canJump(from)) return true; return false;
  }
  bool jump(int from, int to) {
    if (!legal(from, to)) return false;
    int over = jumpedCell(from, to);
    pegs_ ^= (uint64_t{1} << from) | (uint64_t{1} << over) | (uint64_t{1} << to);
    history_.push_back({static_cast<uint8_t>(from), static_cast<uint8_t>(to)}); return true;
  }
  bool undo() {
    if (history_.empty()) return false;
    auto j = history_.back(); history_.pop_back();
    int over = jumpedCell(j.from, j.to);
    pegs_ ^= (uint64_t{1} << j.from) | (uint64_t{1} << over) | (uint64_t{1} << j.to); return true;
  }
  bool perfect() const { return count() == 1 && occupied(initialHole()); }
  int moves() const { return static_cast<int>(history_.size()); }
  const std::vector<Jump>& history() const { return history_; }
  // Replaying legal jumps validates every intermediate state from an SD save.
  bool restore(Board b, const std::vector<Jump>& jumps) {
    if (static_cast<unsigned>(b) > 1 || jumps.size() > 31) return false;
    PegSolitaireModel restored(b);
    for (auto j : jumps) if (!restored.jump(j.from, j.to)) return false;
    *this = restored; return true;
  }
  int navigate(int current, int dr, int dc) const {
    if (current < 0 || current >= size_) return initialHole();
    auto xy = [this](int i) {
      return Cell{cells_[i].row * 2, board_ == Board::Triangle ? cells_[i].col * 2 - cells_[i].row : cells_[i].col * 2};
    };
    auto origin = xy(current); int best = current, score = 10000;
    for (int i = 0; i < size_; ++i) {
      auto c = xy(i); int y = c.row - origin.row, x = c.col - origin.col;
      int along = dr ? y * dr : x * dc, across = dr ? x : y;
      if (along <= 0) continue;
      if (across < 0) across = -across;
      int value = along + 4 * across;
      if (value < score) { best = i; score = value; }
    }
    return best;
  }
 private:
  Board board_ = Board::English;
  std::array<Cell, 33> cells_{};
  int size_ = 0;
  uint64_t pegs_ = 0;
  std::vector<Jump> history_;
};
