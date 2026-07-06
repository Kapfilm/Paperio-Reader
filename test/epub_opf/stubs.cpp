#include <string>
#include <vector>

#include "../../lib/Epub/Epub/BookMetadataCache.h"

namespace opf_test_hooks {
std::vector<std::string>* g_spineHrefSink = nullptr;
}

void BookMetadataCache::createSpineEntry(const std::string& href) {
  if (opf_test_hooks::g_spineHrefSink != nullptr) {
    opf_test_hooks::g_spineHrefSink->push_back(href);
  }
}
