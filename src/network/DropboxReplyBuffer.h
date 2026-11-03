#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// Bounded HTTP reply collector/ArduinoJson reader. In a TLS body callback a
// growing std::string can need old+new contiguous allocations simultaneously;
// with exceptions disabled, its failed operator new terminates the firmware.
// Small malloc blocks fail explicitly instead. JSON consumes/frees them only
// after the TLS connection has been closed, with no contiguous flattening copy.
// OAuth replies stay in RAM and are never spooled to the SD card.
class DropboxReplyBuffer {
 public:
  using Allocate = void* (*)(size_t);
  using Release = void (*)(void*);
  explicit DropboxReplyBuffer(Allocate allocate = std::malloc, Release release = std::free)
      : allocate_(allocate), release_(release) {}
  ~DropboxReplyBuffer() { clear(); }
  DropboxReplyBuffer(const DropboxReplyBuffer&) = delete;
  DropboxReplyBuffer& operator=(const DropboxReplyBuffer&) = delete;

  bool append(const uint8_t* bytes, size_t length, size_t maximum) {
    if (size_ > maximum || length > maximum - size_) return false;
    while (length > 0) {
      if (!tail_ || tail_->used == sizeof(tail_->bytes)) {
        auto* block = static_cast<Block*>(allocate_(sizeof(Block)));
        if (!block) return false;
        block->next = nullptr;
        block->used = 0;
        if (tail_)
          tail_->next = block;
        else
          head_ = block;
        tail_ = block;
      }
      const size_t count = std::min(length, sizeof(tail_->bytes) - tail_->used);
      std::memcpy(tail_->bytes + tail_->used, bytes, count);
      tail_->used += count;
      size_ += count;
      bytes += count;
      length -= count;
    }
    return true;
  }

  int read() {
    uint8_t byte;
    return readBytes(reinterpret_cast<char*>(&byte), 1) == 1 ? byte : -1;
  }

  size_t readBytes(char* target, size_t length) {
    size_t copied = 0;
    while (head_ && copied < length) {
      const size_t count = std::min(length - copied, head_->used - readOffset_);
      std::memcpy(target + copied, head_->bytes + readOffset_, count);
      copied += count;
      readOffset_ += count;
      size_ -= count;
      if (readOffset_ == head_->used) {
        Block* consumed = head_;
        head_ = head_->next;
        if (!head_) tail_ = nullptr;
        readOffset_ = 0;
        release_(consumed);
      }
    }
    return copied;
  }

  void clear() {
    while (head_) {
      Block* next = head_->next;
      release_(head_);
      head_ = next;
    }
    tail_ = nullptr;
    size_ = 0;
    readOffset_ = 0;
  }

  size_t size() const { return size_; }

 private:
  struct Block {
    Block* next;
    size_t used;
    uint8_t bytes[1024];
  };
  Allocate allocate_;
  Release release_;
  Block* head_ = nullptr;
  Block* tail_ = nullptr;
  size_t size_ = 0;
  size_t readOffset_ = 0;
};
