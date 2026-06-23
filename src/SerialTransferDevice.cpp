#include "SerialTransferDevice.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <Logging.h>
#include <esp_task_wdt.h>

#include <cstdarg>

#include "RecentBooksStore.h"

namespace {
// Per-byte read timeout while a transfer message is in flight. Must stay small
// so a stalled host can't freeze loop() (and with it button/cancel handling)
// for long.
constexpr unsigned long kReadTimeoutMs = 2000;

// Last path component (handles both '/' and '\\' separators the host may send).
std::string baseName(const std::string& path) {
  const size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Where uploaded books land. MicroReader scans the whole card recursively, so
// the exact folder is cosmetic; a dedicated folder keeps uploads tidy.
constexpr char kBooksRoot[] = "/books";

// Recursively collect .epub paths, capped to keep RAM bounded on huge cards.
constexpr size_t kMaxListed = 500;

void collectEpubs(const std::string& dir, std::vector<std::string>& out) {
  if (out.size() >= kMaxListed) return;
  auto d = Storage.open(dir.c_str());
  if (!d || !d.isDirectory()) {
    if (d) d.close();
    return;
  }
  d.rewindDirectory();
  char name[256];
  for (auto entry = d.openNextFile(); entry; entry = d.openNextFile()) {
    entry.getName(name, sizeof(name));
    if (name[0] == '.') {
      entry.close();
      continue;
    }
    std::string child = dir;
    if (child.empty() || child.back() != '/') child += '/';
    child += name;
    const bool isDir = entry.isDirectory();
    entry.close();
    if (isDir) {
      collectEpubs(child, out);
    } else if (FsHelpers::hasEpubExtension(std::string_view{name})) {
      out.push_back(child);
      if (out.size() >= kMaxListed) break;
    }
  }
  d.close();
}
}  // namespace

SerialTransferDevice::SerialTransferDevice() : protocol_(*this) {}

bool SerialTransferDevice::poll() { return protocol_.poll(); }

void SerialTransferDevice::dropByte() {
  if (!lookahead_.empty()) {
    lookahead_.pop_front();
    return;
  }
  if (logSerial.available() > 0) logSerial.read();
}

void SerialTransferDevice::emitStatus(const char* fmt, ...) {
  if (!statusFn_) return;
  char buf[96];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  statusFn_(statusCtx_, buf);
}

size_t SerialTransferDevice::available() {
  const int live = logSerial.available();
  return lookahead_.size() + (live > 0 ? static_cast<size_t>(live) : 0);
}

int SerialTransferDevice::peek(size_t i) {
  // Pull bytes into the lookahead until it holds index i (non-destructive read).
  while (lookahead_.size() <= i) {
    const int b = logSerial.read();
    if (b < 0) return -1;  // nothing more available right now
    lookahead_.push_back(static_cast<uint8_t>(b));
  }
  return lookahead_[i];
}

int SerialTransferDevice::readByte() {
  if (!lookahead_.empty()) {
    const int b = lookahead_.front();
    lookahead_.pop_front();
    return b;
  }
  const unsigned long start = millis();
  while (logSerial.available() <= 0) {
    if (millis() - start >= kReadTimeoutMs) return -1;
    // Yield + feed the watchdog while waiting so a slow/stalled host can't trip
    // the task WDT or peg the CPU (the old busy-wait did both).
    esp_task_wdt_reset();
    vTaskDelay(1);
  }
  return logSerial.read();
}

void SerialTransferDevice::writeBytes(const uint8_t* data, size_t len) { logSerial.write(data, len); }

bool SerialTransferDevice::fileBegin(const std::string& path) {
  Storage.ensureDirectoryExists(kBooksRoot);
  uploadName_ = baseName(path);
  if (!Storage.openFileForWrite("SerialTransfer", path, uploadFile_)) {
    emitStatus("Upload failed (open): %s", uploadName_.c_str());
    return false;
  }
  uploadPath_ = path;
  uploadOpen_ = true;
  coalesce_.clear();
  coalesce_.reserve(kCoalesceSize);
  uploadBytes_ = 0;
  uploadStartMs_ = millis();
  // Emitted before the protocol streams chunks; the activity paints it while no
  // SD/serial traffic is in flight, so there's no SPI contention.
  emitStatus("Receiving '%s'", uploadName_.c_str());
  return true;
}

bool SerialTransferDevice::flushCoalesceBuffer() {
  if (coalesce_.empty()) return true;
  const size_t written = uploadFile_.write(coalesce_.data(), coalesce_.size());
  const bool ok = written == coalesce_.size();
  coalesce_.clear();
  return ok;
}

bool SerialTransferDevice::fileWrite(const uint8_t* data, size_t len) {
  if (!uploadOpen_) return false;
  // Feed the task WDT once per chunk so a long steady upload (which never waits
  // in readByte) can't trip it.
  esp_task_wdt_reset();
  uploadBytes_ += static_cast<uint32_t>(len);
  // Buffer into a larger block; flush when full. This lets the protocol ACK each
  // 2048-byte chunk without waiting on a fresh SD/FAT block write every time —
  // SD latency is amortized across ~16 KB writes instead of paid per chunk.
  coalesce_.insert(coalesce_.end(), data, data + len);
  if (coalesce_.size() >= kCoalesceSize) return flushCoalesceBuffer();
  return true;
}

void SerialTransferDevice::fileEnd(bool keep) {
  if (!uploadOpen_) return;
  bool ok = keep;
  if (keep) ok = flushCoalesceBuffer();
  uploadFile_.close();
  uploadOpen_ = false;
  coalesce_.clear();
  coalesce_.shrink_to_fit();

  if (keep && ok) {
    const unsigned long elapsed = millis() - uploadStartMs_;
    const unsigned long kbps =
        elapsed > 0 ? (static_cast<unsigned long>(uploadBytes_) * 1000UL) / (elapsed * 1024UL) : 0;
    // LOG_INF is muted on the wire during a transfer but still hits the RTC ring
    // buffer (getLastLogs) for post-hoc debugging.
    LOG_INF("SXF", "Upload %s: %u bytes in %lu ms (~%lu KB/s)", uploadPath_.c_str(),
            static_cast<unsigned>(uploadBytes_), elapsed, kbps);
    const unsigned long kb = (static_cast<unsigned long>(uploadBytes_) + 1023UL) / 1024UL;
    emitStatus("Saved '%s': %lu KB in %lu.%lus (%lu KB/s)", uploadName_.c_str(), kb, elapsed / 1000UL,
               (elapsed % 1000UL) / 100UL, kbps);
  } else {
    // Caller asked to discard (timeout/CRC mismatch) or the final flush failed.
    Storage.remove(uploadPath_.c_str());
    emitStatus("Upload failed: '%s'", uploadName_.c_str());
  }
}

std::vector<serialtransfer::BookEntry> SerialTransferDevice::listBooks() {
  emitStatus("Listing books...");
  std::vector<std::string> paths;
  collectEpubs("/", paths);

  std::vector<serialtransfer::BookEntry> out;
  out.reserve(paths.size());
  for (auto& p : paths) {
    serialtransfer::BookEntry e;
    e.path = p;
    // Use cached metadata if this book has been opened before; otherwise leave
    // title/author empty (the host falls back to the filename). No metadata
    // opens here — that would risk OOM on large folders.
    const RecentBook cached = RECENT_BOOKS.getBookByPath(p);
    if (!cached.path.empty()) {
      e.title = cached.title;
      e.author = cached.author;
    }
    out.push_back(std::move(e));
  }
  emitStatus("Listed %u book(s)", static_cast<unsigned>(out.size()));
  return out;
}

bool SerialTransferDevice::removeFile(const std::string& path) {
  const bool ok = Storage.remove(path.c_str());
  emitStatus(ok ? "Removed '%s'" : "Remove failed: '%s'", baseName(path).c_str());
  return ok;
}

std::string SerialTransferDevice::statusLine() {
  char buf[96];
  snprintf(buf, sizeof(buf), "free=%u min=%u total=%u", static_cast<unsigned>(ESP.getFreeHeap()),
           static_cast<unsigned>(ESP.getMinFreeHeap()), static_cast<unsigned>(ESP.getHeapSize()));
  return std::string(buf);
}

std::string SerialTransferDevice::uploadDestination(const std::string& name) {
  // Strip any path components the host may have sent; we control the folder.
  std::string base = name;
  const size_t slash = base.find_last_of("/\\");
  if (slash != std::string::npos) base = base.substr(slash + 1);
  std::string dest = kBooksRoot;
  dest += '/';
  dest += base;
  return dest;
}
