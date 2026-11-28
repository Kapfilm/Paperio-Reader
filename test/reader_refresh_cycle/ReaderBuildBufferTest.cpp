#include <gtest/gtest.h>
#include "src/activities/reader/ReaderBuildBuffer.h"
#include <string>
namespace {
struct Renderer {
 bool x3=false,resident=true,fast=false; char red='A',displayed='B',write='C';
 uint8_t scratch[8]{};std::string calls;
 bool hasSecondaryBuffer(){return resident;}
 bool isX3(){return x3;}
 void syncRedRamFromFrameBuffer(){calls+='S';red=resident?displayed:write;}
 uint8_t* borrowSecondaryBuffer(size_t* n){calls+='B';resident=false;*n=8;return scratch;}
 void setSingleBufferFastDiff(bool enabled){fast=enabled;}
};
TEST(ReaderBuildBuffer,SeedsDisplayedPageBeforeScratchCanOverwriteIt){
 Renderer r;size_t n=0;ASSERT_NE(reader_build::borrowBackgroundBuffer(r,&n),nullptr);
 EXPECT_EQ(r.calls,"SB");EXPECT_EQ(r.red,'B');EXPECT_EQ(r.write,'C');EXPECT_EQ(n,8u);
 r.scratch[0]=0xEE; // indexing destroys the host copy, not the controller baseline
 reader_build::resumeBorrowedBuild(r);EXPECT_TRUE(r.fast);EXPECT_EQ(r.red,'B');
 r.write='D';EXPECT_EQ(r.red,'B'); // next FAST transition is B -> D, not A -> D
}
TEST(ReaderBuildBuffer,MissingResidentBufferCannotSeedFromPrerender){
 Renderer r;r.resident=false;size_t n=0;
 EXPECT_EQ(reader_build::borrowBackgroundBuffer(r,&n),nullptr);EXPECT_TRUE(r.calls.empty());EXPECT_EQ(r.red,'A');
}
TEST(ReaderBuildBuffer,X3KeepsControllerBaselineWithoutExtraSeed){
 Renderer r;r.x3=true;size_t n=0;ASSERT_NE(reader_build::borrowBackgroundBuffer(r,&n),nullptr);EXPECT_EQ(r.calls,"B");
}
TEST(ReaderBuildBuffer,HiddenPopupDoesNotDisableFastBorrowedPageTurns){
 Renderer r;r.resident=false;reader_build::resumeBorrowedBuild(r);EXPECT_TRUE(r.fast);EXPECT_TRUE(r.calls.empty());
}
}

TEST(ReaderBuildBuffer,X4KeepsDisplayedBufferAndShortIdleSlices){
 EXPECT_FALSE(reader_build::allowBackgroundBorrow(true));
 EXPECT_TRUE(reader_build::allowBackgroundBorrow(false));
 EXPECT_EQ(reader_build::backgroundSliceBudget(true),8u);
 EXPECT_EQ(reader_build::backgroundSliceBudget(false),40u);
}
