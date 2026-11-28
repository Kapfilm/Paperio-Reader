#include <gtest/gtest.h>
#include <I18n.h>
#include "activities/games/casefiles/CaseFilesText.h"
#include "activities/games/casefiles/CaseFilesData.h"
using namespace casefiles;
namespace {
Puzzle example(int theme=0){Puzzle p;p.theme=theme;p.k=4;p.n=5;for(int g=0;g<4;++g){p.groups[g].kind=g;for(int i=0;i<5;++i){p.groups[g].items[i]=i;p.groups[g].owner[i]=i;p.groups[g].slot[i]=i;}}return p;}
Clue relation(char type,Entity a,Entity b){Clue c;c.type=type;c.count=2;c.entities[0]=a;c.entities[1]=b;return c;}
}
TEST(CaseText, RussianReadsAsSentencesAndKeepsNamesFirst){caseRussian=true;auto p=example();auto c=relation('=',{0,0},{1,1});EXPECT_EQ(clueText(p,c),"Ада живёт на 2-м этаже.");std::swap(c.entities[0],c.entities[1]);EXPECT_EQ(clueText(p,c),"Ада живёт на 2-м этаже.");c.type='!';EXPECT_EQ(clueText(p,c),"Ада живёт не на 2-м этаже.");}
TEST(CaseText, RussianRelativeSubjectGetsClosingComma){caseRussian=true;auto p=example();auto c=relation('=',{3,4},{2,0});EXPECT_EQ(clueText(p,c),"Гость, потерявший ключ, приезжает в 18:00.");}
TEST(CaseText, OrderedCluesUseHoursFloorsAndPlaces){caseRussian=true;auto p=example();auto c=relation('+',{0,0},{0,1});c.group=2;c.difference=2;EXPECT_EQ(clueText(p,c),"Ада приезжает на 2 часа позже, чем Бруно.");c.group=1;c.difference=1;EXPECT_EQ(clueText(p,c),"Ада живёт на 1 этаж выше, чем Бруно.");p.theme=2;c.difference=5;EXPECT_EQ(clueText(p,c),"Альба занимает место на 5 мест ниже, чем Бен.");}
TEST(CaseText, XorAndPairStateTheirExactMeaning){caseRussian=true;auto p=example();Clue c;c.type='e';c.count=3;c.entities={Entity{0,0},Entity{1,1},Entity{3,4},Entity{}};EXPECT_EQ(clueText(p,c),"Верно ровно одно из двух: Ада живёт на 2-м этаже; либо Ада теряет ключ.");c.type='o';c.count=4;c.entities={Entity{0,0},Entity{0,1},Entity{1,1},Entity{3,4}};EXPECT_EQ(clueText(p,c),"Есть два разных человека: Ада и Бруно. Один живёт на 2-м этаже, а другой теряет ключ.");}
TEST(CaseText, AllCluesInBothLanguagesContainNoUnexpandedTemplates){for(bool ru:{false,true}){caseRussian=ru;for(int l=0;l<3;++l)for(int i=0;i<200;++i){Puzzle p;ASSERT_TRUE(p.parse(bankLine(l,i)));for(const auto&c:p.clues){auto text=clueText(p,c);EXPECT_FALSE(text.empty());EXPECT_EQ(text.find('%'),std::string::npos)<<text;EXPECT_EQ(text.find("Имя:"),std::string::npos)<<text;EXPECT_EQ(text.find("значение"),std::string::npos)<<text;EXPECT_EQ(text.back(),'.');}}}}

TEST(CaseText, RussianTuesdayAndComparisonDescription){caseRussian=true;auto p=example(3);auto c=relation('=',{0,0},{1,1});EXPECT_EQ(clueText(p,c),"Арда приходит во вторник.");p=example();c=relation('<',{0,0},{3,4});c.group=2;EXPECT_EQ(clueText(p,c),"Ада приезжает раньше, чем гость, потерявший ключ.");}

TEST(CaseText, RelativeDescriptionsInPairKeepSeparatingComma){caseRussian=true;auto p=example();Clue c;c.type='o';c.count=4;c.entities={Entity{3,0},Entity{3,4},Entity{1,0},Entity{1,1}};EXPECT_EQ(clueText(p,c),"Есть два разных человека: гость, потерявший шарф, и гость, потерявший ключ. Один живёт на 1-м этаже, а другой живёт на 2-м этаже.");}
