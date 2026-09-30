#pragma once

#include <cstdint>
#include <string>
#include <string_view>

enum class BookArchiveType : uint8_t {
  None,
  Epub,
  Fb2,
};

// Cheap extension checks used by UI paths. A generic .zip is accepted by the
// browser and then routed by detectBookArchiveType() from its central directory.
bool isFb2BookPath(std::string_view path);
bool isBookZipPath(std::string_view path);
bool isFb2OrZipBookPath(std::string_view path);

// Inspects ZIP metadata only; chapter/image data is not inflated.
BookArchiveType detectBookArchiveType(const std::string& path);
