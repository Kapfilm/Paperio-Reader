#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>

#include "Fb2Types.h"

// Two bounded write-back pages for initially empty scratch files. Keeping the
// records and strings independently cached avoids evicting SdFat's shared sector
// cache for every tiny title/record while a third file supplies the source XML.
class BufferedFb2ScanStorage final : public Fb2ScanStorage {
 public:
  static constexpr size_t PAGE_BYTES = 4096;

  explicit BufferedFb2ScanStorage(std::shared_ptr<Fb2ScanStorage> backing)
      : backing_(std::move(backing)), good_(backing_ != nullptr) {}

  bool good() const { return good_; }

  bool read(bool strings, uint32_t offset, void* data, size_t size) override {
    auto& page = pages_[strings];
    if (!good_ || offset > page.length || size > page.length - offset) return false;
    auto* target = static_cast<uint8_t*>(data);
    while (size) {
      if (!load(strings, offset)) return false;
      const size_t inPage = offset - page.start;
      const size_t count = std::min(size, PAGE_BYTES - inPage);
      std::memcpy(target, page.bytes.data() + inPage, count);
      target += count;
      offset += static_cast<uint32_t>(count);
      size -= count;
    }
    return true;
  }

  bool write(bool strings, uint32_t offset, const void* data, size_t size) override {
    auto& page = pages_[strings];
    // Scratch files grow by append. Do not create holes that the firmware's
    // seek-before-write backend could not represent on FAT.
    if (!good_ || offset > page.length || size > UINT32_MAX - offset) return false;
    const auto* source = static_cast<const uint8_t*>(data);
    while (size) {
      if (!load(strings, offset)) return false;
      const size_t inPage = offset - page.start;
      const size_t count = std::min(size, PAGE_BYTES - inPage);
      std::memcpy(page.bytes.data() + inPage, source, count);
      page.dirty = true;
      source += count;
      offset += static_cast<uint32_t>(count);
      page.length = std::max(page.length, offset);
      size -= count;
    }
    return true;
  }

  // Call before accepting the completed scan/package, so delayed I/O failures
  // cannot turn into a successful conversion. Destruction only discards cache.
  bool flush() {
    if (!good_) return false;
    return flushPage(false) && flushPage(true);
  }

 private:
  struct Page {
    std::array<uint8_t, PAGE_BYTES> bytes{};
    uint32_t start = 0;
    uint32_t length = 0;
    uint32_t persistedLength = 0;
    bool valid = false;
    bool dirty = false;
  };

  bool flushPage(bool strings) {
    auto& page = pages_[strings];
    if (!good_) return false;
    if (!page.dirty) return true;
    const size_t count = std::min<size_t>(PAGE_BYTES, page.length - page.start);
    if (!backing_->write(strings, page.start, page.bytes.data(), count)) return good_ = false;
    page.persistedLength = std::max(page.persistedLength, page.start + static_cast<uint32_t>(count));
    page.dirty = false;
    return true;
  }

  bool load(bool strings, uint32_t offset) {
    auto& page = pages_[strings];
    const uint32_t start = offset - offset % PAGE_BYTES;
    if (page.valid && page.start == start) return true;
    if (!flushPage(strings)) return false;
    page.valid = false;
    if (start < page.persistedLength) {
      const size_t count = std::min<size_t>(PAGE_BYTES, page.persistedLength - start);
      if (!backing_->read(strings, start, page.bytes.data(), count)) return good_ = false;
    }
    page.start = start;
    page.valid = true;
    return true;
  }

  std::shared_ptr<Fb2ScanStorage> backing_;
  std::array<Page, 2> pages_{};
  bool good_;
};
