#pragma once

#include <HalStorage.h>

#include <cstdint>
#include <string>
#include <vector>

class Dictionary {
 public:
  enum class Result : uint8_t { Found, NotFound, LowMemory, ReadError, Unsupported };

  bool openFirst(std::string* folderNameOut = nullptr);
  bool open(const char* folderName);
  bool needsIndex();
  bool buildIndex(void (*yieldFn)(void*) = nullptr, void* ctx = nullptr);
  bool lookup(const char* word, std::string& definitionOut, std::string& headwordOut, Result* resultOut = nullptr);

  static constexpr uint32_t MAX_DEFINITION_BYTES = 32 * 1024;

 private:
  struct Location {
    uint32_t offset = 0;
    uint32_t size = 0;
    bool found = false;
  };

  static constexpr uint32_t SAMPLE_INTERVAL = 256;
  static constexpr size_t PATH_BYTES = 192;
  static constexpr uint32_t QIDX_MAGIC = 0x58444951;
  static constexpr uint32_t SIDX_MAGIC = 0x58444953;
  static constexpr uint32_t QIDX_VERSION = 1;

  bool readIndexHeader(HalFile& sidecar, uint32_t magic, uint32_t sourceSize, uint32_t& sampleCount) const;
  bool buildSidecar(const char* sourceSuffix, const char* sidecarSuffix, uint32_t magic, uint8_t suffixBytes,
                    void (*yieldFn)(void*), void* ctx);
  Location locate(HalFile& idx, HalFile& qidx, uint32_t idxSize, uint32_t sampleCount, const char* target,
                  std::string& headwordOut);
  Location locateByOrdinal(HalFile& idx, HalFile& qidx, uint32_t idxSize, uint32_t sampleCount, uint32_t ordinal,
                           std::string& headwordOut);
  Location locateSynonym(HalFile& idx, HalFile& qidx, uint32_t idxSize, uint32_t idxSampleCount, const char* target,
                         std::string& headwordOut);
  bool readDefinition(const Location& location, std::string& out, Result* resultOut);
  static int readWord(HalFile& file, char* out, size_t outSize);
  static std::string cleanWord(const char* word);
  static void stemVariants(const std::string& word, std::vector<std::string>& out);

  std::string basePath;
  bool hasPlainDict = false;
  bool hasSyn = false;
  char wordBuf[256] = {};
};
