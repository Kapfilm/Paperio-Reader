#include <gtest/gtest.h>
#include <algorithm>
#include <util/WordClock.h>

namespace {
std::string slice(const std::string& text, int column, int length) {
  size_t first=0, last=0; int character=0;
  for (size_t i=0;i<text.size();++i) {
    if ((static_cast<unsigned char>(text[i]) & 0xc0) == 0x80) continue;
    if (character == column) first=i;
    if (character == column+length) { last=i; break; }
    ++character;
  }
  if (!last) last=text.size();
  return text.substr(first,last-first);
}
std::string phrase(int hour, int minute) {
  auto frame=word_clock::at(hour,minute); std::string value;
  std::sort(frame.spans.begin(), frame.spans.begin()+frame.spanCount, [](const auto& a,const auto& b) { return a.row < b.row || (a.row == b.row && a.column < b.column); });
  for(int i=0;i<frame.spanCount;++i) {
    const auto span=frame.spans[i];
    if(i) value+=' ';
    value+=slice(word_clock::rows()[span.row],span.column,span.length);
  }
  return value;
}
TEST(WordClock, EveryMinuteFitsStableGridAndRemainderDots) {
  for(const auto& row:word_clock::rows()) EXPECT_EQ(word_clock::detail::letters(row),word_clock::kColumns);
  for(int h=0;h<24;++h) for(int m=0;m<60;++m) {
    const auto frame=word_clock::at(h,m);
    ASSERT_GT(frame.spanCount,0); ASSERT_LE(frame.spanCount,8); EXPECT_EQ(frame.minuteDots,m%5);
    for(int i=0;i<frame.spanCount;++i) {
      const auto s=frame.spans[i]; EXPECT_GE(s.row,0); EXPECT_LT(s.row,word_clock::kRows);
      EXPECT_GE(s.column,0); EXPECT_GT(s.length,0); EXPECT_LE(s.column+s.length,word_clock::kColumns);
      if (i) {
        const auto previous=frame.spans[i-1];
        EXPECT_LT(previous.row*word_clock::kColumns+previous.column, s.row*word_clock::kColumns+s.column);
      }
    }
  }
}
TEST(WordClock, RussianGrammarAndMidnightRollover) {
  EXPECT_EQ(phrase(1,0),"РОВНО ОДИН ЧАС");
  EXPECT_EQ(phrase(2,0),"РОВНО ДВА ЧАСА");
  EXPECT_EQ(phrase(5,0),"РОВНО ПЯТЬ ЧАСОВ");
  EXPECT_EQ(phrase(0,5),"ПЯТЬ МИНУТ ПЕРВОГО");
  EXPECT_EQ(phrase(2,10),"ДЕСЯТЬ МИНУТ ТРЕТЬЕГО");
  EXPECT_EQ(phrase(2,20),"ДВАДЦАТЬ МИНУТ ТРЕТЬЕГО");
  EXPECT_EQ(phrase(2,55),"БЕЗ ПЯТИ МИНУТ ТРИ");
  EXPECT_EQ(phrase(2,15),"ЧЕТВЕРТЬ ТРЕТЬЕГО");
  EXPECT_EQ(phrase(2,25),"ДВАДЦАТЬ ПЯТЬ МИНУТ ТРЕТЬЕГО");
  EXPECT_EQ(phrase(2,30),"ПОЛОВИНА ТРЕТЬЕГО");
  EXPECT_EQ(phrase(2,35),"БЕЗ ДВАДЦАТИ ПЯТИ МИНУТ ТРИ");
  EXPECT_EQ(phrase(2,40),"БЕЗ ДВАДЦАТИ МИНУТ ТРИ");
  EXPECT_EQ(phrase(2,45),"БЕЗ ЧЕТВЕРТИ ТРИ");
  EXPECT_EQ(phrase(2,50),"БЕЗ ДЕСЯТИ МИНУТ ТРИ");
  EXPECT_EQ(phrase(23,55),"БЕЗ ПЯТИ МИНУТ ДВЕНАДЦАТЬ");
}
TEST(WordClock, ExtraMinutesDoNotChangeWords) {
  for(int h=0;h<24;++h) for(int m=0;m<60;m+=5)
    for(int delta=1;delta<5;++delta) EXPECT_EQ(phrase(h,m),phrase(h,m+delta));
}
}
