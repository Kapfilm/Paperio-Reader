#pragma once

#include <array>
#include <string>

namespace word_clock {
constexpr int kRows = 17;
constexpr int kColumns = 18;
struct Span { int row = 0; int column = 0; int length = 0; };
struct Frame {
  std::array<Span, 8> spans{};
  int spanCount = 0;
  int minuteDots = 0;
};
namespace detail {
enum Token {
  EXACT, HOUR, HOURS_FEW, HOURS_MANY, FIVE, TEN, MINUTES, TWENTY, HALF,
  QUARTER, TO, FIVE_TO, TWENTY_TO, TEN_TO, QUARTER_TO, ONE, TWO,
  THREE, FOUR, FIVE_HOUR, SIX, SEVEN, EIGHT, NINE, TEN_HOUR, ELEVEN, TWELVE,
  FIRST, SECOND, THIRD, FIFTH, FOURTH, SIXTH, SEVENTH, EIGHTH, NINTH, TENTH, ELEVENTH, TWELFTH, COUNT
};
inline int letters(const std::string& text) {
  int count = 0;
  for (unsigned char c : text) if ((c & 0xc0) != 0x80) ++count;
  return count;
}
struct Layout {
  std::array<std::string, kRows> rows{};
  std::array<Span, COUNT> positions{};
  Layout() {
    const auto put = [&](Token token, int row, const char* text) {
      const int column = letters(rows[row]);
      positions[token] = {row, column, letters(text)};
      rows[row] += text;
    };
    put(EXACT,0,"РОВНО");
    put(TO,0,"БЕЗ");
    put(TWENTY_TO,0,"ДВАДЦАТИ");
    put(FIVE_TO,1,"ПЯТИ");
    put(TEN_TO,1,"ДЕСЯТИ");
    put(QUARTER_TO,1,"ЧЕТВЕРТИ");
    put(TWENTY,2,"ДВАДЦАТЬ");
    put(FIVE,2,"ПЯТЬ");
    put(TEN,2,"ДЕСЯТЬ");
    put(MINUTES,3,"МИНУТ");
    put(QUARTER,3,"ЧЕТВЕРТЬ");
    put(HALF,4,"ПОЛОВИНА");
    put(ONE,4,"ОДИН");
    put(TWO,4,"ДВА");
    put(THREE,4,"ТРИ");
    put(FOUR,5,"ЧЕТЫРЕ");
    put(FIVE_HOUR,5,"ПЯТЬ");
    put(SIX,5,"ШЕСТЬ");
    put(SEVEN,6,"СЕМЬ");
    put(EIGHT,6,"ВОСЕМЬ");
    put(NINE,6,"ДЕВЯТЬ");
    put(TEN_HOUR,7,"ДЕСЯТЬ");
    put(ELEVEN,7,"ОДИННАДЦАТЬ");
    put(TWELVE,8,"ДВЕНАДЦАТЬ");
    put(HOUR,8,"ЧАС");
    put(HOURS_FEW,8,"ЧАСА");
    put(HOURS_MANY,9,"ЧАСОВ");
    put(FIRST,9,"ПЕРВОГО");
    put(SECOND,10,"ВТОРОГО");
    put(THIRD,10,"ТРЕТЬЕГО");
    put(FOURTH,11,"ЧЕТВЁРТОГО");
    put(FIFTH,11,"ПЯТОГО");
    put(SIXTH,12,"ШЕСТОГО");
    put(SEVENTH,12,"СЕДЬМОГО");
    put(EIGHTH,13,"ВОСЬМОГО");
    put(NINTH,13,"ДЕВЯТОГО");
    put(TENTH,14,"ДЕСЯТОГО");
    put(ELEVENTH,15,"ОДИННАДЦАТОГО");
    put(TWELFTH,16,"ДВЕНАДЦАТОГО");
    // Fixed filler letters keep the grid stable across all times and wake-ups.
    const char* filler[] = {"А","Е","И","О","К","Л","М","Н","Р","С","Т","У"};
    for (int r = 0; r < kRows; ++r) {
      while (letters(rows[r]) < kColumns) rows[r] += filler[(letters(rows[r]) + r * 7) % 12];
    }
  }
};
inline const Layout& layout() { static const Layout value; return value; }
}  // namespace detail
inline const std::array<std::string, kRows>& rows() { return detail::layout().rows; }

// Five-minute phrases plus zero to four extra minute dots; hours use 12-hour speech.
inline Frame at(int hour, int minute) {
  using namespace detail;
  hour = ((hour % 24) + 24) % 24;
  minute = ((minute % 60) + 60) % 60;
  Frame frame;
  frame.minuteDots = minute % 5;
  const auto add = [&](Token token) { frame.spans[frame.spanCount++] = layout().positions[token]; };
  constexpr Token cardinals[] = {TWELVE, ONE, TWO, THREE, FOUR, FIVE_HOUR, SIX, SEVEN, EIGHT, NINE, TEN_HOUR, ELEVEN};
  constexpr Token ordinals[] = {TWELFTH, FIRST, SECOND, THIRD, FOURTH, FIFTH, SIXTH, SEVENTH, EIGHTH, NINTH, TENTH, ELEVENTH};
  const int step = minute / 5;
  const int next = (hour + 1) % 12;
  if (step == 0) {
    add(EXACT); add(cardinals[hour % 12]);
    add(hour % 12 == 1 ? HOUR : (hour % 12 >= 2 && hour % 12 <= 4 ? HOURS_FEW : HOURS_MANY));
  } else if (step <= 6) {
    if (step == 3) add(QUARTER);
    else if (step == 6) add(HALF);
    else {
      if (step >= 4) add(TWENTY);
      if (step == 1 || step == 5) add(FIVE);
      if (step == 2) add(TEN);
      add(MINUTES);
    }
    add(ordinals[next]);
  } else {
    add(TO);
    if (step == 7 || step == 8) add(TWENTY_TO);
    if (step == 7 || step == 11) add(FIVE_TO);
    if (step == 9) add(QUARTER_TO);
    if (step == 10) add(TEN_TO);
    if (step != 9) add(MINUTES);
    add(cardinals[next]);
  }
  return frame;
}
}  // namespace word_clock
