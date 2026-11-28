#include <gtest/gtest.h>
#include <util/SectionHeading.h>
#include <vector>

namespace {
struct Entry { std::string title; int level; };
std::string heading(const std::vector<Entry>& toc, int index) {
  return status_bar::sectionHeading(index, int(toc.size()), [&](int i) { return toc[i]; }, "Библия");
}
TEST(SectionHeading, BibleChaptersUseCurrentBookAndNeverPreviousSibling) {
  const std::vector<Entry> toc{{"Библия",0},{"Новый Завет",1},{"Евангелие от Луки",2},
      {"Глава 1",3},{"Глава 2",3},{"Евангелие от Иоанна",2},{"Глава 1",3}};
  EXPECT_EQ(heading(toc,2),"Евангелие от Луки");
  EXPECT_EQ(heading(toc,3),"Евангелие от Луки");
  EXPECT_EQ(heading(toc,4),"Евангелие от Луки");
  EXPECT_EQ(heading(toc,5),"Евангелие от Иоанна");
  EXPECT_EQ(heading(toc,6),"Евангелие от Иоанна");
}
TEST(SectionHeading, FlatTocFallsBackToCurrentHeading) {
  const std::vector<Entry> toc{{"Введение",0},{"Дорога домой",0}};
  EXPECT_EQ(heading(toc,1),"Дорога домой");
  EXPECT_TRUE(heading(toc,-1).empty());
  EXPECT_TRUE(heading(toc,2).empty());
}
TEST(SectionHeading, EmptyAncestorAndOverallBookTitleAreSkipped) {
  const std::vector<Entry> toc{{"Библия",0},{"Лука",1},{"",2},{"Глава 21",3}};
  EXPECT_EQ(heading(toc,3),"Лука");
  EXPECT_TRUE(heading({{"Библия",0}},0).empty());
}
}
