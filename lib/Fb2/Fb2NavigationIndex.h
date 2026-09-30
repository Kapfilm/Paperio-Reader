#pragma once
#include <HalStorage.h>
#include <cstdint>
#include <string>

// Additive, disposable navigation cache. All storage and working memory are bounded.
class Fb2NavigationIndex {
 public:
  bool open(const std::string& sectionsPath, const std::string& indexPath, uint8_t sectionVersion);
  static bool build(const std::string& sectionsPath, const std::string& indexPath, uint8_t sectionVersion);
  bool offsetForChapter(uint32_t chapter, uint32_t& offset);
  int chapterForId(const std::string& id);
  bool good() const { return good_; }
  uint32_t chapterCount() const { return count_; }
 private:
  bool good_ = false;
  HalFile sections_, index_;
  uint32_t sourceSize_ = 0, count_ = 0, nodes_ = 0, bucketsOffset_ = 0, nodesOffset_ = 0;
};
