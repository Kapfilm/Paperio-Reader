#include "Fb2NavigationIndex.h"
#include "native/FsFileReader.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
constexpr uint32_t HEADER = 32, BUCKETS = 256, NODE = 20, NONE = UINT32_MAX;
constexpr uint64_t HASH_INIT = 14695981039346656037ULL;
const uint8_t MAGIC[8] = {'F','B','2','N','A','V','0','1'};
uint64_t hashBytes(uint64_t h, const uint8_t* bytes, size_t n) {
  for (size_t i = 0; i < n; ++i) { h ^= bytes[i]; h *= 1099511628211ULL; }
  return h;
}
bool sourceHeader(HalFile& f, uint8_t version) {
  uint8_t h[7];
  return f.seek(0) && f.read(h, sizeof(h)) == sizeof(h) &&
         memcmp(h, "FB2IDX", 6) == 0 && h[6] == version;
}
// Buffered sequential writes avoid one SD transaction per offset or node.
class Writer {
 public:
  explicit Writer(HalFile& f) : file(f), buffer(new (std::nothrow) uint8_t[1024]) {}
  bool write(const void* data, size_t size) {
    if (!buffer) return false;
    const auto* p = static_cast<const uint8_t*>(data);
    while (size) {
      size_t n = std::min(size, size_t(1024 - used));
      memcpy(buffer.get() + used, p, n); used += n; p += n; size -= n;
      if (used == 1024 && !flush()) return false;
    }
    return true;
  }
  bool flush() {
    if (used && file.write(buffer.get(), used) != used) return false;
    used = 0; return true;
  }
 private:
  HalFile& file;
  std::unique_ptr<uint8_t[]> buffer;
  size_t used = 0;
};
bool discard(FsFileReader& in, uint32_t n, uint64_t* hash = nullptr) {
  uint8_t buf[128];
  while (n) {
    size_t take = std::min<size_t>(n, sizeof(buf));
    if (in.read(buf, take) != take) return false;
    if (hash) *hash = hashBytes(*hash, buf, take);
    n -= take;
  }
  return true;
}
bool record(FsFileReader& in, uint64_t& hash, uint16_t& idLength) {
  uint16_t titleLength;
  hash = HASH_INIT;
  return discard(in, 5) && in.read(&idLength, 2) == 2 && discard(in, idLength, &hash) &&
         in.read(&titleLength, 2) == 2 && discard(in, titleLength) && discard(in, 20);
}
}

bool Fb2NavigationIndex::open(const std::string& source, const std::string& path, uint8_t version) {
  good_ = false; count_ = 0;
  // Device HalFile::close asserts on a default-constructed handle.
  // Source/header errors can also leave the index handle unopened.
  auto closeFiles = [&]() {
    if (sections_.isOpen()) sections_.close();
    if (index_.isOpen()) index_.close();
  };
  closeFiles();
  auto fail = [&]() { closeFiles(); return false; };
  if (!Storage.openFileForRead("FB2NAV", source, sections_) || !sourceHeader(sections_, version) ||
      sections_.size64() > UINT32_MAX || !Storage.openFileForRead("FB2NAV", path, index_)) return fail();
  uint8_t magic[8]; uint32_t h[6];
  if (index_.read(magic, 8) != 8 || memcmp(magic, MAGIC, 8) || index_.read(h, 24) != 24) return fail();
  const uint64_t bucketOffset = HEADER + uint64_t(h[2]) * 4;
  const uint64_t nodeOffset = bucketOffset + BUCKETS * 4;
  if (h[1] < 7 || h[0] != version || h[1] != sections_.size64() || h[2] > INT32_MAX ||
      h[2] > (h[1] - 7) / 29 || h[3] > h[2] || h[4] != bucketOffset || h[5] != nodeOffset ||
      nodeOffset + uint64_t(h[3]) * NODE != index_.size64()) return fail();
  sourceSize_ = h[1]; count_ = h[2]; nodes_ = h[3]; bucketsOffset_ = h[4]; nodesOffset_ = h[5];
  good_ = true;
  return true;
}

bool Fb2NavigationIndex::build(const std::string& source, const std::string& path, uint8_t version) {
  HalFile input, output;
  if (!Storage.openFileForRead("FB2NAV", source, input) || !sourceHeader(input, version) ||
      input.size64() > UINT32_MAX) return false;
  const uint32_t sourceSize = input.size64();
  const std::string partial = path + ".part";
  if (!Storage.openFileForWrite("FB2NAV", partial, output)) return false;
  auto fail = [&]() { output.close(); Storage.remove(partial.c_str()); return false; };
  std::unique_ptr<std::array<uint32_t, BUCKETS>> heads(new (std::nothrow) std::array<uint32_t, BUCKETS>);
  if (!heads) return fail();
  heads->fill(NONE);
  Writer out(output);
  uint32_t h[6] = {version, sourceSize, 0, 0, 0, 0};
  if (!out.write(MAGIC, 8) || !out.write(h, 24)) return fail();
  FsFileReader in(input);
  uint32_t count = 0, nodes = 0;
  unsigned long lastYield = millis();
  while (in.tell() < sourceSize) {
    const uint32_t offset = in.tell(); uint64_t hash; uint16_t length;
    if (count == INT32_MAX || !record(in, hash, length) || !out.write(&offset, 4)) return fail();
    ++count; if (length) ++nodes;
    if (millis() - lastYield >= 100) { vTaskDelay(1); lastYield = millis(); }
  }
  const uint64_t bucketOffset = HEADER + uint64_t(count) * 4;
  const uint64_t nodeOffset = bucketOffset + BUCKETS * 4;
  if (nodeOffset + uint64_t(nodes) * NODE > UINT32_MAX || !out.write(heads->data(), BUCKETS * 4) ||
      !in.seek(7)) return fail();
  uint32_t node = 0;
  for (uint32_t chapter = 0; chapter < count; ++chapter) {
    const uint32_t offset = in.tell(); uint64_t hash; uint16_t length;
    if (!record(in, hash, length)) return fail();
    if (length) {
      uint32_t& head = (*heads)[hash % BUCKETS];
      if (!out.write(&hash, 8) || !out.write(&chapter, 4) || !out.write(&offset, 4) ||
          !out.write(&head, 4)) return fail();
      head = node++;
    }
    if (millis() - lastYield >= 100) { vTaskDelay(1); lastYield = millis(); }
  }
  if (node != nodes || in.tell() != sourceSize || !out.flush()) return fail();
  h[2] = count; h[3] = nodes; h[4] = bucketOffset; h[5] = nodeOffset;
  if (!output.seek(8) || output.write(h, 24) != 24 || !output.seek(bucketOffset) ||
      output.write(heads->data(), BUCKETS * 4) != BUCKETS * 4) return fail();
  output.flush();
  if (!output.close()) return fail();
  // Caller only builds absent/invalid disposable indices; never remove source state.
  if (Storage.exists(path.c_str()) && !Storage.remove(path.c_str())) return fail();
  if (!Storage.rename(partial.c_str(), path.c_str())) return fail();
  return true;
}

bool Fb2NavigationIndex::offsetForChapter(uint32_t chapter, uint32_t& offset) {
  if (!good_ || chapter >= count_) return false;
  if (!index_.seek(HEADER + chapter * 4) || index_.read(&offset, 4) != 4 ||
      offset < 7 || offset > sourceSize_ - 29) { good_ = false; return false; }
  return true;
}

int Fb2NavigationIndex::chapterForId(const std::string& id) {
  auto fail = [&]() { good_ = false; return -1; };
  if (!good_) return -1;
  if (id.empty() || id.size() > UINT16_MAX || !count_) return -1;
  const uint64_t hash = hashBytes(HASH_INIT, reinterpret_cast<const uint8_t*>(id.data()), id.size());
  uint32_t node;
  if (!index_.seek(bucketsOffset_ + (hash % BUCKETS) * 4) || index_.read(&node, 4) != 4) return fail();
  int found = -1;
  for (uint32_t visited = 0; node != NONE; ++visited) {
    if (visited >= nodes_ || node >= nodes_) return fail();
    uint64_t candidate; uint32_t fields[3];
    if (!index_.seek(nodesOffset_ + node * NODE) || index_.read(&candidate, 8) != 8 ||
        index_.read(fields, 12) != 12 || fields[0] >= count_ || fields[1] < 7 ||
        fields[1] > sourceSize_ - 29 || (fields[2] != NONE && fields[2] >= node)) return fail();
    node = fields[2];
    if (candidate != hash) continue;
    uint32_t chapterOffset;
    if (!offsetForChapter(fields[0], chapterOffset) || chapterOffset != fields[1]) return fail();
    uint16_t length;
    if (!sections_.seek(fields[1] + 5) || sections_.read(&length, 2) != 2) return fail();
    if (length != id.size()) continue;
    uint8_t bytes[128]; bool equal = true;
    for (size_t pos = 0; pos < length;) {
      size_t take = std::min(sizeof(bytes), size_t(length) - pos);
      if (sections_.read(bytes, take) != take) return fail();
      if (memcmp(bytes, id.data() + pos, take)) equal = false;
      pos += take;
    }
    if (equal && (found < 0 || fields[0] < static_cast<uint32_t>(found))) found = fields[0];
  }
  return found;
}
