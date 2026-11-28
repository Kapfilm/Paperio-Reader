#include <gtest/gtest.h>
#include "src/activities/browser/OpdsPendingSearch.h"
TEST(OpdsPendingSearch,ConsumesBeforeReentrantFetch){
 std::string pending="Толстой";int calls=0;
 EXPECT_TRUE(opds::runPendingSearch(pending,true,[&](const std::string& query){
  ++calls;EXPECT_EQ(query,"Толстой");EXPECT_TRUE(pending.empty());
  EXPECT_FALSE(opds::runPendingSearch(pending,true,[&](const std::string&){++calls;}));
 }));EXPECT_EQ(calls,1);
}
TEST(OpdsPendingSearch,KeepsQueryUntilTemplateIsAvailable){
 std::string pending="book";EXPECT_FALSE(opds::runPendingSearch(pending,false,[](const std::string&){FAIL();}));
 EXPECT_EQ(pending,"book");
}
