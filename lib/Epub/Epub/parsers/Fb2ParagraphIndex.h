#pragma once
#include <cstdint>

// Private generated FB2 attribute; never apply it to user-provided EPUB XHTML.
inline uint16_t parseFb2SourceParagraph(const char* value) {
  if (!value || !*value) return 0;
  uint32_t parsed = 0;
  for (; *value; ++value) {
    if (*value < '0' || *value > '9') return 0;
    parsed = parsed * 10 + (*value - '0');
    if (parsed > UINT16_MAX) return 0;
  }
  return static_cast<uint16_t>(parsed);
}
