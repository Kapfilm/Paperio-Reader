#pragma once

#include <vector>

#include "activities/ActivityResult.h"
#include "activities/reader/WordRef.h"

namespace ClipTextBuilder {
ClippingResult build(const std::vector<WordRef>& words, int from, int to, int startPage, int pageCount);
ClippingResult buildDictionaryWord(const std::vector<WordRef>& words, int cursor, int startPage, int pageCount);
}
