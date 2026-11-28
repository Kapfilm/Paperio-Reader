#include <gtest/gtest.h>
#include <unordered_set>
#include "activities/games/pegsolitaire/PegSolitaireModel.h"

using Game = PegSolitaireModel;
TEST(PegSolitaire, InitialBoardsHaveCorrectHolesAndPegs) {
  Game english; EXPECT_EQ(english.size(), 33); EXPECT_EQ(english.count(), 32);
  EXPECT_FALSE(english.occupied(english.index(3, 3))); EXPECT_EQ(english.index(0, 0), -1);
  EXPECT_EQ(english.index(6, 6), -1);
  Game triangle(Game::Board::Triangle); EXPECT_EQ(triangle.size(), 15); EXPECT_EQ(triangle.count(), 14);
  EXPECT_EQ(triangle.index(1, 2), -1); EXPECT_FALSE(triangle.occupied(0));
}
TEST(PegSolitaire, EnglishHasFourOpeningJumpsAndForbidsDiagonal) {
  Game g; int n = 0;
  for (int a = 0; a < g.size(); ++a) for (int b = 0; b < g.size(); ++b) n += g.legal(a, b);
  EXPECT_EQ(n, 4); EXPECT_FALSE(g.legal(g.index(1, 1), g.index(3, 3)));
  EXPECT_FALSE(g.jump(-1, 0)); EXPECT_FALSE(g.jump(0, 99)); EXPECT_EQ(g.count(), 32);
}
TEST(PegSolitaire, JumpUndoAndReloadKeepExactState) {
  Game g; ASSERT_TRUE(g.jump(g.index(1, 3), g.index(3, 3)));
  EXPECT_EQ(g.count(), 31); EXPECT_FALSE(g.occupied(g.index(2, 3))); EXPECT_EQ(g.moves(), 1);
  Game loaded(Game::Board::Triangle); ASSERT_TRUE(loaded.restore(g.board(), g.history()));
  for (int i = 0; i < g.size(); ++i) EXPECT_EQ(g.occupied(i), loaded.occupied(i));
  ASSERT_TRUE(loaded.undo()); EXPECT_EQ(loaded.count(), 32); EXPECT_FALSE(loaded.undo());
}
TEST(PegSolitaire, InvalidSaveDoesNotMutateLiveGame) {
  Game g; ASSERT_TRUE(g.jump(g.index(1, 3), g.index(3, 3))); auto history = g.history();
  EXPECT_FALSE(g.restore(Game::Board::English, {{0, 255}})); EXPECT_EQ(g.moves(), 1);
  EXPECT_EQ(g.history()[0].from, history[0].from);
  EXPECT_FALSE(g.restore(static_cast<Game::Board>(9), {}));
  std::vector<Game::Jump> excessive(32, {0, 0}); EXPECT_FALSE(g.restore(Game::Board::English, excessive));
}
TEST(PegSolitaire, TriangleAcceptsBothDiagonalAxesButNotEnglishDiagonal) {
  Game g(Game::Board::Triangle);
  EXPECT_TRUE(g.legal(g.index(2, 0), g.index(0, 0)));
  EXPECT_TRUE(g.legal(g.index(2, 2), g.index(0, 0)));
  EXPECT_FALSE(g.legal(g.index(2, 1), g.index(0, 0)));
}
namespace {
uint64_t mask(const Game& g) { uint64_t out = 0; for (int i = 0; i < g.size(); ++i) if (g.occupied(i)) out |= uint64_t{1} << i; return out; }
bool solve(Game& g, std::unordered_set<uint64_t>& dead) {
  if (g.count() == 1) return true;
  auto key = mask(g); if (dead.count(key)) return false;
  for (int a = 0; a < g.size(); ++a) for (int b = 0; b < g.size(); ++b) if (g.jump(a, b)) {
    if (solve(g, dead)) return true; g.undo();
  }
  dead.insert(key); return false;
}
}
TEST(PegSolitaire, TriangleCanCompleteAndUndoEntireWinningGame) {
  Game g(Game::Board::Triangle); std::unordered_set<uint64_t> dead;
  ASSERT_TRUE(solve(g, dead)); EXPECT_EQ(g.count(), 1); EXPECT_EQ(g.moves(), 13); EXPECT_FALSE(g.anyMove());
  Game loaded; ASSERT_TRUE(loaded.restore(g.board(), g.history())); EXPECT_EQ(mask(g), mask(loaded));
  for (int i = 0; i < 13; ++i) ASSERT_TRUE(loaded.undo());
  EXPECT_EQ(loaded.count(), 14); EXPECT_EQ(loaded.moves(), 0); EXPECT_FALSE(loaded.occupied(0));
}
TEST(PegSolitaire, EveryHoleIsReachableWithDirectionalButtons) {
  for (auto type : {Game::Board::English, Game::Board::Triangle}) {
    Game g(type); std::unordered_set<int> seen{g.initialHole()};
    bool changed = true;
    while (changed) {
      changed = false; auto old = seen;
      for (int i : old) for (auto d : {Game::Cell{-1,0}, {1,0}, {0,-1}, {0,1}})
        changed |= seen.insert(g.navigate(i, d.row, d.col)).second;
    }
    EXPECT_EQ(seen.size(), g.size());
  }
}

TEST(PegSolitaire, EnglishCanWinInCentreAndReplayAllThirtyOneJumps) {
  // A full solution found by a depth-first search over this C++ model's jumps.
  const std::vector<Game::Jump> solution = {
      {4,16},{7,9},{0,8},{2,0},{9,7},{6,8},{10,2},{12,10},{15,3},{0,8},{13,15},
      {15,3},{17,5},{2,10},{19,17},{17,5},{27,15},{20,22},{22,8},{3,15},{15,17},
      {24,10},{5,17},{26,24},{23,25},{32,24},{17,29},{30,32},{32,24},{25,23},{28,16}};
  Game g;
  for (auto j : solution) ASSERT_TRUE(g.jump(j.from, j.to));
  EXPECT_EQ(g.count(), 1); EXPECT_EQ(g.moves(), 31); EXPECT_TRUE(g.perfect()); EXPECT_FALSE(g.anyMove());
  Game loaded; ASSERT_TRUE(loaded.restore(Game::Board::English, g.history()));
  EXPECT_TRUE(loaded.perfect());
  for (int i = 0; i < 31; ++i) ASSERT_TRUE(loaded.undo());
  EXPECT_EQ(loaded.count(), 32); EXPECT_EQ(loaded.moves(), 0);
}
