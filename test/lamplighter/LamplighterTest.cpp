#include <gtest/gtest.h>
#include "activities/games/lamplighter/LamplighterModel.h"
using lamplighter::Model;
TEST(LamplighterBanks, AllSixHundredAreTreesAndStartsUseMatchingShapes){
 Model m;for(int level=0;level<3;++level)for(int p=0;p<200;++p){m.open(level,p);ASSERT_EQ(m.cells(),(5+2*level)*(5+2*level));ASSERT_LT(m.source(),m.cells());for(int i=0;i<m.cells();++i){auto mask=m.tiles[i];bool found=false;for(int k=0;k<4;++k){found|=mask==m.solution(i);mask=Model::rotateMask(mask);}ASSERT_TRUE(found);m.tiles[i]=m.solution(i);}ASSERT_TRUE(m.solved())<<level<<' '<<p;ASSERT_EQ(m.lampCount(true),m.lampCount(false));}
}
TEST(Lamplighter, HintsSolveEveryPuzzleWithoutUndoBreakingHintLocks){
 Model m;for(int l=0;l<3;++l)for(int p=0;p<200;++p){m.open(l,p);int n=0;while(!m.solved()&&n++<82){int cell=m.hint();ASSERT_GE(cell,0);ASSERT_EQ(m.tiles[cell],m.solution(cell));ASSERT_EQ(m.locks[cell],2);ASSERT_FALSE(m.toggleLock(cell));}ASSERT_TRUE(m.solved());ASSERT_TRUE(m.wasCompleted(l,p));ASSERT_FALSE(m.undo());}
 for(int l=0;l<3;++l)EXPECT_EQ(m.completedCount(l),200);
}
TEST(Lamplighter, RotationLocksUndoAndRestart){Model m;int cell=0;while(cell<m.cells()&&Model::rotateMask(m.tiles[cell])==m.tiles[cell])++cell;ASSERT_LT(cell,m.cells());auto old=m.tiles[cell];ASSERT_TRUE(m.rotate(cell));EXPECT_EQ(m.tiles[cell],Model::rotateMask(old));EXPECT_EQ(m.moves,1u);ASSERT_TRUE(m.undo());EXPECT_EQ(m.tiles[cell],old);EXPECT_EQ(m.moves,2u);ASSERT_TRUE(m.toggleLock(cell));EXPECT_FALSE(m.rotate(cell));ASSERT_TRUE(m.toggleLock(cell));EXPECT_TRUE(m.rotate(cell));m.restart();EXPECT_TRUE(m.history.empty());EXPECT_EQ(m.moves,0u);for(int i=0;i<m.cells();++i){EXPECT_EQ(m.tiles[i],m.initial(i));EXPECT_EQ(m.locks[i],0);}}
TEST(Lamplighter, RequiresAllCellsNoLooseEndsAndNoLoops){Model m;m.open(0,0);for(int i=0;i<m.cells();++i)m.tiles[i]=m.solution(i);ASSERT_TRUE(m.solved());m.tiles[0]|=1;EXPECT_FALSE(m.solved());m.tiles.fill(15);EXPECT_FALSE(m.solved());m.tiles.fill(0);EXPECT_FALSE(m.solved());}
TEST(LamplighterSaves, RoundTripEveryDifficultyAndRejectCorruption){Model m;for(int l=0;l<3;++l){m.open(l,199);m.rotate(0);m.toggleLock(1);m.hint();auto bytes=m.encode();Model restored;ASSERT_TRUE(restored.decode(bytes));EXPECT_EQ(restored.encode(),bytes);auto old=restored.encode();for(size_t p=0;p<bytes.size();++p){auto bad=bytes;bad[p]^=0x80;EXPECT_FALSE(restored.decode(bad));EXPECT_EQ(restored.encode(),old);}for(size_t size=0;size<bytes.size();++size){std::vector<uint8_t>shortened(bytes.begin(),bytes.begin()+size);EXPECT_FALSE(restored.decode(shortened));}}}
TEST(LamplighterSaves, BoundedHistorySurvivesHintAndUndo){Model m;for(int i=0;i<700;++i){m.rotate(i%m.cells());}EXPECT_LE(m.history.size(),500u);for(int i=0;i<5;++i)m.hint();for(int i=0;i<20;++i)m.undo();for(int i=0;i<m.cells();++i)if(m.locks[i]==2)EXPECT_EQ(m.tiles[i],m.solution(i));Model copy;ASSERT_TRUE(copy.decode(m.encode()));EXPECT_EQ(copy.encode(),m.encode());}

TEST(LamplighterSaves, RejectsInvalidFieldsEvenWithValidChecksum){
 Model m;m.open(2,199);m.hint();auto bytes=m.encode();
 auto seal=[](std::vector<uint8_t>&b){uint32_t h=2166136261u;for(size_t i=0;i+4<b.size();++i)h=(h^b[i])*16777619u;for(int k=0;k<4;++k)b[b.size()-4+k]=h>>(8*k);};
 auto reject=[&](size_t index,uint8_t value){auto bad=bytes;bad[index]=value;seal(bad);Model copy;auto before=copy.encode();EXPECT_FALSE(copy.decode(bad));EXPECT_EQ(copy.encode(),before);};
 reject(4,3);reject(5,200);reject(13,200);reject(100,0);reject(181,3);
 auto bad=bytes;size_t countOffset=100+2*m.cells();bad[countOffset]=255;bad[countOffset+1]=255;seal(bad);Model restored;EXPECT_FALSE(restored.decode(bad));
}
