#include "Fb2AnchorIndex.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <new>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
constexpr uint32_t HEADER = 32, BUCKETS = 4096, START = HEADER + BUCKETS * 4, NODE = 18;
constexpr uint32_t NONE = UINT32_MAX;
const uint8_t MAGIC[8] = {'F','B','2','A','N','C','0','1'};
uint64_t hashId(const std::string& id) {
  uint64_t h = 14695981039346656037ULL;
  for (unsigned char c : id) { h ^= c; h *= 1099511628211ULL; }
  return h;
}
bool sourceSize(const std::string& path, uint8_t version, uint32_t& size) {
  HalFile file;
  uint8_t header[7];
  if (!Storage.openFileForRead("FB2ANCH", path, file) || file.size64() > UINT32_MAX ||
      file.read(header, sizeof(header)) != sizeof(header) || memcmp(header, "FB2IDX", 6) ||
      header[6] != version) return false;
  size = file.size64();
  return true;
}
}

struct Fb2AnchorIndexWriter::State {
  HalFile file;
  std::string path, partial;
  std::array<uint32_t, BUCKETS> heads;
  uint8_t buffer[1024];
  size_t used = 0;
  uint32_t source = 0, offset = START, nodes = 0, version = 0;
  unsigned long lastYield = 0;
  bool ok = false;
  ~State() {
    if (file.isOpen()) file.close();
    if (!partial.empty()) Storage.remove(partial.c_str());
  }
  bool flush() {
    if (used && file.write(buffer, used) != used) return ok = false;
    used = 0;
    return true;
  }
  bool write(const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    while (size) {
      const size_t take = std::min(size, sizeof(buffer) - used);
      memcpy(buffer + used, bytes, take);
      used += take; bytes += take; size -= take;
      if (used == sizeof(buffer) && !flush()) return false;
    }
    return true;
  }
};
Fb2AnchorIndexWriter::Fb2AnchorIndexWriter() = default;
Fb2AnchorIndexWriter::~Fb2AnchorIndexWriter() = default;
bool Fb2AnchorIndexWriter::good() const { return state_ && state_->ok; }

bool Fb2AnchorIndexWriter::begin(const std::string& sections, const std::string& path, uint8_t version) {
  state_.reset(new (std::nothrow) State);
  if (!state_ || !sourceSize(sections, version, state_->source)) return false;
  auto& s = *state_;
  s.path = path; s.partial = path + ".part"; s.version = version;
  s.heads.fill(NONE);
  if (!Storage.openFileForWrite("FB2ANCH", s.partial, s.file)) return false;
  const uint32_t header[6] = {version, s.source, 0, START, BUCKETS, START};
  s.ok = s.write(MAGIC, 8) && s.write(header, sizeof(header)) && s.write(s.heads.data(), BUCKETS * 4);
  s.lastYield = millis();
  return s.ok;
}

bool Fb2AnchorIndexWriter::add(const std::string& id, uint32_t chapter) {
  if (!good()) return false;
  auto& s = *state_;
  if (id.empty()) return true;
  if (id.size() > UINT16_MAX || chapter >= (s.source - 7) / 29 || chapter > INT32_MAX ||
      uint64_t(s.offset) + NODE + id.size() > UINT32_MAX) return s.ok = false;
  const uint64_t hash = hashId(id);
  uint32_t& head = s.heads[hash % BUCKETS];
  const uint16_t length = id.size();
  if (!s.write(&hash, 8) || !s.write(&chapter, 4) || !s.write(&head, 4) ||
      !s.write(&length, 2) || !s.write(id.data(), id.size())) return s.ok = false;
  head = s.offset; s.offset += NODE + length; ++s.nodes;
  if (millis() - s.lastYield >= 100) { vTaskDelay(1); s.lastYield = millis(); }
  return true;
}

bool Fb2AnchorIndexWriter::finish() {
  if (!good()) return false;
  auto& s = *state_;
  const uint32_t header[6] = {s.version, s.source, s.nodes, s.offset, BUCKETS, START};
  if (!s.flush() || !s.file.seek(8) || s.file.write(header, sizeof(header)) != sizeof(header) ||
      s.file.write(s.heads.data(), BUCKETS * 4) != BUCKETS * 4) return s.ok = false;
  s.file.flush();
  if (!s.file.close()) return s.ok = false;
  // Preserve the previous complete index if publishing the replacement fails.
  const std::string backup = s.path + ".old";
  const bool hadOld = Storage.exists(s.path.c_str());
  if (hadOld && ((Storage.exists(backup.c_str()) && !Storage.remove(backup.c_str())) ||
                 !Storage.rename(s.path.c_str(), backup.c_str()))) return s.ok = false;
  if (!Storage.rename(s.partial.c_str(), s.path.c_str())) {
    if (hadOld) Storage.rename(backup.c_str(), s.path.c_str());
    return s.ok = false;
  }
  s.partial.clear();
  if (hadOld) Storage.remove(backup.c_str());
  state_.reset(); // Release the builder heap before rendering or opening a page.
  return true;
}

bool Fb2AnchorIndex::open(const std::string& sections, const std::string& path, uint8_t version) {
  good_ = false;
  if (file_.isOpen()) file_.close();
  uint32_t source;
  if (!sourceSize(sections, version, source) || !Storage.openFileForRead("FB2ANCH", path, file_)) return false;
  auto fail = [&]() { if (file_.isOpen()) file_.close(); return false; };
  uint8_t magic[8]; uint32_t header[6];
  if (file_.read(magic, 8) != 8 || memcmp(magic, MAGIC, 8) ||
      file_.read(header, sizeof(header)) != sizeof(header) || header[0] != version || header[1] != source ||
      header[3] < START || header[3] != file_.size64() || header[4] != BUCKETS || header[5] != START ||
      uint64_t(header[2]) * (NODE + 1) > header[3] - START) return fail();
  size_ = header[3]; nodes_ = header[2]; chapterBound_ = (source - 7) / 29;
  good_ = true;
  return true;
}

int Fb2AnchorIndex::chapterForId(const std::string& id) {
  if (!good_ || id.empty() || id.size() > UINT16_MAX) return -1;
  auto fail = [&]() { good_ = false; return -1; };
  const uint64_t hash = hashId(id);
  uint32_t offset;
  if (!file_.seek(HEADER + (hash % BUCKETS) * 4) || file_.read(&offset, 4) != 4) return fail();
  int found = -1;
  for (uint32_t visited = 0; offset != NONE; ++visited) {
    if (visited >= nodes_ || offset < START || uint64_t(offset) + NODE > size_) return fail();
    uint8_t record[NODE]; uint64_t candidate; uint32_t chapter, next; uint16_t length;
    if (!file_.seek(offset) || file_.read(record, NODE) != NODE) return fail();
    memcpy(&candidate, record, 8); memcpy(&chapter, record + 8, 4);
    memcpy(&next, record + 12, 4); memcpy(&length, record + 16, 2);
    if (!length || chapter >= chapterBound_ || chapter > INT32_MAX || uint64_t(offset) + NODE + length > size_ ||
        (next != NONE && (next < START || next >= offset))) return fail();
    offset = next;
    if (candidate != hash || length != id.size()) continue;
    bool equal = true;
    uint8_t bytes[128];
    for (size_t pos = 0; pos < length;) {
      const size_t take = std::min(sizeof(bytes), size_t(length) - pos);
      if (file_.read(bytes, take) != take) return fail();
      if (memcmp(bytes, id.data() + pos, take)) equal = false;
      pos += take;
    }
    if (equal && (found < 0 || chapter < static_cast<uint32_t>(found))) found = chapter;
  }
  return found;
}
