#pragma once
#include <algorithm>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

class String : public std::string {
 public:
  using std::string::string;
};
class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* p, size_t n) {
    size_t done = 0;
    while (done < n && write(p[done])) ++done;
    return done;
  }
};
using oflag_t = unsigned;
inline constexpr oflag_t O_RDONLY = 0;
using TickType_t = unsigned;
inline constexpr int pdTRUE = 1, pdFALSE = 0;
inline constexpr TickType_t portMAX_DELAY = ~0u;
#define pdMS_TO_TICKS(ms) (ms)
struct TestSemaphore {
  explicit TestSemaphore(bool recursive) : recursive(recursive) {}
  std::recursive_mutex mutex;
  std::mutex meta;
  bool recursive;
  std::thread::id owner;
  unsigned depth = 0;
  bool heldByCurrent() {
    std::lock_guard guard(meta);
    return depth && owner == std::this_thread::get_id();
  }
};
using SemaphoreHandle_t = TestSemaphore*;
namespace storage_test {
inline TestSemaphore* storage = nullptr;
inline TestSemaphore* spi = nullptr;
inline std::atomic<unsigned> unlockedIo = 0, closes = 0, failedTakes = 0, spiTakeCalls = 0;
inline std::atomic<bool> failNextSpiTake = false, closeFails = false;
inline std::mutex gateMutex;
inline std::condition_variable gateCv;
inline bool blockRead = false, readEntered = false, releaseRead = false;
inline void io() {
  if (!storage || !spi || !storage->heldByCurrent() || !spi->heldByCurrent()) ++unlockedIo;
}
}  // namespace storage_test
inline SemaphoreHandle_t createTestSemaphore(bool recursive) {
  auto* s = new TestSemaphore(recursive);
  if (!storage_test::storage)
    storage_test::storage = s;
  else if (!storage_test::spi)
    storage_test::spi = s;
  return s;
}
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return createTestSemaphore(false); }
inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return createTestSemaphore(true); }
inline int takeTestSemaphore(SemaphoreHandle_t s, bool recursive) {
  assert(s);
  if (s == storage_test::spi) {
    ++storage_test::spiTakeCalls;
    if (storage_test::failNextSpiTake.exchange(false)) {
      ++storage_test::failedTakes;
      return pdFALSE;
    }
  }
  if (s->heldByCurrent() && !recursive) throw std::logic_error("non-recursive storage lock re-entered");
  s->mutex.lock();
  {
    std::lock_guard guard(s->meta);
    s->owner = std::this_thread::get_id();
    ++s->depth;
  }
  return pdTRUE;
}
inline int giveTestSemaphore(SemaphoreHandle_t s) {
  {
    std::lock_guard guard(s->meta);
    assert(s->depth && s->owner == std::this_thread::get_id());
    if (!--s->depth) s->owner = {};
  }
  s->mutex.unlock();
  return pdTRUE;
}
inline int xSemaphoreTake(SemaphoreHandle_t s, TickType_t) { return takeTestSemaphore(s, false); }
inline int xSemaphoreTakeRecursive(SemaphoreHandle_t s, TickType_t) { return takeTestSemaphore(s, true); }
inline int xSemaphoreGive(SemaphoreHandle_t s) { return giveTestSemaphore(s); }
inline int xSemaphoreGiveRecursive(SemaphoreHandle_t s) { return giveTestSemaphore(s); }

class FakeFsFile {
 public:
  FakeFsFile() = default;
  explicit FakeFsFile(bool open) : open_(open) {}
  FakeFsFile(FakeFsFile&& other) : open_(other.open_) { other.open_ = false; }
  FakeFsFile& operator=(FakeFsFile&& other) {
    if (this != &other) {
      if (open_) close();
      open_ = other.open_;
      other.open_ = false;
    }
    return *this;
  }
  ~FakeFsFile() {
    if (open_) close();
  }
  bool isOpen() const { return open_; }
  bool close() {
    if (open_) {
      storage_test::io();
      ++storage_test::closes;
      open_ = false;
    }
    return !storage_test::closeFails;
  }
  bool isDirectory() const { return false; }
  void flush() { storage_test::io(); }
  size_t getName(char*, size_t) { return 0; }
  size_t size() const { return 4; }
  size_t fileSize() const { return 4; }
  size_t position() const { return 0; }
  bool seekSet(uint64_t) {
    storage_test::io();
    return true;
  }
  bool seekCur(int64_t) {
    storage_test::io();
    return true;
  }
  int available() const { return 0; }
  int read(void*, size_t) {
    storage_test::io();
    std::unique_lock gate(storage_test::gateMutex);
    if (storage_test::blockRead) {
      storage_test::readEntered = true;
      storage_test::gateCv.notify_all();
      storage_test::gateCv.wait(gate, [] { return storage_test::releaseRead; });
    }
    return 0;
  }
  int read() {
    char c;
    return read(&c, 1);
  }
  size_t write(const void*, size_t n) {
    storage_test::io();
    return n;
  }
  size_t write(uint8_t) {
    storage_test::io();
    return 1;
  }
  bool rename(const char*) {
    storage_test::io();
    return true;
  }
  bool getModifyDateTime(uint16_t*, uint16_t*) { return true; }
  bool getCreateDateTime(uint16_t*, uint16_t*) { return true; }
  void rewindDirectory() {}
  FakeFsFile openNextFile() {
    storage_test::io();
    return FakeFsFile(true);
  }

 private:
  bool open_ = false;
};
class SDCardManager {
 public:
  static SDCardManager& getInstance() {
    static SDCardManager s;
    return s;
  }
  bool begin() { return true; }
  bool ready() const { return true; }
  std::vector<String> listFiles(const char*, int) { return {}; }
  String readFile(const char*) { return {}; }
  bool readFileToStream(const char*, Print& out, size_t) {
    static const uint8_t data[] = {1, 2, 3, 4};
    return out.write(data, sizeof(data)) == sizeof(data);
  }
  size_t readFileToBuffer(const char*, char*, size_t, size_t) { return 0; }
  bool writeFile(const char*, const String&) {
    storage_test::io();
    return true;
  }
  bool ensureDirectoryExists(const char*) {
    storage_test::io();
    return true;
  }
  uint64_t sdTotalBytes() const { return 1024; }
  uint64_t sdUsedBytes() { return 0; }
  FakeFsFile open(const char*, oflag_t) {
    storage_test::io();
    return FakeFsFile(true);
  }
  bool mkdir(const char*, bool) { return true; }
  bool exists(const char*) { return true; }
  bool remove(const char*) { return true; }
  bool rename(const char*, const char*) { return true; }
  bool rmdir(const char*) { return true; }
  bool removeDir(const char*) { return true; }
  bool openFileForRead(const char*, const char*, FakeFsFile& file) {
    storage_test::io();
    file = FakeFsFile(true);
    return true;
  }
  bool openFileForWrite(const char*, const char*, FakeFsFile& file) {
    storage_test::io();
    file = FakeFsFile(true);
    return true;
  }
};
struct FsDateTime {
  template <class T>
  static void setCallback(T) {}
};
#define FS_DATE(a, b, c) 0
#define FS_TIME(a, b, c) 0
struct HalClock {
  static bool isSynced() { return false; }
  static time_t now() { return 0; }
};
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_DBG(...) ((void)0)
