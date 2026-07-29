#include "ClipTextBuilder.h"

#include <algorithm>

namespace ClipTextBuilder {
namespace {
std::string clean(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  for (const char c : input) {
    if (c == '\r' || c == '\n' || c == '\t') {
      if (!out.empty() && out.back() != ' ') out.push_back(' ');
    } else {
      out.push_back(c);
    }
  }
  while (!out.empty() && out.front() == ' ') out.erase(out.begin());
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}
}  // namespace

ClippingResult build(const std::vector<WordRef>& words, const int from, const int to, const int startPage,
                     const int pageCount) {
  ClippingResult result;
  if (from < 0 || to < from || to >= static_cast<int>(words.size())) return result;

  result.text.reserve(256);
  result.sectionPage = static_cast<uint16_t>(startPage + words[from].pageIndex);
  result.endSectionPage = static_cast<uint16_t>(startPage + words[to].pageIndex);
  result.sectionPageCount = static_cast<uint16_t>(std::max(1, pageCount));
  result.startPageWordIndex = words[from].pageWordIndex;
  result.endPageWordIndex = words[to].pageWordIndex;
  result.wordCount = static_cast<uint16_t>(to - from + 1);

  for (int i = from; i <= to; ++i) {
    const std::string word = clean(words[i].text);
    if (word.empty()) continue;
    if (!result.text.empty()) result.text += words[i].paragraphStart ? '\n' : ' ';
    result.text += word;
  }
  return result;
}
}  // namespace ClipTextBuilder
