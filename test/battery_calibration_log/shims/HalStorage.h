#pragma once

#include <fcntl.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace calibration_test {
struct StorageState {
  bool ready = true;
  bool markerPresent = false;
  bool failOpen = false;
  bool failWrite = false;
  bool shortWrite = false;
  bool failClose = false;
  unsigned existsChecks = 0;
  unsigned opens = 0;
  unsigned writes = 0;
  unsigned closes = 0;
  int lastOpenFlags = 0;
  std::string data;
  std::optional<uint64_t> virtualSize;
};
inline StorageState state;
}  // namespace calibration_test

class HalFile {
 public:
  explicit HalFile(bool isOpen) : open(isOpen) {}
  explicit operator bool() const { return open; }
  uint64_t fileSize64() const {
    return calibration_test::state.virtualSize.value_or(calibration_test::state.data.size());
  }
  size_t write(const void* data, size_t length) {
    auto& state = calibration_test::state;
    ++state.writes;
    if (!open || state.failWrite) return 0;
    const size_t written = state.shortWrite && length != 0 ? length - 1 : length;
    state.data.append(static_cast<const char*>(data), written);
    if (state.virtualSize) *state.virtualSize += written;
    return written;
  }
  bool close() {
    if (!open) return false;
    open = false;
    ++calibration_test::state.closes;
    return !calibration_test::state.failClose;
  }

 private:
  bool open;
};

class CalibrationTestStorage {
 public:
  bool ready() const { return calibration_test::state.ready; }
  bool exists(const char* path) {
    ++calibration_test::state.existsChecks;
    return std::string(path) == "/battery-calibration.enable" && calibration_test::state.markerPresent;
  }
  HalFile open(const char*, int flags) {
    auto& state = calibration_test::state;
    ++state.opens;
    state.lastOpenFlags = flags;
    if (flags & O_TRUNC) state.data.clear();
    return HalFile(!state.failOpen);
  }
};

inline CalibrationTestStorage Storage;
