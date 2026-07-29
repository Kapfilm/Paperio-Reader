#pragma once

#include <string>

namespace ClippingsManager {
bool appendKindleExport(const std::string& bookTitle, const std::string& author, const std::string& chapterTitle,
                        int page, const std::string& text);
}
