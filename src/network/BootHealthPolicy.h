#pragma once
#include <cstdint>
#include <cstring>

namespace boot_health {
constexpr uint32_t kMagic = 0x42484731;
constexpr uint32_t kCrashLimit = 3;
constexpr uint32_t kHealthyMs = 60000;
constexpr uint32_t kMaxLoopGapMs = 5000;
struct ImageId {
  uint32_t address = 0;
  uint8_t sha[32] = {};
};
inline bool equal(const ImageId& a, const ImageId& b) {
  return a.address == b.address && std::memcmp(a.sha, b.sha, sizeof(a.sha)) == 0;
}
struct Record {
  uint32_t magic = kMagic;
  ImageId current;
  ImageId fallback;
  uint32_t failures = 0;
  uint32_t trusted = 0;
  uint32_t armed = 0;
  uint32_t blocked = 0;
  uint32_t observed = 0;
};
inline bool valid(const Record& r) {
  return r.magic == kMagic && r.failures <= kCrashLimit && r.trusted <= 1 && r.armed <= 1 && r.blocked <= 1 && r.observed <= 1;
}
inline Record onBoot(Record r, const ImageId& image, bool crash) {
  if (!valid(r) || !equal(r.current, image)) {
    r = Record{};
    r.current = image;
    // No provenance: never assume the other slot is a reader firmware.
  } else if (r.observed && crash && r.failures < kCrashLimit) {
    ++r.failures;
  }
  r.observed = 1;
  return r;
}
inline Record stageUpdate(const Record& running, const ImageId& target) {
  Record next;
  next.current = target;
  if (running.trusted && !running.blocked && running.failures == 0 && !equal(running.current, target)) {
    next.fallback = running.current;
    next.armed = 1;
  }
  return next;
}
class HealthWindow {
 public:
  bool tick(uint32_t now, bool ready, bool renderBusy) {
    if (!ready || (started && now - last > kMaxLoopGapMs)) started = false;
    last = now;
    if (!ready) return false;
    if (!started) { since = now; started = true; }
    // Rendering briefly is normal. A stuck renderer must never confirm a boot.
    if (!renderBusy) lastRenderIdle = now;
    if (renderBusy && now - lastRenderIdle > kMaxLoopGapMs) since = now;
    return !renderBusy && now - since >= kHealthyMs;
  }
 private:
  bool started = false;
  uint32_t since = 0, last = 0, lastRenderIdle = 0;
};
}  // namespace boot_health
