#include <ChapterXPathIndexer.h>
#include <ProgressMapper.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <limits>

namespace {
// Stub only the expensive XPath indexer. The code under test is the production
// ProgressMapper.cpp, including its percentage fallback and reconciliation.
struct XPathResolution {
  bool found = false;
  bool exact = false;
  float intra = 0;
};
XPathResolution resolution;

class ProgressMapperTest : public ::testing::Test {
 protected:
  void SetUp() override { resolution = {}; }
  const std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  static constexpr const char* xpath = "/body/DocFragment[2]/body/p[7]/text().4";
};
}  // namespace

std::string ChapterXPathIndexer::findXPathForProgress(const std::shared_ptr<Epub>&, int, float) { return ""; }

bool ChapterXPathIndexer::findProgressForXPath(const std::shared_ptr<Epub>&, int, const std::string&, float& intra,
                                               bool& exact, uint16_t* listItem) {
  intra = resolution.intra;
  exact = resolution.exact;
  if (listItem) *listItem = 0;
  return resolution.found;
}

bool ChapterXPathIndexer::tryExtractSpineIndexFromXPath(const std::string& path, int& index) {
  int fragment = 0;
  if (std::sscanf(path.c_str(), "/body/DocFragment[%d]", &fragment) != 1 || fragment < 1) return false;
  index = fragment - 1;
  return true;
}

bool ChapterXPathIndexer::tryExtractParagraphIndexFromXPath(const std::string& path, uint16_t& index) {
  const auto pos = path.find("/p[");
  unsigned paragraph = 0;
  if (pos == std::string::npos || std::sscanf(path.c_str() + pos, "/p[%u]", &paragraph) != 1) return false;
  index = static_cast<uint16_t>(paragraph);
  return paragraph > 0;
}

TEST_F(ProgressMapperTest, ExactElementMatchStillReconcilesWithPercentageInSameChapter) {
  resolution = {true, true, 0.2f};
  // 60% of the book implies 80% of chapter 2, whereas its exact XPath is at 20%.
  const auto position = ProgressMapper::toCrossPoint(epub, {xpath, 0.6f}, 1, 101);
  EXPECT_EQ(position.spineIndex, 1);
  EXPECT_EQ(position.totalPages, 101);
  EXPECT_EQ(position.pageNumber, 80);
  EXPECT_TRUE(position.hasParagraphIndex);
  EXPECT_EQ(position.paragraphIndex, 7);
}

TEST_F(ProgressMapperTest, ApproximateXPathStillReconcilesWithPercentage) {
  resolution = {true, false, 0.2f};
  const auto position = ProgressMapper::toCrossPoint(epub, {xpath, 0.6f}, 1, 101);
  EXPECT_EQ(position.spineIndex, 1);
  EXPECT_EQ(position.pageNumber, 80);
}

TEST_F(ProgressMapperTest, PercentageOnlyZeroLandsAtStart) {
  const auto position = ProgressMapper::toCrossPoint(epub, {"", 0.0f}, 0, 101);
  EXPECT_EQ(position.spineIndex, 0);
  EXPECT_EQ(position.pageNumber, 0);
  EXPECT_EQ(position.totalPages, 101);
}

TEST_F(ProgressMapperTest, PercentageOnlyMiddleLandsInMiddleChapter) {
  const auto position = ProgressMapper::toCrossPoint(epub, {"", 0.5f}, 0, 101);
  EXPECT_EQ(position.spineIndex, 1);
  EXPECT_EQ(position.pageNumber, 50);
  EXPECT_EQ(position.totalPages, 101);
}

TEST_F(ProgressMapperTest, PercentageOnlyOneLandsAtLastPage) {
  const auto position = ProgressMapper::toCrossPoint(epub, {"", 1.0f}, 0, 101);
  EXPECT_EQ(position.spineIndex, 2);
  EXPECT_EQ(position.pageNumber, 100);
  EXPECT_EQ(position.totalPages, 101);
}

TEST_F(ProgressMapperTest, LocatorOnlyNaNUsesResolvedXPath) {
  resolution = {true, true, 0.37f};
  bool resolvedExactAnchor = false;
  const auto position = ProgressMapper::toCrossPoint(epub, {xpath, std::numeric_limits<float>::quiet_NaN()}, 1, 101,
                                                     &resolvedExactAnchor);
  EXPECT_EQ(position.spineIndex, 1);
  EXPECT_EQ(position.pageNumber, 37);
  EXPECT_EQ(position.totalPages, 101);
  EXPECT_TRUE(position.hasParagraphIndex);
  EXPECT_EQ(position.paragraphIndex, 7);
  EXPECT_TRUE(resolvedExactAnchor);
}

TEST_F(ProgressMapperTest, LocatorOnlyNaNPreservesChapterWhenIndexerUnavailable) {
  bool resolvedExactAnchor = true;
  const auto position = ProgressMapper::toCrossPoint(epub, {xpath, std::numeric_limits<float>::quiet_NaN()}, 1, 101,
                                                     &resolvedExactAnchor);
  EXPECT_EQ(position.spineIndex, 1);
  EXPECT_EQ(position.pageNumber, 0);
  EXPECT_TRUE(position.hasParagraphIndex);
  EXPECT_EQ(position.paragraphIndex, 7);
  EXPECT_FALSE(resolvedExactAnchor) << "a paragraph predicate alone is not a resolved content anchor";
}

TEST_F(ProgressMapperTest, LocatorOnlyApproximateXPathIsNotAnExactAnchor) {
  resolution = {true, false, 0.37f};
  bool resolvedExactAnchor = true;
  const auto position = ProgressMapper::toCrossPoint(epub, {xpath, std::numeric_limits<float>::quiet_NaN()}, 1, 101,
                                                     &resolvedExactAnchor);
  EXPECT_EQ(position.spineIndex, 1);
  EXPECT_EQ(position.pageNumber, 37);
  EXPECT_FALSE(resolvedExactAnchor);
}

TEST_F(ProgressMapperTest, UnresolvedTextOffsetsDoNotBecomeExactAnchors) {
  for (const char* suffix : {".10", ".0"}) {
    SCOPED_TRACE(suffix);
    const std::string path = std::string("/body/DocFragment[2]/body/text()[1]") + suffix;
    bool resolvedExactAnchor = true;
    const auto position = ProgressMapper::toCrossPoint(epub, {path, std::numeric_limits<float>::quiet_NaN()}, 1, 101,
                                                       &resolvedExactAnchor);
    EXPECT_EQ(position.spineIndex, 1);
    EXPECT_EQ(position.pageNumber, 0);
    EXPECT_FALSE(resolvedExactAnchor) << "fallback chapter selection must not authorize a locator-only sync";
  }
}

TEST_F(ProgressMapperTest, InvalidLocatorClearsExactAnchorOutput) {
  bool resolvedExactAnchor = true;
  ProgressMapper::toCrossPoint(epub, {"invalid.10", std::numeric_limits<float>::quiet_NaN()}, 1, 101,
                               &resolvedExactAnchor);
  EXPECT_FALSE(resolvedExactAnchor);
  resolvedExactAnchor = true;
  ProgressMapper::toCrossPoint(nullptr, {xpath, std::numeric_limits<float>::quiet_NaN()}, 1, 101, &resolvedExactAnchor);
  EXPECT_FALSE(resolvedExactAnchor) << "early-return paths must initialize the output";
}

// ForwardMapper emits bare paths for nested text. Matching a long div exactly
// identifies the element, but not the reader's position inside that element.
TEST_F(ProgressMapperTest, LongNestedBlockDoesNotJumpToItsStart) {
  resolution = {true, true, 0.0f};
  const auto position = ProgressMapper::toCrossPoint(epub, {"/body/DocFragment[2]/body/div[1]", 0.6f}, 1, 101);
  EXPECT_EQ(position.spineIndex, 1);
  EXPECT_EQ(position.pageNumber, 80);
}
