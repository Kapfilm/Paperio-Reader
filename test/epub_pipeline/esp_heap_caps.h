#pragma once
// Host stub for <esp_heap_caps.h>. Returns a large constant so every heap gate
// in the pipeline passes deterministically — control flow must not depend on
// the host machine's actual memory state.
#include <cstddef>
#include <cstdint>

#define MALLOC_CAP_8BIT (1 << 0)
#define MALLOC_CAP_DEFAULT (1 << 1)
#define MALLOC_CAP_INTERNAL (1 << 2)

inline size_t heap_caps_get_largest_free_block(uint32_t /*caps*/) { return 200 * 1024; }
inline size_t heap_caps_get_free_size(uint32_t /*caps*/) { return 300 * 1024; }
