#include <gtest/gtest.h>
#include "activities/games/kakuro/KakuroBoard.h"
#include <set>
#include <cstring>

TEST(Kakuro, All600OriginalDefinitionsAndSolutions) {
  for (int level=0; level<3; ++level) {
    std::set<std::string> seen;
    for (int puzzle=0; puzzle<200; ++puzzle) {
      KakuroBoard b; ASSERT_TRUE(b.build(static_cast<KakuroBoard::Difficulty>(level), puzzle));
      EXPECT_EQ(b.rows,6+level); EXPECT_EQ(b.columns,6+level);
      EXPECT_TRUE(seen.insert(std::string(reinterpret_cast<char*>(b.solution),64)).second);
      EXPECT_FALSE(b.isSolved());
      for (int r=0;r<b.rows;++r) for(int c=0;c<b.columns;++c) if(b.isPlayable(r,c)) {
        for(bool across:{true,false}) {
          auto run=b.runFor(r,c,across); ASSERT_GE(run.count,2); ASSERT_LE(run.count,9);
          std::string digits;
          for(int i=0;i<run.count;++i) digits+=char('0'+b.solution[run.cells[i]]);
          std::sort(digits.begin(),digits.end());
          auto combos=b.combinations(run.count,run.sum);
          EXPECT_NE(std::find(combos.begin(),combos.end(),digits),combos.end());
        }
        EXPECT_TRUE(b.place(r,c,b.solution[b.idx(r,c)]));
      }
      EXPECT_TRUE(b.isSolved());
      for(int r=0;r<b.rows;++r)for(int c=0;c<b.columns;++c)EXPECT_FALSE(b.hasError(r,c));
    }
  }
}
TEST(Kakuro, RuleErrorsDoNotRevealArbitraryWrongDigits) {
  KakuroBoard b;ASSERT_TRUE(b.build(KakuroBoard::Difficulty::Easy,0));
  int cell=0;while(!b.playable[cell])++cell;
  auto run=b.runFor(cell/8,cell%8,true);ASSERT_GE(run.count,2);
  ASSERT_TRUE(b.place(run.cells[0]/8,run.cells[0]%8,1));
  ASSERT_TRUE(b.place(run.cells[1]/8,run.cells[1]%8,1));
  EXPECT_TRUE(b.hasError(run.cells[0]/8,run.cells[0]%8));
  EXPECT_TRUE(b.hasError(run.cells[1]/8,run.cells[1]%8));
  ASSERT_TRUE(b.undo()); EXPECT_EQ(b.user[run.cells[1]],0);
  EXPECT_FALSE(b.hasError(run.cells[0]/8,run.cells[0]%8));
}
TEST(Kakuro, HintsAreProtectedUndoAndPersistenceAreValidated) {
  KakuroBoard b;ASSERT_TRUE(b.build(KakuroBoard::Difficulty::Hard,199));
  int cell=0;while(!b.playable[cell])++cell;
  ASSERT_TRUE(b.place(cell/8,cell%8, b.solution[cell]==1?2:1));
  ASSERT_TRUE(b.hint(cell/8,cell%8)); EXPECT_TRUE(b.isHinted(cell/8,cell%8));
  EXPECT_FALSE(b.place(cell/8,cell%8,0)); EXPECT_FALSE(b.undo()); EXPECT_EQ(b.user[cell],b.solution[cell]);
  auto file=HalFile::forReadWrite(); ASSERT_TRUE(b.writeUserTo(file)); file.seek(0);
  KakuroBoard restored;ASSERT_TRUE(restored.build(KakuroBoard::Difficulty::Hard,199));ASSERT_TRUE(restored.readUserFrom(file));
  EXPECT_EQ(restored.user[cell],b.solution[cell]);EXPECT_TRUE(restored.isHinted(cell/8,cell%8));
  auto truncated=HalFile::fromString("abc");EXPECT_FALSE(restored.readUserFrom(truncated)); EXPECT_TRUE(restored.isHinted(cell/8,cell%8));
  b.resetUser();EXPECT_FALSE(b.isHinted(cell/8,cell%8));EXPECT_EQ(b.countFilled(),0);
}
TEST(Kakuro, CombinationsAndBounds) {
  EXPECT_EQ(KakuroBoard::combinations(2,16),(std::vector<std::string>{"79"}));
  EXPECT_EQ(KakuroBoard::combinations(3,7),(std::vector<std::string>{"124"}));
  EXPECT_EQ(KakuroBoard::combinations(2,10).size(),4u);
  KakuroBoard b;EXPECT_FALSE(b.build(static_cast<KakuroBoard::Difficulty>(3),0));EXPECT_FALSE(b.build(KakuroBoard::Difficulty::Easy,200));
}

namespace {
// Independent run-consistency solver: enumerate every legal assignment under
// current cell domains, intersect support from across/down, and repeat. Reaching
// singleton domains proves uniqueness without consulting the stored answer.
void supportAssignments(const KakuroBoard::Run& run, const std::array<uint16_t,64>& domains,
                        int position, int remaining, uint16_t used,
                        std::array<uint8_t,9>& chosen, std::array<uint16_t,64>& support) {
  if (position == run.count) {
    if (remaining == 0) for (int i=0;i<run.count;++i) support[run.cells[i]] |= uint16_t(1u<<chosen[i]);
    return;
  }
  if (remaining <= 0) return;
  const auto cell=run.cells[position];
  for(int digit=1;digit<=9;++digit) if ((domains[cell]&(1u<<digit)) && !(used&(1u<<digit)) && digit<=remaining) {
    chosen[position]=digit;
    supportAssignments(run,domains,position+1,remaining-digit,used|(1u<<digit),chosen,support);
  }
}
}
TEST(Kakuro, All600AreUniquelySolvedByRunReasoning) {
  for(int level=0;level<3;++level)for(int puzzle=0;puzzle<200;++puzzle) {
    SCOPED_TRACE(std::to_string(level)+":"+std::to_string(puzzle));
    KakuroBoard b;ASSERT_TRUE(b.build(static_cast<KakuroBoard::Difficulty>(level),puzzle));
    std::array<uint16_t,64> domains{};
    std::vector<KakuroBoard::Run> runs;
    for(int r=0;r<b.rows;++r)for(int c=0;c<b.columns;++c)if(b.isPlayable(r,c)) {
      domains[b.idx(r,c)]=0x3fe;
      if(c==0||!b.isPlayable(r,c-1))runs.push_back(b.runFor(r,c,true));
      if(r==0||!b.isPlayable(r-1,c))runs.push_back(b.runFor(r,c,false));
    }
    bool changed=true;
    while(changed) {
      changed=false;
      for(const auto&run:runs) {
        std::array<uint16_t,64> support{};std::array<uint8_t,9> chosen{};
        supportAssignments(run,domains,0,run.sum,0,chosen,support);
        for(int i=0;i<run.count;++i) {
          const int cell=run.cells[i];const auto next=domains[cell]&support[cell];
          ASSERT_NE(next,0);changed|=next!=domains[cell];domains[cell]=next;
        }
      }
    }
    for(int cell=0;cell<64;++cell)if(b.playable[cell])EXPECT_EQ(domains[cell],1u<<b.solution[cell]);
  }
}
