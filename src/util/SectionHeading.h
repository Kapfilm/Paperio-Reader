#pragma once

#include <string>

namespace status_bar {
// TOC is in document order. A container is its own section heading;
// a leaf uses its nearest named ancestor (never a preceding sibling).
// Accessor keeps the helper usable with the on-disk TOC without copying it.
template <typename EntryAt>
std::string sectionHeading(int current, int count, const EntryAt& entryAt, const std::string& bookTitle) {
  if (current < 0 || current >= count) return {};
  const auto active = entryAt(current);
  if (current + 1 < count && entryAt(current + 1).level > active.level &&
      !active.title.empty() && active.title != bookTitle) return active.title;
  int level = active.level;
  for (int i = current - 1; i >= 0 && level > 0; --i) {
    const auto ancestor = entryAt(i);
    if (ancestor.level >= level) continue;
    level = ancestor.level;
    if (!ancestor.title.empty() && ancestor.title != bookTitle) return ancestor.title;
  }
  return active.title == bookTitle ? std::string() : active.title;
}
}  // namespace status_bar
