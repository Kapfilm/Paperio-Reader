#include "DictionaryRegistry.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace DictionaryRegistry {
namespace {

constexpr const char* DICT_ROOTS[] = {"/dictionaries", "/.dictionaries"};

int asciiCaseCmp(const char* a, const char* b) {
  while (*a && *b) {
    const int ca = std::tolower(static_cast<unsigned char>(*a));
    const int cb = std::tolower(static_cast<unsigned char>(*b));
    if (ca != cb) return ca - cb;
    ++a;
    ++b;
  }
  return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}

bool asciiCaseContains(const char* text, const char* needle) {
  if (!text || !needle || *needle == '\0') return false;
  const size_t needleLength = strlen(needle);
  for (; *text; ++text) {
    size_t i = 0;
    while (i < needleLength && text[i] &&
           std::tolower(static_cast<unsigned char>(text[i])) == std::tolower(static_cast<unsigned char>(needle[i]))) {
      ++i;
    }
    if (i == needleLength) return true;
  }
  return false;
}

bool prefersLatinInput(const char* folderName, const char* stem) {
  constexpr const char* ASCII_MARKERS[] = {"mueller", "en-ru", "en_ru", "eng-rus", "english-russian"};
  if (std::any_of(ASCII_MARKERS, ASCII_MARKERS + std::size(ASCII_MARKERS), [folderName, stem](const char* marker) {
        return asciiCaseContains(folderName, marker) || asciiCaseContains(stem, marker);
      }))
    return true;
  // Common Russian names for English-to-Russian dictionaries. UTF-8 is
  // checked byte-for-byte in both title-case forms; no locale tables needed.
  return strstr(folderName, "Англо") || strstr(folderName, "англо") || strstr(folderName, "Английско") ||
         strstr(folderName, "английско") || strstr(folderName, "Мюллер") || strstr(folderName, "мюллер");
}

bool findStem(const char* folderPath, std::string& stemOut) {
  auto dir = Storage.open(folderPath);
  if (!dir || !dir.isDirectory()) return false;

  dir.rewindDirectory();
  char name[128] = {};
  char foundStem[128] = {};
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    entry.getName(name, sizeof(name));
    if (entry.isDirectory() || strncmp(name, "._", 2) == 0) continue;
    const size_t len = strlen(name);
    if (len <= 4 || strcmp(name + len - 4, ".idx") != 0) continue;
    name[len - 4] = '\0';
    if (foundStem[0] != '\0' && strcmp(foundStem, name) != 0) {
      LOG_DBG("DREG", "Skipping ambiguous dictionary folder %s", folderPath);
      return false;
    }
    strncpy(foundStem, name, sizeof(foundStem) - 1);
  }
  if (foundStem[0] == '\0') return false;

  char base[192];
  const int n = snprintf(base, sizeof(base), "%s/%s", folderPath, foundStem);
  if (n < 0 || static_cast<size_t>(n) >= sizeof(base)) return false;
  char path[208];
  snprintf(path, sizeof(path), "%s.dict", base);
  const bool hasPlain = Storage.exists(path);
  snprintf(path, sizeof(path), "%s.dict.dz", base);
  if (!hasPlain && !Storage.exists(path)) return false;

  stemOut = foundStem;
  return true;
}

}  // namespace

void discover(std::vector<DictionaryEntry>& out) {
  out.clear();
  out.reserve(8);
  for (const char* root : DICT_ROOTS) {
    auto dir = Storage.open(root);
    if (!dir || !dir.isDirectory()) continue;
    dir.rewindDirectory();
    char name[128] = {};
    for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      entry.getName(name, sizeof(name));
      if (!entry.isDirectory() || name[0] == '.') continue;
      char folderPath[192];
      const int n = snprintf(folderPath, sizeof(folderPath), "%s/%s", root, name);
      if (n < 0 || static_cast<size_t>(n) >= sizeof(folderPath)) continue;
      std::string stem;
      if (!findStem(folderPath, stem)) continue;
      const bool latinInput = prefersLatinInput(name, stem.c_str());
      out.push_back({name, std::move(stem), latinInput});
    }
  }
  std::sort(out.begin(), out.end(), [](const DictionaryEntry& a, const DictionaryEntry& b) {
    return asciiCaseCmp(a.name.c_str(), b.name.c_str()) < 0;
  });
}

bool resolveBasePath(const char* folderName, std::string& basePathOut) {
  if (!folderName || folderName[0] == '\0' || folderName[0] == '.' || strpbrk(folderName, "/\\")) return false;
  for (const char* root : DICT_ROOTS) {
    char folderPath[192];
    const int n = snprintf(folderPath, sizeof(folderPath), "%s/%s", root, folderName);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(folderPath)) continue;
    std::string stem;
    if (!findStem(folderPath, stem)) continue;
    basePathOut = std::string(folderPath) + "/" + stem;
    return true;
  }
  return false;
}

bool resolveFirst(std::string& folderNameOut, std::string& basePathOut) {
  std::vector<DictionaryEntry> entries;
  discover(entries);
  if (entries.empty()) return false;
  folderNameOut = entries.front().name;
  return resolveBasePath(folderNameOut.c_str(), basePathOut);
}

}  // namespace DictionaryRegistry
