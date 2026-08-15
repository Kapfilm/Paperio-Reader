#include "Dictionary.h"

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "DictZip.h"
#include "DictionaryRegistry.h"

namespace {

constexpr size_t QIDX_HEADER_WORDS = 5;
constexpr size_t QIDX_HEADER_BYTES = QIDX_HEADER_WORDS * sizeof(uint32_t);
constexpr uint32_t DEFINITION_HEADROOM = 12 * 1024;

uint32_t readBe32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

int indexCmp(const char* a, const char* b) {
  while (*a && *b) {
    const unsigned char ca = static_cast<unsigned char>(*a);
    const unsigned char cb = static_cast<unsigned char>(*b);
    const int fa = ca < 0x80 ? std::tolower(ca) : ca;
    const int fb = cb < 0x80 ? std::tolower(cb) : cb;
    if (fa != fb) return fa - fb;
    ++a;
    ++b;
  }
  return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}

bool isWordEdgeByte(const unsigned char c) { return c >= 0x80 || std::isalnum(c) != 0; }

}  // namespace

bool Dictionary::openFirst(std::string* folderNameOut) {
  std::string folder;
  std::string resolved;
  if (!DictionaryRegistry::resolveFirst(folder, resolved)) return false;
  if (resolved.size() + sizeof(".dict.dz") >= PATH_BYTES) return false;
  if (folderNameOut) *folderNameOut = folder;
  basePath = std::move(resolved);
  hasPlainDict = Storage.exists((basePath + ".dict").c_str());
  hasSyn = Storage.exists((basePath + ".syn").c_str());
  return true;
}

bool Dictionary::open(const char* folderName) {
  basePath.clear();
  std::string resolved;
  if (!DictionaryRegistry::resolveBasePath(folderName, resolved)) return false;
  if (resolved.size() + sizeof(".dict.dz") >= PATH_BYTES) return false;
  basePath = std::move(resolved);
  hasPlainDict = Storage.exists((basePath + ".dict").c_str());
  hasSyn = Storage.exists((basePath + ".syn").c_str());
  return true;
}

bool Dictionary::readIndexHeader(HalFile& sidecar, const uint32_t magic, const uint32_t sourceSize,
                                 uint32_t& sampleCount) const {
  uint32_t header[QIDX_HEADER_WORDS] = {};
  if (!sidecar.seekSet(0) || sidecar.read(header, sizeof(header)) != static_cast<int>(sizeof(header))) return false;
  if (header[0] != magic || header[1] != QIDX_VERSION || header[2] != SAMPLE_INTERVAL || header[4] != sourceSize)
    return false;
  sampleCount = header[3];
  return sampleCount > 0;
}

bool Dictionary::needsIndex() {
  if (basePath.empty()) return false;
  const auto stale = [this](const char* sourceSuffix, const char* sidecarSuffix, const uint32_t magic) {
    HalFile source;
    if (!Storage.openFileForRead("DICT", basePath + sourceSuffix, source)) return false;
    const uint32_t sourceSize = static_cast<uint32_t>(source.fileSize());
    source.close();
    HalFile sidecar;
    uint32_t count = 0;
    const bool valid = Storage.openFileForRead("DICT", basePath + sidecarSuffix, sidecar) &&
                       readIndexHeader(sidecar, magic, sourceSize, count);
    sidecar.close();
    return !valid;
  };
  // Building a synonym sidecar can require scanning tens of megabytes.  Never
  // do that synchronously from DictionaryDefinitionActivity::onEnter().  A
  // prebuilt .sidx is used when present; otherwise synonym lookup is skipped
  // and the normal headword/morphology fallbacks remain available.
  return stale(".idx", ".qidx", QIDX_MAGIC);
}

bool Dictionary::buildIndex(void (*yieldFn)(void*), void* ctx) {
  return buildSidecar(".idx", ".qidx", QIDX_MAGIC, 8, yieldFn, ctx);
}

bool Dictionary::buildSidecar(const char* sourceSuffix, const char* sidecarSuffix, const uint32_t magic,
                              const uint8_t suffixBytes, void (*yieldFn)(void*), void* ctx) {
  if (basePath.empty()) return false;
  HalFile source;
  if (!Storage.openFileForRead("DICT", basePath + sourceSuffix, source)) return false;
  const uint32_t sourceSize = static_cast<uint32_t>(source.fileSize());
  HalFile existing;
  uint32_t existingSamples = 0;
  if (Storage.openFileForRead("DICT", basePath + sidecarSuffix, existing) &&
      readIndexHeader(existing, magic, sourceSize, existingSamples)) {
    existing.close();
    source.close();
    return true;
  }
  existing.close();

  constexpr size_t CHUNK_BYTES = 4096;
  auto buffer = makeUniqueNoThrow<uint8_t[]>(CHUNK_BYTES);
  if (!buffer) {
    source.close();
    return false;
  }

  HalFile out;
  const std::string sidecarPath = basePath + sidecarSuffix;
  if (!Storage.openFileForWrite("DICT", sidecarPath, out)) {
    source.close();
    return false;
  }
  const uint32_t placeholder[QIDX_HEADER_WORDS] = {};
  if (out.write(placeholder, sizeof(placeholder)) != sizeof(placeholder)) {
    source.close();
    out.close();
    Storage.remove(sidecarPath.c_str());
    return false;
  }

  uint32_t zero = 0;
  if (out.write(&zero, sizeof(zero)) != sizeof(zero)) {
    source.close();
    out.close();
    Storage.remove(sidecarPath.c_str());
    return false;
  }
  uint32_t sampleCount = 1;
  uint32_t entryCount = 0;
  uint32_t pos = 0;
  uint8_t suffixLeft = 0;
  uint32_t sinceYield = 0;
  bool ok = true;
  while (pos < sourceSize && ok) {
    const int n = source.read(buffer.get(), CHUNK_BYTES);
    if (n <= 0) {
      ok = false;
      break;
    }
    for (int i = 0; i < n; ++i) {
      if (suffixLeft == 0) {
        if (buffer[i] == 0) suffixLeft = suffixBytes;
      } else if (--suffixLeft == 0) {
        ++entryCount;
        const uint32_t next = pos + static_cast<uint32_t>(i) + 1;
        if (entryCount % SAMPLE_INTERVAL == 0 && next < sourceSize) {
          if (out.write(&next, sizeof(next)) != sizeof(next)) {
            ok = false;
            break;
          }
          ++sampleCount;
        }
      }
    }
    pos += static_cast<uint32_t>(n);
    sinceYield += static_cast<uint32_t>(n);
    if (yieldFn && sinceYield >= 64 * 1024) {
      sinceYield = 0;
      yieldFn(ctx);
    }
  }

  if (ok) {
    const uint32_t header[QIDX_HEADER_WORDS] = {magic, QIDX_VERSION, SAMPLE_INTERVAL, sampleCount, sourceSize};
    ok = out.seekSet(0) && out.write(header, sizeof(header)) == sizeof(header);
  }
  source.close();
  out.close();
  if (!ok) {
    Storage.remove(sidecarPath.c_str());
  }
  return ok;
}

int Dictionary::readWord(HalFile& file, char* out, const size_t outSize) {
  size_t used = 0;
  while (used + 1 < outSize) {
    const int ch = file.read();
    if (ch < 0) return -1;
    if (ch == 0) {
      out[used] = '\0';
      return static_cast<int>(used);
    }
    out[used++] = static_cast<char>(ch);
  }
  out[outSize - 1] = '\0';
  int ch = 0;
  do {
    ch = file.read();
  } while (ch > 0);
  return static_cast<int>(outSize - 1);
}

Dictionary::Location Dictionary::locate(HalFile& idx, HalFile& qidx, const uint32_t idxSize, const uint32_t sampleCount,
                                        const char* target, std::string& headwordOut) {
  Location result;
  uint32_t lo = 0;
  uint32_t hi = sampleCount > 0 ? sampleCount - 1 : 0;
  uint32_t startOffset = 0;
  while (lo < hi) {
    const uint32_t mid = (lo + hi + 1) / 2;
    uint32_t offset = 0;
    if (!qidx.seekSet(QIDX_HEADER_BYTES + mid * sizeof(uint32_t)) ||
        qidx.read(&offset, sizeof(offset)) != static_cast<int>(sizeof(offset)) || !idx.seekSet(offset) ||
        readWord(idx, wordBuf, sizeof(wordBuf)) < 0) {
      lo = 0;
      break;
    }
    if (indexCmp(wordBuf, target) <= 0)
      lo = mid;
    else
      hi = mid - 1;
  }
  if (sampleCount > 0 && qidx.seekSet(QIDX_HEADER_BYTES + lo * sizeof(uint32_t))) {
    qidx.read(&startOffset, sizeof(startOffset));
  }
  if (!idx.seekSet(startOffset)) return result;

  while (static_cast<uint32_t>(idx.position()) < idxSize) {
    if (readWord(idx, wordBuf, sizeof(wordBuf)) < 0) break;
    uint8_t suffix[8];
    if (idx.read(suffix, sizeof(suffix)) != static_cast<int>(sizeof(suffix))) break;
    const int cmp = indexCmp(wordBuf, target);
    if (cmp == 0) {
      result.offset = readBe32(suffix);
      result.size = readBe32(suffix + 4);
      result.found = true;
      headwordOut = wordBuf;
      return result;
    }
    if (cmp > 0) break;
  }
  return result;
}

Dictionary::Location Dictionary::locateByOrdinal(HalFile& idx, HalFile& qidx, const uint32_t idxSize,
                                                 const uint32_t sampleCount, const uint32_t ordinal,
                                                 std::string& headwordOut) {
  Location result;
  if (sampleCount == 0) return result;
  uint32_t sample = ordinal / SAMPLE_INTERVAL;
  if (sample >= sampleCount) sample = sampleCount - 1;
  uint32_t offset = 0;
  if (!qidx.seekSet(QIDX_HEADER_BYTES + sample * sizeof(uint32_t)) ||
      qidx.read(&offset, sizeof(offset)) != static_cast<int>(sizeof(offset)) || !idx.seekSet(offset)) {
    return result;
  }

  uint32_t current = sample * SAMPLE_INTERVAL;
  uint8_t suffix[8];
  while (current <= ordinal && static_cast<uint32_t>(idx.position()) < idxSize) {
    if (readWord(idx, wordBuf, sizeof(wordBuf)) < 0 ||
        idx.read(suffix, sizeof(suffix)) != static_cast<int>(sizeof(suffix))) {
      return result;
    }
    if (current++ != ordinal) continue;
    result.offset = readBe32(suffix);
    result.size = readBe32(suffix + 4);
    result.found = true;
    headwordOut = wordBuf;
    return result;
  }
  return result;
}

Dictionary::Location Dictionary::locateSynonym(HalFile& idx, HalFile& qidx, const uint32_t idxSize,
                                               const uint32_t idxSampleCount, const char* target,
                                               std::string& headwordOut) {
  Location result;
  HalFile syn;
  HalFile sidx;
  if (!Storage.openFileForRead("DICT", basePath + ".syn", syn) ||
      !Storage.openFileForRead("DICT", basePath + ".sidx", sidx)) {
    syn.close();
    sidx.close();
    return result;
  }
  const uint32_t synSize = static_cast<uint32_t>(syn.fileSize());
  uint32_t sampleCount = 0;
  if (!readIndexHeader(sidx, SIDX_MAGIC, synSize, sampleCount)) {
    syn.close();
    sidx.close();
    return result;
  }

  uint32_t lo = 0;
  uint32_t hi = sampleCount - 1;
  uint32_t startOffset = 0;
  while (lo < hi) {
    const uint32_t mid = (lo + hi + 1) / 2;
    uint32_t offset = 0;
    if (!sidx.seekSet(QIDX_HEADER_BYTES + mid * sizeof(uint32_t)) ||
        sidx.read(&offset, sizeof(offset)) != static_cast<int>(sizeof(offset)) || !syn.seekSet(offset) ||
        readWord(syn, wordBuf, sizeof(wordBuf)) < 0) {
      lo = 0;
      break;
    }
    if (indexCmp(wordBuf, target) <= 0)
      lo = mid;
    else
      hi = mid - 1;
  }
  if (sidx.seekSet(QIDX_HEADER_BYTES + lo * sizeof(uint32_t))) {
    sidx.read(&startOffset, sizeof(startOffset));
  }
  if (syn.seekSet(startOffset)) {
    while (static_cast<uint32_t>(syn.position()) < synSize) {
      if (readWord(syn, wordBuf, sizeof(wordBuf)) < 0) break;
      uint8_t ordinalBytes[4];
      if (syn.read(ordinalBytes, sizeof(ordinalBytes)) != static_cast<int>(sizeof(ordinalBytes))) break;
      const int cmp = indexCmp(wordBuf, target);
      if (cmp == 0) {
        const uint32_t ordinal = readBe32(ordinalBytes);
        syn.close();
        sidx.close();
        return locateByOrdinal(idx, qidx, idxSize, idxSampleCount, ordinal, headwordOut);
      }
      if (cmp > 0) break;
    }
  }
  syn.close();
  sidx.close();
  return result;
}

std::string Dictionary::cleanWord(const char* word) {
  if (!word) return {};
  const auto* bytes = reinterpret_cast<const unsigned char*>(word);
  size_t begin = 0;
  size_t end = strlen(word);
  while (begin < end) {
    if (!isWordEdgeByte(bytes[begin]))
      ++begin;
    else if (end - begin >= 3 && bytes[begin] == 0xE2 && (bytes[begin + 1] == 0x80 || bytes[begin + 1] == 0x81))
      begin += 3;
    else
      break;
  }
  while (end > begin) {
    if (!isWordEdgeByte(bytes[end - 1]))
      --end;
    else if (end - begin >= 3 && bytes[end - 3] == 0xE2 && (bytes[end - 2] == 0x80 || bytes[end - 2] == 0x81))
      end -= 3;
    else
      break;
  }
  if (begin >= end) return {};

  std::string result(word + begin, end - begin);
  for (size_t i = 0; i < result.size(); ++i) {
    auto c = static_cast<unsigned char>(result[i]);
    if (c < 0x80) {
      result[i] = static_cast<char>(std::tolower(c));
      continue;
    }
    if (i + 1 >= result.size()) break;
    const auto next = static_cast<unsigned char>(result[i + 1]);
    // UTF-8 Russian uppercase: А-П, Р-Я and Ё.
    if (c == 0xD0 && next >= 0x90 && next <= 0x9F) {
      result[i + 1] = static_cast<char>(next + 0x20);
    } else if (c == 0xD0 && next >= 0xA0 && next <= 0xAF) {
      result[i] = static_cast<char>(0xD1);
      result[i + 1] = static_cast<char>(next - 0x20);
    } else if (c == 0xD0 && next == 0x81) {
      result[i] = static_cast<char>(0xD1);
      result[i + 1] = static_cast<char>(0x91);
    }
    ++i;
  }
  return result;
}

void Dictionary::stemVariants(const std::string& word, std::vector<std::string>& out) {
  out.clear();
  out.reserve(24);
  const size_t n = word.size();
  const auto add = [&out](std::string value) {
    if (std::find(out.begin(), out.end(), value) == out.end()) out.push_back(std::move(value));
  };
  const auto russianCaseVariant = [](const std::string& source, const bool firstOnly) {
    std::string value = source;
    bool changed = false;
    bool firstCodepoint = true;
    for (size_t i = 0; i + 1 < value.size(); ++i) {
      const auto lead = static_cast<unsigned char>(value[i]);
      const auto tail = static_cast<unsigned char>(value[i + 1]);
      if (!firstOnly || firstCodepoint) {
        if (lead == 0xD0 && tail >= 0xB0 && tail <= 0xBF) {
          value[i + 1] = static_cast<char>(tail - 0x20);  // а-п -> А-П
          changed = true;
        } else if (lead == 0xD1 && tail >= 0x80 && tail <= 0x8F) {
          value[i] = static_cast<char>(0xD0);  // р-я -> Р-Я
          value[i + 1] = static_cast<char>(tail + 0x20);
          changed = true;
        } else if (lead == 0xD1 && tail == 0x91) {
          value[i] = static_cast<char>(0xD0);  // ё -> Ё
          value[i + 1] = static_cast<char>(0x81);
          changed = true;
        }
      }
      if (lead >= 0xC0) {
        firstCodepoint = false;
        ++i;
      } else {
        firstCodepoint = false;
      }
    }
    return changed ? value : std::string();
  };

  // Russian StarDict collections use different key casing: lower-case (Dahl),
  // title-case (BSE), and all-caps (the Big Encyclopaedic Dictionary). Search
  // each spelling independently so the bytewise order of their .idx files stays
  // valid for the sampled binary search.
  const std::string titleCase = russianCaseVariant(word, true);
  if (!titleCase.empty()) add(titleCase);
  const std::string upperCase = russianCaseVariant(word, false);
  if (!upperCase.empty()) add(upperCase);

  const auto addRussianCandidate = [&add, &russianCaseVariant](std::string value) {
    add(value);
    std::string title = russianCaseVariant(value, true);
    if (!title.empty()) add(std::move(title));
    std::string upper = russianCaseVariant(value, false);
    if (!upper.empty()) add(std::move(upper));
  };

  std::string withoutYo = word;
  bool replacedYo = false;
  for (size_t i = 0; i + 1 < withoutYo.size(); ++i) {
    if (static_cast<unsigned char>(withoutYo[i]) == 0xD1 && static_cast<unsigned char>(withoutYo[i + 1]) == 0x91) {
      withoutYo[i] = static_cast<char>(0xD0);
      withoutYo[i + 1] = static_cast<char>(0xB5);  // ё -> е
      replacedYo = true;
      ++i;
    }
  }
  if (replacedYo) {
    add(withoutYo);
    const std::string savedWord = withoutYo;
    std::string title = savedWord;
    const auto lead = static_cast<unsigned char>(title[0]);
    const auto tail = title.size() > 1 ? static_cast<unsigned char>(title[1]) : 0;
    if (lead == 0xD0 && tail >= 0xB0 && tail <= 0xBF) title[1] = static_cast<char>(tail - 0x20);
    add(std::move(title));
    std::string upper = savedWord;
    for (size_t i = 0; i + 1 < upper.size(); ++i) {
      const auto upperLead = static_cast<unsigned char>(upper[i]);
      const auto upperTail = static_cast<unsigned char>(upper[i + 1]);
      if (upperLead == 0xD0 && upperTail >= 0xB0 && upperTail <= 0xBF) {
        upper[i + 1] = static_cast<char>(upperTail - 0x20);
      } else if (upperLead == 0xD1 && upperTail >= 0x80 && upperTail <= 0x8F) {
        upper[i] = static_cast<char>(0xD0);
        upper[i + 1] = static_cast<char>(upperTail + 0x20);
      }
      if (upperLead >= 0xC0) ++i;
    }
    add(std::move(upper));
  }

  // Lightweight Russian adjective lemmatisation. StarDict indexes contain the
  // masculine nominative form, while EPUB text commonly contains declined
  // forms (e.g. "назидательного" -> "назидательный"). Try all three valid
  // nominative endings because the stem alone cannot distinguish -ый/-ий/-ой.
  const auto addAdjectiveLemmas = [&addRussianCandidate](const std::string& stem) {
    addRussianCandidate(stem + "ый");
    addRussianCandidate(stem + "ий");
    addRussianCandidate(stem + "ой");
  };
  const auto addAdjectiveAndOcrLemmas = [&addAdjectiveLemmas](const std::string& stem) {
    addAdjectiveLemmas(stem);
    constexpr const char* OCR_ENDING = "етельн";
    const size_t endingBytes = strlen(OCR_ENDING);
    if (stem.size() >= endingBytes && stem.compare(stem.size() - endingBytes, endingBytes, OCR_ENDING) == 0) {
      constexpr const char* NORMAL_ENDING = "ательн";
      std::string corrected = stem;
      corrected.replace(corrected.size() - endingBytes, endingBytes, NORMAL_ENDING);
      addAdjectiveLemmas(corrected);
    }
  };
  const auto adjectiveStem = [&word, n](const char* suffix) -> std::string {
    const size_t len = strlen(suffix);
    return n > len && word.compare(n - len, len, suffix) == 0 ? word.substr(0, n - len) : std::string();
  };
  constexpr const char* LONG_ADJECTIVE_ENDINGS[] = {"ого", "его", "ому", "ему", "ыми", "ими"};
  bool adjectiveAdded = false;
  for (const char* suffix : LONG_ADJECTIVE_ENDINGS) {
    const std::string stem = adjectiveStem(suffix);
    if (!stem.empty()) {
      addAdjectiveAndOcrLemmas(stem);
      adjectiveAdded = true;
      break;
    }
  }
  if (!adjectiveAdded) {
    constexpr const char* SHORT_ADJECTIVE_ENDINGS[] = {"ая", "яя", "ую", "юю", "ые", "ие", "ых", "их", "ым", "им"};
    for (const char* suffix : SHORT_ADJECTIVE_ENDINGS) {
      const std::string stem = adjectiveStem(suffix);
      if (!stem.empty()) {
        addAdjectiveAndOcrLemmas(stem);
        break;
      }
    }
  }

  const auto ends = [&word, n](const char* suffix) {
    const size_t len = strlen(suffix);
    return n > len && word.compare(n - len, len, suffix) == 0;
  };
  if (ends("'s")) add(word.substr(0, n - 2));
  if (ends("ies")) add(word.substr(0, n - 3) + "y");
  if (ends("es")) add(word.substr(0, n - 2));
  if (ends("s")) add(word.substr(0, n - 1));
  if (ends("ed")) {
    add(word.substr(0, n - 2));
    add(word.substr(0, n - 1));
  }
  if (ends("ing")) {
    add(word.substr(0, n - 3));
    add(word.substr(0, n - 3) + "e");
  }
}

bool Dictionary::readDefinition(const Location& location, std::string& out, Result* resultOut) {
  if (!location.found) return false;
  const uint32_t size = std::min(location.size, MAX_DEFINITION_BYTES);
  if (size == 0) return false;
  const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_DEFAULT);
  if (largest < size + DEFINITION_HEADROOM) {
    if (resultOut) *resultOut = Result::LowMemory;
    return false;
  }
  HalFile dict;
  constexpr const char* TEMP_PATH = "/.crosspoint/dictionary-entry.tmp";
  bool temporary = false;
  if (hasPlainDict) {
    if (!Storage.openFileForRead("DICT", basePath + ".dict", dict)) {
      if (resultOut) *resultOut = Result::ReadError;
      return false;
    }
  } else {
    Storage.mkdir("/.crosspoint");
    Storage.remove(TEMP_PATH);
    HalFile extracted;
    if (!Storage.openFileForWrite("DICT", TEMP_PATH, extracted)) {
      if (resultOut) *resultOut = Result::ReadError;
      return false;
    }
    DictZip::ExtractError error = DictZip::ExtractError::None;
    const bool extractedOk =
        DictZip::extractEntry((basePath + ".dict.dz").c_str(), location.offset, size, extracted, &error);
    extracted.close();
    if (!extractedOk || !Storage.openFileForRead("DICT", TEMP_PATH, dict)) {
      Storage.remove(TEMP_PATH);
      if (resultOut) *resultOut = error == DictZip::ExtractError::LowMemory ? Result::LowMemory : Result::ReadError;
      return false;
    }
    temporary = true;
  }
  const uint32_t fileSize = static_cast<uint32_t>(dict.fileSize());
  const uint32_t readOffset = temporary ? 0 : location.offset;
  if (readOffset > fileSize || size > fileSize - readOffset || !dict.seekSet(readOffset)) {
    dict.close();
    if (temporary) Storage.remove(TEMP_PATH);
    if (resultOut) *resultOut = Result::ReadError;
    return false;
  }
  out.assign(size, '\0');
  if (dict.read(out.data(), size) != static_cast<int>(size)) {
    out.clear();
    dict.close();
    if (temporary) Storage.remove(TEMP_PATH);
    if (resultOut) *resultOut = Result::ReadError;
    return false;
  }
  dict.close();
  if (temporary) Storage.remove(TEMP_PATH);
  return true;
}

bool Dictionary::lookup(const char* word, std::string& definitionOut, std::string& headwordOut, Result* resultOut) {
  if (resultOut) *resultOut = Result::NotFound;
  if (basePath.empty()) return false;
  const std::string cleaned = cleanWord(word);
  if (cleaned.empty()) return false;

  HalFile idx;
  HalFile qidx;
  if (!Storage.openFileForRead("DICT", basePath + ".idx", idx) ||
      !Storage.openFileForRead("DICT", basePath + ".qidx", qidx)) {
    if (resultOut) *resultOut = Result::ReadError;
    return false;
  }
  const uint32_t idxSize = static_cast<uint32_t>(idx.fileSize());
  uint32_t sampleCount = 0;
  if (!readIndexHeader(qidx, QIDX_MAGIC, idxSize, sampleCount)) {
    if (resultOut) *resultOut = Result::ReadError;
    return false;
  }

  Location location = locate(idx, qidx, idxSize, sampleCount, cleaned.c_str(), headwordOut);
  if (!location.found && hasSyn) {
    location = locateSynonym(idx, qidx, idxSize, sampleCount, cleaned.c_str(), headwordOut);
  }
  if (!location.found) {
    std::vector<std::string> variants;
    stemVariants(cleaned, variants);
    for (const auto& variant : variants) {
      location = locate(idx, qidx, idxSize, sampleCount, variant.c_str(), headwordOut);
      if (location.found) break;
    }
  }
  idx.close();
  qidx.close();
  if (!location.found) return false;
  if (!readDefinition(location, definitionOut, resultOut)) return false;
  if (resultOut) *resultOut = Result::Found;
  return true;
}
