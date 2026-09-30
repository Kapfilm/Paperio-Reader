#pragma once
#include <HalStorage.h>
#include <cstdint>
#include <memory>
#include <string>

// Disposable SD index for anchors inside virtual chapters. Lookup uses constant RAM.
class Fb2AnchorIndex {
 public:
  bool open(const std::string& sectionsPath, const std::string& indexPath, uint8_t version);
  int chapterForId(const std::string& id);
  bool good() const { return good_; }
 private:
  HalFile file_;
  bool good_ = false;
  uint32_t size_ = 0, nodes_ = 0, chapterBound_ = 0;
};

// Sequential builder: 16 KiB bucket heads plus a 1 KiB write buffer, on the heap.
class Fb2AnchorIndexWriter {
 public:
  Fb2AnchorIndexWriter();
  ~Fb2AnchorIndexWriter();
  Fb2AnchorIndexWriter(const Fb2AnchorIndexWriter&) = delete;
  Fb2AnchorIndexWriter& operator=(const Fb2AnchorIndexWriter&) = delete;
  bool begin(const std::string& sectionsPath, const std::string& indexPath, uint8_t version);
  bool add(const std::string& id, uint32_t chapter);
  bool finish();
  bool good() const;
 private:
  struct State;
  std::unique_ptr<State> state_;
};
