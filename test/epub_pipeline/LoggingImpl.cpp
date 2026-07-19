// Host logging for the pipeline harness: LOG_* goes to stderr so pipeline
// failures are diagnosable, while stdout stays reserved for the canonical dump.
// (Other host tests use test/zip_entry_reader/LoggingStub.cpp, which is silent.)
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>

void logPrintf(const char* level, const char* origin, const char* format, ...) {
  std::fprintf(stderr, "[%s][%s] ", level, origin);
  va_list args;
  va_start(args, format);
  std::vfprintf(stderr, format, args);
  va_end(args);
  std::fprintf(stderr, "\n");
}
std::string getLastLogs() { return {}; }
void clearLastLogs() {}
bool sanitizeLogHead() { return false; }

extern "C" {
uint32_t uzlib_adler32(const void*, unsigned int, uint32_t prev) { return prev; }
uint32_t uzlib_crc32(const void*, unsigned int, uint32_t crc) { return crc; }
}
