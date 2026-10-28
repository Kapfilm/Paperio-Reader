#pragma once

#include <cstdint>
#include <string>
#include <vector>

// A three-chapter book with equal uncompressed sizes: only the metadata used by
// ProgressMapper is represented, avoiding EPUB storage and parsing dependencies.
class Epub {
 public:
  struct TocItem {
    std::string title;
  };
  int getSpineItemsCount() const { return static_cast<int>(cumulativeSizes.size()); }
  size_t getBookSize() const { return cumulativeSizes.empty() ? 0 : cumulativeSizes.back(); }
  size_t getCumulativeSpineItemSize(int index) const { return cumulativeSizes.at(index); }
  float calculateProgress(int index, float intra) const {
    const size_t previous = index > 0 ? cumulativeSizes.at(index - 1) : 0;
    return (previous + (cumulativeSizes.at(index) - previous) * intra) / getBookSize();
  }
  int getTocIndexForSpineIndex(int index) const { return index; }
  TocItem getTocItem(int) const { return {"Chapter"}; }

  std::vector<size_t> cumulativeSizes{1000, 2000, 3000};
};
