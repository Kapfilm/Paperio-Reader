#include <Fb2ParagraphIndex.h>
#include <Fb2SyncPosition.h>
#include <gtest/gtest.h>

namespace {
struct Position {
  int spineIndex = 2, pageNumber = 19, totalPages = 60;
  uint16_t paragraphIndex = 0, listItemIndex = 0;
  bool hasParagraphIndex = false, hasListItemIndex = false;
};
const std::string section = "/FictionBook/body[1]/section[3]/section[2]";
}  // namespace

TEST(Fb2SyncPosition, BeginningWithoutParagraphDoesNotInventFirstParagraph) {
  const auto sent = fb2_sync::makeLocator(section, 0, 0);
  EXPECT_EQ(sent, section + ".0");
  fb2_sync::Locator received;
  ASSERT_TRUE(fb2_sync::parseLocator(sent, received));
  EXPECT_EQ(received.sections, (std::vector<uint16_t>{3, 2}));
  EXPECT_EQ(received.paragraph, 0);
  EXPECT_TRUE(received.sectionStart);
  Position local;
  ASSERT_TRUE(fb2_sync::applyLocator(received, 2, 2, true, local));
  EXPECT_EQ(local.pageNumber, 0);
  EXPECT_FALSE(local.hasParagraphIndex);
}

TEST(Fb2SyncPosition, TrueSourceParagraphSurvivesRoundTrip) {
  fb2_sync::Locator received;
  ASSERT_TRUE(fb2_sync::parseLocator(fb2_sync::makeLocator(section, 27, 4), received));
  EXPECT_EQ(received.paragraph, 27);
  Position local;
  ASSERT_TRUE(fb2_sync::applyLocator(received, 2, 2, true, local));
  EXPECT_TRUE(local.hasParagraphIndex);
  EXPECT_EQ(local.paragraphIndex, 27);
}

TEST(Fb2SyncPosition, UnknownParagraphMidChapterKeepsPercentageEstimate) {
  fb2_sync::Locator received;
  ASSERT_TRUE(fb2_sync::parseLocator(fb2_sync::makeLocator(section, 0, 4), received));
  EXPECT_FALSE(received.sectionStart);
  Position local;
  ASSERT_TRUE(fb2_sync::applyLocator(received, 2, 2, true, local));
  EXPECT_EQ(local.pageNumber, 19);
  EXPECT_FALSE(local.hasParagraphIndex);
}

TEST(Fb2SyncPosition, UnknownParagraphWithoutPercentageCannotApply) {
  fb2_sync::Locator received;
  ASSERT_TRUE(fb2_sync::parseLocator(section, received));
  Position local;
  EXPECT_FALSE(fb2_sync::applyLocator(received, 2, 2, false, local));
  EXPECT_EQ(local.pageNumber, 19);
}

TEST(Fb2SyncPosition, HeadingLocatorWithoutPercentageCanApplyStart) {
  fb2_sync::Locator received;
  ASSERT_TRUE(fb2_sync::parseLocator(section + ".0", received));
  Position local;
  ASSERT_TRUE(fb2_sync::applyLocator(received, 2, 2, false, local));
  EXPECT_EQ(local.pageNumber, 0);
}

TEST(Fb2SyncPosition, CiteParagraphIsNotASiblingSourceParagraph) {
  fb2_sync::Locator received;
  ASSERT_TRUE(fb2_sync::parseLocator(section + "/cite[1]/p[4]/text().0", received));
  EXPECT_EQ(received.paragraph, 0);
  EXPECT_FALSE(received.sectionStart);
  Position local;
  ASSERT_TRUE(fb2_sync::applyLocator(received, 2, 2, true, local));
  EXPECT_EQ(local.pageNumber, 19);
}

TEST(Fb2SyncPosition, LegacyLocatorStillUnderstandsOriginalSectionAndParagraph) {
  fb2_sync::Locator received;
  ASSERT_TRUE(fb2_sync::parseLocator("/body/DocFragment[4]/body/section/p[8]/text().0", received));
  EXPECT_EQ(received.legacyOrdinal, 4);
  EXPECT_EQ(received.paragraph, 8);
}

TEST(Fb2SyncPosition, InvalidOrOverflowedIndicesAreRejected) {
  fb2_sync::Locator received;
  EXPECT_FALSE(fb2_sync::parseLocator("/FictionBook/body[1]/section[0].0", received));
  EXPECT_FALSE(fb2_sync::parseLocator("/FictionBook/body[1]/section[65536].0", received));
  EXPECT_FALSE(fb2_sync::parseLocator(section + "/p[99999999999999]/text().0", received));
  EXPECT_FALSE(fb2_sync::parseLocator("/FictionBook/body[2]/title/p[1]", received));
}

TEST(Fb2SyncPosition, SourceSectionConstrainsDifferentPercentageChapter) {
  fb2_sync::Locator received;
  ASSERT_TRUE(fb2_sync::parseLocator(section + "/p[3]/text().0", received));
  Position local;
  ASSERT_TRUE(fb2_sync::applyLocator(received, 5, 5, true, local));
  EXPECT_EQ(local.spineIndex, 5);
  EXPECT_EQ(local.pageNumber, 0);
  EXPECT_EQ(local.paragraphIndex, 3);
}

TEST(Fb2SyncPosition, AttributeAcceptsOnlyValidSourceCoordinates) {
  EXPECT_EQ(parseFb2SourceParagraph("1"), 1);
  EXPECT_EQ(parseFb2SourceParagraph("65535"), 65535);
  const char* invalidValues[] = {nullptr, "", "0", "-1", "65536", "9999999999999", "2x"};
  for (const char* invalid : invalidValues) {
    EXPECT_EQ(parseFb2SourceParagraph(invalid), 0);
  }
}
