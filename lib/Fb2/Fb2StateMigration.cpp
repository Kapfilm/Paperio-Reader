#include "Fb2StateMigration.h"
#include <HalStorage.h>
#include <Logging.h>
#include <array>
#include <cstring>

namespace {
constexpr uint16_t NO_POSITION = UINT16_MAX;
std::string markerPath(const std::string& path) { return path + ".native-chapter-migration"; }
struct Record { uint8_t phase = 0; uint16_t spine = NO_POSITION; };
bool readRecord(const std::string& path, Record& record) {
  FsFile file;
  std::string selected = markerPath(path);
  if (Storage.exists((selected + ".done3").c_str())) selected += ".done3";
  else if (Storage.exists((selected + ".done2").c_str())) selected += ".done2";
  if (!Storage.openFileForRead("FB2M", selected, file)) return false;
  uint8_t bytes[7];
  if (file.read(bytes, sizeof(bytes)) != sizeof(bytes) || std::memcmp(bytes, "F2M1", 4) ||
      bytes[4] < 1 || bytes[4] > 3) return false;
  record.phase = bytes[4];
  record.spine = bytes[5] | (uint16_t(bytes[6]) << 8);
  return true;
}
bool writeRecord(const std::string& path, const Record& record) {
  // Each completed phase gets its own immutable marker. A power loss while
  // committing a later phase cannot erase the previous valid recovery record.
  const auto target = markerPath(path) + (record.phase > 1 ? ".done" + std::to_string(record.phase) : "");
  const auto temporary = target + ".tmp";
  FsFile file;
  if (!Storage.openFileForWrite("FB2M", temporary, file)) return false;
  const uint8_t bytes[] = {'F', '2', 'M', '1', record.phase, uint8_t(record.spine), uint8_t(record.spine >> 8)};
  if (file.write(bytes, sizeof(bytes)) != sizeof(bytes) || !file.close()) return false;
  if (Storage.exists(target.c_str())) return true;
  return Storage.rename(temporary.c_str(), target.c_str());
}
bool copyIfAbsent(const std::string& from, const std::string& to) {
  if (!Storage.exists(from.c_str()) || Storage.exists(to.c_str())) return true;
  FsFile input, output;
  const std::string partial = to + ".partial";
  if (!Storage.openFileForRead("FB2M", from, input) ||
      !Storage.openFileForWrite("FB2M", partial, output)) return false;
  std::array<uint8_t, 512> buffer;
  while (input.available()) {
    const int n = input.read(buffer.data(), buffer.size());
    if (n <= 0 || output.write(buffer.data(), n) != size_t(n)) return false;
  }
  if (!output.close()) return false;
  return Storage.rename(partial.c_str(), to.c_str());
}
template<typename T> bool readPod(FsFile& file, T& value) {
  return file.read(reinterpret_cast<uint8_t*>(&value), sizeof(value)) == sizeof(value);
}
bool skip(FsFile& file, uint32_t length) {
  if (length > file.size() - file.position()) return false;
  return file.seek(file.position() + length);
}
// Same innerStartOffset identifies all virtual slices of one source section.
bool sourceChapter(FsFile& file, uint16_t oldSpine, uint16_t& nativeSpine) {
  uint32_t previousOffset = UINT32_MAX;
  uint32_t ordinal = 0;
  for (uint32_t spine = 0; spine <= oldSpine; ++spine) {
    uint8_t level;
    uint32_t offset;
    uint16_t length;
    if (!readPod(file, level) || !readPod(file, offset) || !readPod(file, length) || length > 4096 ||
        !skip(file, length) || !readPod(file, length) || length > 4096 || !skip(file, length) ||
        !skip(file, 5 * sizeof(uint32_t))) return false;
    if (spine && offset != previousOffset) ++ordinal;
    previousOffset = offset;
  }
  if (ordinal >= NO_POSITION) return false;
  nativeSpine = uint16_t(ordinal);
  return true;
}
bool restoreBaseline(const std::string& cachePath, const Record& record) {
  const auto backup = Fb2StateMigration::backupPath(cachePath);
  if (!Storage.mkdir(cachePath.c_str(), true) && !Storage.exists(cachePath.c_str())) return false;
  for (const auto* name : {"stats.bin", "reader_settings.bin"}) {
    if (!copyIfAbsent(backup + "/" + name, cachePath + "/" + name)) return false;
  }
  // A previous successful restore is preserved across a failed indexing retry.
  if (record.spine == NO_POSITION || Storage.exists((cachePath + "/progress.bin").c_str())) return true;
  FsFile file;
  const std::string progress = cachePath + "/progress.bin";
  const std::string partial = progress + ".partial";
  if (!Storage.openFileForWrite("FB2M", partial, file)) return false;
  uint8_t bytes[11] = {uint8_t(record.spine), uint8_t(record.spine >> 8)};
  if (file.write(bytes, sizeof(bytes)) != sizeof(bytes) || !file.close()) return false;
  return Storage.rename(partial.c_str(), progress.c_str());
}
}
namespace Fb2StateMigration {
std::string backupPath(const std::string& cachePath) { return cachePath + ".before-native-chapters"; }
bool prepare(const std::string& cachePath) {
  const auto marker = markerPath(cachePath);
  const auto backup = backupPath(cachePath);
  // Recover a completed marker write interrupted between unlink and rename.
  if (!Storage.exists(marker.c_str()) && Storage.exists((marker + ".tmp").c_str()) &&
      !Storage.rename((marker + ".tmp").c_str(), marker.c_str())) return false;
  Record record;
  if (Storage.exists(marker.c_str())) {
    if (!readRecord(cachePath, record)) return false;
    if (record.phase > 1) {
      // A downgrade may have regenerated another v29 package. Do not reinterpret
      // its coordinates or overwrite the original migration archive.
      FsFile current;
      uint8_t header[7];
      if (Storage.openFileForRead("FB2M", cachePath + "/.fb2_sections.bin", current) &&
          current.read(header, sizeof(header)) == sizeof(header) &&
          !std::memcmp(header, "FB2IDX", 6) && header[6] == 29) return false;
      return true;
    }
    if (Storage.exists(backup.c_str())) {
      FsFile current;
      uint8_t header[7];
      if (Storage.openFileForRead("FB2M", cachePath + "/.fb2_sections.bin", current) &&
          current.read(header, sizeof(header)) == sizeof(header) &&
          !std::memcmp(header, "FB2IDX", 6) && header[6] == 29) return false;
    } else if (!Storage.rename(cachePath.c_str(), backup.c_str())) {
      return false;
    }
    return restoreBaseline(cachePath, record);
  }
  const bool hasUnversionedState = Storage.exists((cachePath + "/progress.bin").c_str()) ||
                                   Storage.exists((cachePath + "/bookmarks.bin").c_str());
  FsFile index;
  if (!Storage.openFileForRead("FB2M", cachePath + "/.fb2_sections.bin", index)) return !hasUnversionedState;
  uint8_t header[7];
  if (index.read(header, sizeof(header)) != sizeof(header) || std::memcmp(header, "FB2IDX", 6))
    return !hasUnversionedState;
  if (header[6] == 30) return true;
  if (header[6] != 29) return !hasUnversionedState;
  // Never overwrite an existing archive, including after a firmware rollback.
  if (Storage.exists(backup.c_str())) return false;
  FsFile progress;
  if (Storage.openFileForRead("FB2M", cachePath + "/progress.bin", progress)) {
    uint16_t oldSpine;
    if (!readPod(progress, oldSpine) || !sourceChapter(index, oldSpine, record.spine)) return false;
    progress.close();
  }
  index.close();
  record.phase = 1;
  if (!writeRecord(cachePath, record) || !Storage.rename(cachePath.c_str(), backup.c_str())) return false;
  LOG_INF("FB2M", "Archived virtual-chapter cache at %s; resume at source chapter %u", backup.c_str(), record.spine);
  return restoreBaseline(cachePath, record);
}
bool needsReaderMigration(const std::string& path) { Record r; return readRecord(path, r) && r.phase == 1; }
bool needsNotice(const std::string& path) { Record r; return readRecord(path, r) && r.phase < 3; }
bool markReaderMigrated(const std::string& path) { Record r; return readRecord(path, r) && (r.phase > 1 || (r.phase = 2, writeRecord(path, r))); }
bool acknowledgeNotice(const std::string& path) { Record r; return readRecord(path, r) && (r.phase == 3 || (r.phase = 3, writeRecord(path, r))); }
}
