#pragma once
// Host stub for <esp_system.h>. Large constant free-heap so heap gates pass
// deterministically (see esp_heap_caps.h stub). Pulls in the Arduino shim
// because device code that includes esp headers gets millis() implicitly from
// the Arduino core.
#include <Arduino.h>

#include <cstdint>

inline uint32_t esp_get_free_heap_size() { return 300 * 1024; }
