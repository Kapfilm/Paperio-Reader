#include <gtest/gtest.h>
#include <algorithm>
#include <numeric>
#include <random>
#include "activities/games/casefiles/CaseFilesModel.h"
#include "activities/games/casefiles/CaseFilesData.h"
using namespace casefiles;
TEST(CaseBanks, AllSixHundredParseAndCluesAgreeWithSolution){
 std::array<int,6>themes{};std::array<int,6>types{};
 for(int l=0;l<3;++l)for(int i=0;i<200;++i){Puzzle p;ASSERT_TRUE(p.parse(bankLine(l,i)))<<l<<" "<<i;EXPECT_EQ(p.n,l==2?5:4);EXPECT_EQ(p.k,l==0?3:4);++themes[p.theme];for(const auto&c:p.clues){EXPECT_TRUE(p.clueTrue(c))<<l<<" "<<i<<" "<<c.type;++types[c.type=='='?0:c.type=='!'?1:c.type=='e'?2:c.type=='o'?3:c.type=='<'?4:5];}}
 for(auto n:themes)EXPECT_GT(n,0);for(auto n:types)EXPECT_GT(n,0);
}
namespace {
int solutions(Puzzle& p,int g){
 if(g==p.k)return 1;int total=0;std::array<uint8_t,5> perm{};std::iota(perm.begin(),perm.begin()+p.n,0);
 do{for(int person=0;person<p.n;++person){p.groups[g].slot[person]=perm[person];p.groups[g].owner[perm[person]]=person;}
  bool ok=true;for(const auto&c:p.clues){bool ready=true;for(int e=0;e<c.count;++e)if(c.entities[e].group>g)ready=false;if((c.type=='<'||c.type=='+')&&c.group>g)ready=false;if(ready&&!p.clueTrue(c)){ok=false;break;}}
  if(ok){total+=solutions(p,g+1);if(total>1)return total;}
 }while(std::next_permutation(perm.begin(),perm.begin()+p.n));return total;
}
}
TEST(CaseBanks, EveryPuzzleHasOneSolution){for(int l=0;l<3;++l)for(int i=0;i<200;++i){Puzzle p;ASSERT_TRUE(p.parse(bankLine(l,i)));EXPECT_EQ(solutions(p,1),1)<<l<<" "<<i;}}
TEST(CaseRules, MarksDotsConflictsAndUndo){Model m;ASSERT_TRUE(m.open(2,0));m.table=0;m.cursor=0;EXPECT_TRUE(m.cycle());EXPECT_EQ(m.marks[0][0],Model::Cross);EXPECT_TRUE(m.cycle());EXPECT_EQ(m.marks[0][0],Model::Tick);EXPECT_TRUE(m.ruledOut(0,0,1));EXPECT_EQ(m.marks[0][1],Model::Blank);m.cursor=1;m.cycle();m.cycle();EXPECT_TRUE(m.conflict(0,0,0));EXPECT_TRUE(m.undo());EXPECT_EQ(m.marks[0][1],Model::Cross);}
TEST(CaseRules, HintRemovesWrongMarkBeforeGivingProtectedTick){Model m;ASSERT_TRUE(m.open(1,0));int wrong=0;while(m.belongs(0,wrong/m.puzzle.n,wrong%m.puzzle.n))++wrong;m.cursor=wrong;m.cycle();m.cycle();EXPECT_EQ(m.wrongCount(),1);ASSERT_TRUE(m.hint());EXPECT_EQ(m.wrongCount(),0);EXPECT_EQ(m.marks[0][wrong],Model::Blank);ASSERT_TRUE(m.hint());EXPECT_EQ(m.marks[m.table][m.cursor],Model::Hint);EXPECT_FALSE(m.cycle());}
TEST(CaseRules, CompletionUsesOnlyNameGridsAndSurvivesRevisit){Model m;ASSERT_TRUE(m.open(2,0));for(int pi=0;pi<m.pairCount;++pi)if(m.pairs[pi].a==0)for(int c=0;c<25;++c)if(m.belongs(pi,c/5,c%5))m.marks[pi][c]=Model::Tick;m.noteSolved();EXPECT_TRUE(m.solved());EXPECT_TRUE(m.isCompleted(2,0));m.open(2,1);m.open(2,0);EXPECT_TRUE(m.isCompleted(2,0));EXPECT_EQ(m.completedCount(2),1);m.reset();EXPECT_FALSE(m.isCompleted(2,0));}
TEST(CaseSave, RoundTripPreservesProgressMarksStrikesAndUndo){Model m;m.open(2,31);m.cursor=4;m.cycle();m.struck[0]=1;m.hint();m.completed[0]=0x55;auto b=m.encode();Model restored;ASSERT_TRUE(restored.decode(b));EXPECT_EQ(restored.encode(),b);EXPECT_EQ(restored.completed[0],0x55);EXPECT_TRUE(restored.undo());}
TEST(CaseSave, CorruptTruncatedAndRandomSavesRejected){Model m;m.open(0,0);auto b=m.encode();for(size_t i=0;i<b.size();++i){auto bad=b;bad[i]^=0x80;Model r;EXPECT_FALSE(r.decode(bad));}for(size_t i=0;i<b.size();++i){auto bad=b;bad.resize(i);Model r;EXPECT_FALSE(r.decode(bad));}std::mt19937 rng(9);for(int i=0;i<1000;++i){std::vector<uint8_t>bad(rng()%1900);for(auto&v:bad)v=rng();Model r;EXPECT_FALSE(r.decode(bad));}}
TEST(CaseSemantics, ExclusiveOrPairAndOriginalOrder){Puzzle p;p.theme=5;p.k=3;p.n=4;for(int g=0;g<3;++g)for(int i=0;i<4;++i){p.groups[g].owner[i]=i;p.groups[g].slot[i]=i;p.groups[g].items[i]=i+1;}
 Clue c;c.type='e';c.count=3;c.entities[0]={0,0};c.entities[1]={1,0};c.entities[2]={2,1};EXPECT_TRUE(p.clueTrue(c));c.entities[2]={2,0};EXPECT_FALSE(p.clueTrue(c));
 c.type='o';c.count=4;c.entities={Entity{0,0},Entity{0,1},Entity{1,1},Entity{2,0}};EXPECT_TRUE(p.clueTrue(c));c.entities[1]={1,0};EXPECT_FALSE(p.clueTrue(c));
 c.type='+';c.count=2;c.group=2;c.entities[0]={0,2};c.entities[1]={0,1};c.difference=1;EXPECT_TRUE(p.clueTrue(c));p.groups[2].items[2]=4;c.difference=2;EXPECT_TRUE(p.clueTrue(c));
}
TEST(CaseSave, RejectsOutOfRangeFieldsEvenWithValidChecksum){
 Model m;m.open(2,0);auto original=m.encode();
 auto forge=[&](size_t offset,uint8_t value){auto b=original;b[offset]=value;uint32_t hash=2166136261u;for(size_t i=0;i<b.size()-4;++i)hash=(hash^b[i])*16777619u;for(int i=0;i<4;++i)b[b.size()-4+i]=hash>>(8*i);Model restored;return restored.decode(b);};
 EXPECT_FALSE(forge(4,3));EXPECT_FALSE(forge(5,200));EXPECT_FALSE(forge(6,6));EXPECT_FALSE(forge(7,25));
 EXPECT_FALSE(forge(8,200));EXPECT_FALSE(forge(8+3+75,2));EXPECT_FALSE(forge(8+3+75+12,4));
}

#include "activities/games/casefiles/CaseFilesControls.h"
TEST(CaseControls, ShortPressAndLongReleaseAreExclusive){
 HoldButton b;using E=HoldButton::Event;
 EXPECT_EQ(b.update(true,true,false,100),E::None);
 EXPECT_EQ(b.update(false,false,true,200),E::Short);
 EXPECT_EQ(b.update(true,true,false,300),E::None);
 EXPECT_EQ(b.update(false,true,false,950),E::Long);
 EXPECT_EQ(b.update(false,true,false,1100),E::None);
 EXPECT_EQ(b.update(false,false,true,1200),E::None);
 EXPECT_EQ(b.update(false,false,true,1300),E::None);
}
TEST(CaseControls, CancelAndClockWrapDoNotGenerateExtraMarks){
 HoldButton b;using E=HoldButton::Event;
 b.update(true,true,false,10);b.reset();EXPECT_EQ(b.update(false,false,true,20),E::None);
 b.update(true,true,false,0xffffff00u);EXPECT_EQ(b.update(false,true,false,0x200u),E::Long);
 EXPECT_EQ(b.update(false,false,true,0x300u),E::None);
 EXPECT_EQ(b.update(true,false,true,1000),E::Short);
}
