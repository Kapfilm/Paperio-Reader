#pragma once
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace fb2_sync {
struct Locator {
  uint16_t body = 1;
  std::vector<uint16_t> sections;
  uint16_t paragraph = 0;
  int legacyOrdinal = 0;
  bool sectionStart = false;
};

inline bool readIndex(const std::string& path, size_t& at, uint16_t& value) {
  if (at >= path.size() || path[at] != '[') return false;
  size_t i = at + 1;
  uint32_t parsed = 0;
  const size_t first = i;
  while (i < path.size() && std::isdigit(static_cast<unsigned char>(path[i]))) {
    parsed = parsed * 10 + path[i++] - '0';
    if (parsed > UINT16_MAX) return false;
  }
  if (i == first || i >= path.size() || path[i] != ']' || parsed == 0) return false;
  value = static_cast<uint16_t>(parsed);
  at = i + 1;
  return true;
}

inline bool parseLocator(std::string path, Locator& out) {
  for (char& c : path) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  Locator result;
  size_t at = path.find("/fictionbook");
  if (at != std::string::npos) {
    at = path.find("/body", at + 12);
    if (at == std::string::npos) return false;
    at += 5;
    if (at < path.size() && path[at] == '[' && !readIndex(path, at, result.body)) return false;
    while (path.compare(at, 8, "/section") == 0) {
      if (result.sections.size() == 32) return false;
      at += 8;
      uint16_t index = 0;
      if (!readIndex(path, at, index)) return false;
      result.sections.push_back(index);
    }
    if (result.sections.empty()) return false;
  } else {
    at = path.find("/docfragment[");
    if (at == std::string::npos) return false;
    at += 12;
    uint16_t ordinal = 0;
    if (!readIndex(path, at, ordinal)) return false;
    result.legacyOrdinal = ordinal;
    if (path.compare(at, 5, "/body") == 0) at += 5;
    if (path.compare(at, 8, "/section") == 0) at += 8;
  }
  // Only direct source-section p[N] uses the source paragraph coordinate.
  // A cite/epigraph/title p[N] has its own sibling index and must use percentage fallback.
  if (path.compare(at, 3, "/p[") == 0) {
    at += 2;
    if (!readIndex(path, at, result.paragraph)) return false;
  } else {
    const std::string tail = path.substr(at);
    result.sectionStart = tail == ".0" || tail == "/text().0" || tail == "/text()[1].0";
  }
  out = result;
  return true;
}

inline std::string makeLocator(const std::string& section, uint16_t paragraph, int page) {
  if (paragraph > 0) return section + "/p[" + std::to_string(paragraph) + "]/text().0";
  // Zero means no source paragraph, e.g. a heading. Never invent p[1].
  return section + (page == 0 ? ".0" : "");
}

template <typename Position>
bool applyLocator(const Locator& locator, int first, int last, bool hasPercentage, Position& position) {
  if (first < 0 || last < first) return false;
  if (!hasPercentage && !locator.sectionStart && locator.paragraph == 0) return false;
  position.hasParagraphIndex = false;
  position.paragraphIndex = 0;
  position.hasListItemIndex = false;
  position.listItemIndex = 0;
  if (position.spineIndex < first || position.spineIndex > last || locator.sectionStart) {
    position.spineIndex = first;
    position.pageNumber = 0;
    position.totalPages = 0;
  }
  if (locator.paragraph > 0) {
    position.paragraphIndex = locator.paragraph;
    position.hasParagraphIndex = true;
  }
  return true;
}
}  // namespace fb2_sync
