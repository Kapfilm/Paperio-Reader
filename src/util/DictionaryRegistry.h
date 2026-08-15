#pragma once

#include <string>
#include <vector>

struct DictionaryEntry {
  std::string name;
  std::string stem;
  // Translation dictionaries such as Mueller should be tried before general
  // Russian dictionaries when the selected word is written in Latin script.
  bool prefersLatinInput = false;
};

namespace DictionaryRegistry {

// Finds one usable StarDict dictionary per subfolder under /dictionaries and
// /.dictionaries. A usable folder contains exactly one .idx stem and matching
// .dict or .dict.dz data.
void discover(std::vector<DictionaryEntry>& out);

// Resolves a persisted folder name to /<root>/<folder>/<stem>.
bool resolveBasePath(const char* folderName, std::string& basePathOut);

// Resolves the first discovered dictionary for callers that explicitly need a
// single fallback. Automatic reader lookup iterates discover() instead.
bool resolveFirst(std::string& folderNameOut, std::string& basePathOut);

}  // namespace DictionaryRegistry
