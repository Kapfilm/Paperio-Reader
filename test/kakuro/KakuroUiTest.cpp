#include <gtest/gtest.h>
#include "activities/games/kakuro/KakuroCodec.h"
#include "activities/games/kakuro/KakuroLayout.h"
namespace {
kakuro_codec::Bytes encoded(){KakuroSaveSlot s;s.board.build(KakuroBoard::Difficulty::Hard,199);for(int r=0;r<8;++r)for(int c=0;c<8;++c)if(s.board.isPlayable(r,c)){s.cursorRow=r;s.cursorColumn=c;s.board.hint(r,c);goto found;}found:s.difficulty=KakuroBoard::Difficulty::Hard;s.variant=199;s.elapsedSec=3600;kakuro_codec::Bytes b{'K',2,2,199};kakuro_codec::u16(b,s.elapsedSec);b.push_back(s.cursorRow);b.push_back(s.cursorColumn);b.insert(b.end(),s.board.user,s.board.user+64);b.insert(b.end(),s.board.hinted,s.board.hinted+64);kakuro_codec::seal(b);return b;}
TEST(KakuroPersistence,RoundTripRetainsLastPuzzleAndProtectedHint){auto b=encoded();KakuroSaveSlot out;ASSERT_TRUE(kakuro_codec::decode(b,out));EXPECT_EQ(out.variant,199);EXPECT_EQ(out.elapsedSec,3600);EXPECT_TRUE(out.board.isHinted(out.cursorRow,out.cursorColumn));EXPECT_FALSE(out.board.place(out.cursorRow,out.cursorColumn,0));}
TEST(KakuroPersistence,RejectsEveryTruncationAndSingleByteCorruption){auto b=encoded();for(size_t n=0;n<b.size();++n){auto t=b;t.resize(n);KakuroSaveSlot out;EXPECT_FALSE(kakuro_codec::decode(t,out))<<n;}for(size_t i=0;i<b.size();++i){auto t=b;t[i]^=1;KakuroSaveSlot out;EXPECT_FALSE(kakuro_codec::decode(t,out))<<i;}}
TEST(KakuroPersistence,RejectsResealedInvalidIdAndHint){auto b=encoded();b[3]=200;b.resize(b.size()-4);kakuro_codec::seal(b);KakuroSaveSlot out;EXPECT_FALSE(kakuro_codec::decode(b,out));b=encoded();b[72]=2;b.resize(b.size()-4);kakuro_codec::seal(b);EXPECT_FALSE(kakuro_codec::decode(b,out));}
TEST(KakuroLayout,AllBoardsAndInterfaceSizesFitWithoutOverlappingPalette){for(int w:{480,528,600})for(int h:{792,800,1024})for(int board:{6,7,8})for(int hintHeight:{40,60,80}){auto v=kakuro_ui::layout(w,h,board,board,23,hintHeight);EXPECT_GE(v.cell,32);EXPECT_GE(v.left,0);EXPECT_LE(v.left+v.cell*board,w);EXPECT_LE(v.combinationY+6*23,v.paletteY);EXPECT_LE(v.paletteY+v.paletteHeight,h-hintHeight);}}
}

TEST(KakuroLayout,EnclosedEmptyBlockIsBoxedButEdgeOpeningIsBlank){KakuroBoard b;b.rows=5;b.columns=5;for(int r=0;r<5;++r)for(int c=0;c<5;++c)b.playable[KakuroBoard::idx(r,c)]=1;b.playable[KakuroBoard::idx(2,2)]=0;b.playable[KakuroBoard::idx(0,0)]=0;b.playable[KakuroBoard::idx(0,1)]=0;auto out=kakuro_ui::outsideBlocks(b);EXPECT_FALSE(out[KakuroBoard::idx(2,2)]);EXPECT_TRUE(out[KakuroBoard::idx(0,0)]);EXPECT_TRUE(out[KakuroBoard::idx(0,1)]);}

TEST(KakuroLayout,BoardsAreCenteredAndHintsStayAbovePalette){
 for(int n:{6,7,8})for(int width:{480,528,600}){
  auto v=kakuro_ui::layout(width,800,n,n,23,60);
  EXPECT_LE(std::abs(v.left-(width-v.left-n*v.cell)),1);
  EXPECT_LE(std::abs((v.top-94)-(v.combinationY-18-v.gridBottom)),1);
  EXPECT_EQ(v.paletteY-v.combinationY,6*23+12);
  EXPECT_GE(v.top,94);
 }
}

TEST(KakuroLayout,HintLinesStayTogetherImmediatelyAboveDigits){
 for(int lines=2;lines<=6;++lines){
  const int y=kakuro_ui::hintBlockTop(660,23,lines);
  EXPECT_EQ(y+lines*23,652);
 }
 EXPECT_EQ(kakuro_ui::hintBlockTop(660,23,2),606);
}
