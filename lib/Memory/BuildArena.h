#pragma once
// Bump arena for the section-build path (docs/compiled-book-pipeline-plan.md
// Phase 2). One up-front allocation sized to a named budget replaces the
// per-site free-heap gates: allocation inside the build becomes deterministic
// (either the arena fits the budget or the build refuses to start), so
// mid-build OOM/fragmentation surprises — the heap-recovery-restart class of
// bugs — cannot occur.
//
// Lifetime model (FreeInkBook-inspired):
//   - alloc() bump-allocates; there is NO per-object free, so fragmentation
//     cannot accumulate.
//   - mark()/release(m) scope transient work (inflate window, parse chunks):
//     save the cursor, work, restore. Releases MUST nest (release the newest
//     mark first); release() clamps and ignores a stale (out-of-order) mark in
//     release builds rather than corrupting the cursor.
//   - reset() discards everything at a build boundary.
//
// Diagnostics for host-side budget tests and device logs:
//   - highWater(): peak cursor ever reached (last successful state).
//   - failedAllocSize(): size of the last REFUSED allocation (0 = none) —
//     distinct from highWater(), which only records successes; together they
//     reproduce device OOM conditions exactly in host tests.
//
// Heap discipline: the backing buffer comes from makeUniqueNoThrow (never a
// throwing new); valid() must be checked before use.
#include <Memory.h>

#include <cstddef>
#include <cstdint>

class BuildArena {
 public:
  // Heap-backed arena. valid() is false when the buffer allocation failed —
  // callers treat that exactly like a refused build (retry released/smaller).
  explicit BuildArena(const size_t capacity) : capacity_(capacity) {
    owned_ = makeUniqueNoThrow<uint8_t[]>(capacity);
    base_ = owned_.get();
    if (!base_) capacity_ = 0;
  }

  // Caller-supplied buffer (e.g. a released framebuffer region). Not owned.
  BuildArena(uint8_t* buffer, const size_t capacity) : base_(buffer), capacity_(buffer ? capacity : 0) {}

  BuildArena(const BuildArena&) = delete;
  BuildArena& operator=(const BuildArena&) = delete;

  bool valid() const { return base_ != nullptr; }
  size_t capacity() const { return capacity_; }
  size_t used() const { return cursor_; }
  size_t highWater() const { return highWater_; }
  size_t failedAllocSize() const { return failedAllocSize_; }

  // Bump-allocate `bytes` aligned to `align` (power of two). Returns nullptr
  // when the request does not fit; the arena state is unchanged apart from
  // failedAllocSize() so a caller can log the exact shortfall.
  void* alloc(const size_t bytes, const size_t align = alignof(std::max_align_t)) {
    if (!base_) {
      failedAllocSize_ = bytes;
      return nullptr;
    }
    const size_t aligned = (cursor_ + (align - 1)) & ~(align - 1);
    if (aligned > capacity_ || bytes > capacity_ - aligned) {
      failedAllocSize_ = bytes;
      return nullptr;
    }
    cursor_ = aligned + bytes;
    if (cursor_ > highWater_) highWater_ = cursor_;
    return base_ + aligned;
  }

  // Scratch scoping: snapshot the cursor...
  size_t mark() const { return cursor_; }
  // ...and restore it, reclaiming everything allocated since. Marks must be
  // released newest-first; a mark beyond the current cursor is stale (its
  // scope was already released by an outer release) and is ignored.
  void release(const size_t mark) {
    if (mark <= cursor_) cursor_ = mark;
  }

  // Discard everything (build boundary). highWater/failedAllocSize survive so
  // post-build diagnostics can still report the peak.
  void reset() { cursor_ = 0; }

 private:
  std::unique_ptr<uint8_t[]> owned_;
  uint8_t* base_ = nullptr;
  size_t capacity_ = 0;
  size_t cursor_ = 0;
  size_t highWater_ = 0;
  size_t failedAllocSize_ = 0;
};
