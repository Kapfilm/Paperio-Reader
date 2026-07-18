#include <gtest/gtest.h>

#include <cstdint>

#include "BuildArena.h"

namespace {

TEST(BuildArena, AllocatesAlignedAndBumps) {
  BuildArena arena(256);
  ASSERT_TRUE(arena.valid());

  auto* a = static_cast<uint8_t*>(arena.alloc(3, 1));
  auto* b = static_cast<uint8_t*>(arena.alloc(4, 4));
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(b) % 4, 0u);
  EXPECT_GE(b, a + 3);
  EXPECT_EQ(arena.used(), static_cast<size_t>((b - a) + 4));
}

TEST(BuildArena, DefaultAlignmentIsMaxAlign) {
  BuildArena arena(256);
  arena.alloc(1, 1);
  void* p = arena.alloc(8);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % alignof(std::max_align_t), 0u);
}

TEST(BuildArena, RefusesWhenFullAndRecordsShortfall) {
  BuildArena arena(64);
  ASSERT_NE(arena.alloc(48, 1), nullptr);
  EXPECT_EQ(arena.alloc(32, 1), nullptr);
  EXPECT_EQ(arena.failedAllocSize(), 32u);
  // A refused alloc leaves the cursor untouched: a smaller one still fits.
  EXPECT_NE(arena.alloc(16, 1), nullptr);
  EXPECT_EQ(arena.used(), 64u);
}

TEST(BuildArena, OverflowingAlignmentPaddingIsRefused) {
  BuildArena arena(64);
  ASSERT_NE(arena.alloc(63, 1), nullptr);
  // 1 byte left, but 8-byte alignment would pad past the end — must refuse,
  // not wrap.
  EXPECT_EQ(arena.alloc(1, 8), nullptr);
}

TEST(BuildArena, MarkReleaseReclaimsNested) {
  BuildArena arena(128);
  arena.alloc(16, 1);
  const size_t outer = arena.mark();
  arena.alloc(32, 1);
  const size_t inner = arena.mark();
  arena.alloc(32, 1);

  arena.release(inner);
  EXPECT_EQ(arena.used(), 48u);
  arena.release(outer);
  EXPECT_EQ(arena.used(), 16u);
  // Reclaimed space is reusable.
  EXPECT_NE(arena.alloc(100, 1), nullptr);
}

TEST(BuildArena, StaleMarkIsIgnored) {
  BuildArena arena(128);
  const size_t outer = arena.mark();
  arena.alloc(32, 1);
  const size_t inner = arena.mark();
  arena.release(outer);  // releases the outer scope first (out of order)
  arena.release(inner);  // stale: beyond the cursor now — must be a no-op
  EXPECT_EQ(arena.used(), 0u);
}

TEST(BuildArena, HighWaterTracksPeakAcrossReleaseAndReset) {
  BuildArena arena(128);
  const size_t m = arena.mark();
  arena.alloc(100, 1);
  arena.release(m);
  arena.alloc(10, 1);
  EXPECT_EQ(arena.highWater(), 100u);
  arena.reset();
  EXPECT_EQ(arena.used(), 0u);
  EXPECT_EQ(arena.highWater(), 100u);  // survives reset for post-build diagnostics
}

TEST(BuildArena, ExternalBufferIsUsedInPlace) {
  alignas(std::max_align_t) uint8_t buf[64];
  BuildArena arena(buf, sizeof(buf));
  ASSERT_TRUE(arena.valid());
  auto* p = static_cast<uint8_t*>(arena.alloc(8, 1));
  EXPECT_EQ(p, buf);
}

TEST(BuildArena, NullExternalBufferIsInvalidAndRefuses) {
  BuildArena arena(nullptr, 64);
  EXPECT_FALSE(arena.valid());
  EXPECT_EQ(arena.alloc(1, 1), nullptr);
  EXPECT_EQ(arena.failedAllocSize(), 1u);
}

}  // namespace
