#pragma once

#include <cstddef>
#include <cstdint>

namespace panic_snapshot {

constexpr uint32_t WORDS_PER_LINE = 8;
constexpr uint32_t BYTES_PER_LINE = WORDS_PER_LINE * sizeof(uint32_t);

// Raw memory snapshot only, not a validated call stack. The SP must satisfy the
// architecture's stack alignment, but the last byte in a line is an ordinary
// RAM address, not another SP. Requiring SP alignment there would reject every
// valid 32-byte line (its last word is SP + 28, i.e. 12 modulo 16).
//
// The callbacks used by the panic handler are inline RAM-range checks and a
// volatile word read. No heap allocation, logging, or scheduler APIs are used.
template <typename Frame, typename SaneStackPointer, typename ReadableByte, typename ReadWord>
__attribute__((always_inline)) inline size_t capture(uint32_t sp, Frame* output, size_t capacity,
                                                     SaneStackPointer saneStackPointer, ReadableByte readableByte,
                                                     ReadWord readWord) {
  for (size_t row = 0; row < capacity; ++row) output[row].sp = 0;
  if ((sp & 0xFu) != 0 || !saneStackPointer(sp)) return 0;

  size_t count = 0;
  for (; count < capacity; ++count) {
    if (count > (UINT32_MAX - sp) / BYTES_PER_LINE) break;
    const uint32_t start = sp + static_cast<uint32_t>(count) * BYTES_PER_LINE;
    if (start > UINT32_MAX - (BYTES_PER_LINE - 1)) break;
    const uint32_t end = start + BYTES_PER_LINE - 1;
    if (!readableByte(start) || !readableByte(end)) break;

    output[count].sp = start;
    for (uint32_t word = 0; word < WORDS_PER_LINE; ++word) {
      output[count].spp[word] = readWord(start + word * sizeof(uint32_t));
    }
  }
  return count;
}

}  // namespace panic_snapshot
