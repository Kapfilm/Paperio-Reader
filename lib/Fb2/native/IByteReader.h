// Reader Core — allocation-free byte-stream contract.
//
// Adapted from inkMOD v1.1.8. Kept inside the FB2 library so the parser,
// ZIP reader, and host tests do not depend on inkMOD's src/reader tree.
#pragma once

#include <cstddef>
#include <cstdint>

class IByteReader {
 public:
  virtual ~IByteReader() = default;

  // Reads at most len bytes. Returns 0 at EOF or on a recoverable read error.
  virtual size_t read(void* buf, size_t len) = 0;

  // Absolute seek from the start of the stream.
  virtual bool seek(uint64_t pos) = 0;

  virtual uint64_t tell() const = 0;
  virtual uint64_t size() const = 0;

  uint64_t position() const { return tell(); }
  bool eof() const { return tell() >= size(); }
};
