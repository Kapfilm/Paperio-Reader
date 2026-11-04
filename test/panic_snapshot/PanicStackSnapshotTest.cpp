#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

#include "PanicStackSnapshot.h"

namespace {
constexpr uint32_t kDramLow = 0x3FC80000;
constexpr uint32_t kDramHigh = 0x3FCE0000;
struct Frame {
  uint32_t sp = 0;
  uint32_t spp[8] = {};
};

bool saneStack(uint32_t sp) {
  // Same public contract as the ESP32-C3 SDK: DRAM, 16-byte aligned,
  // with a 16-byte margin from either physical boundary.
  return sp >= kDramLow + 16 && sp <= kDramHigh - 16 && (sp & 15) == 0;
}

bool readableByte(uint32_t address) { return address >= kDramLow && address < kDramHigh; }

TEST(PanicStackSnapshot, BothReportedAlignedStacksCaptureAllRawWords) {
  for (uint32_t sp : {0x3FCA82B0u, 0x3FCA81A0u}) {
    std::array<Frame, 32> output{};
    std::vector<uint32_t> reads;
    ASSERT_TRUE(saneStack(sp));
    ASSERT_FALSE(saneStack(sp + 28));  // Old code rejected this ordinary word address.
    const size_t count =
        panic_snapshot::capture(sp, output.data(), output.size(), saneStack, readableByte, [&](uint32_t address) {
          reads.push_back(address);
          return address ^ 0xA5A5A5A5;
        });
    ASSERT_EQ(count, 32u);
    ASSERT_EQ(reads.size(), 256u);
    EXPECT_EQ(reads.front(), sp);
    EXPECT_EQ(reads.back(), sp + 1020);
    for (size_t row = 0; row < output.size(); ++row) {
      EXPECT_EQ(output[row].sp, sp + row * 32);
      for (size_t word = 0; word < 8; ++word) {
        EXPECT_EQ(output[row].spp[word], (sp + row * 32 + word * 4) ^ 0xA5A5A5A5);
      }
    }
  }
}

TEST(PanicStackSnapshot, InvalidOrMisalignedStacksAreNeverDereferenced) {
  for (uint32_t sp : {0u, 0xAD59u, kDramLow, kDramLow + 4, kDramHigh, 0x3FCA82B4u}) {
    std::array<Frame, 32> output{};
    for (auto& frame : output) frame.sp = 123;
    size_t reads = 0;
    EXPECT_EQ(panic_snapshot::capture(sp, output.data(), output.size(), saneStack, readableByte,
                                      [&](uint32_t) { return ++reads; }),
              0u);
    EXPECT_EQ(reads, 0u);
    for (const auto& frame : output) EXPECT_EQ(frame.sp, 0u);
  }
}

TEST(PanicStackSnapshot, StopsBeforeReadingPastDramBoundary) {
  for (uint32_t sp : {kDramHigh - 32, kDramHigh - 16}) {
    std::array<Frame, 32> output{};
    std::vector<uint32_t> reads;
    const size_t count =
        panic_snapshot::capture(sp, output.data(), output.size(), saneStack, readableByte, [&](uint32_t address) {
          reads.push_back(address);
          return 0;
        });
    EXPECT_EQ(count, sp == kDramHigh - 32 ? 1u : 0u);
    for (uint32_t address : reads) {
      EXPECT_GE(address, kDramLow);
      EXPECT_LE(address, kDramHigh - 4);
    }
    if (!reads.empty()) EXPECT_EQ(reads.back(), kDramHigh - 4);
    EXPECT_EQ(output[count].sp, 0u);
  }
}

TEST(PanicStackSnapshot, RejectsAddressArithmeticOverflowBeforeReading) {
  std::array<Frame, 32> output{};
  size_t reads = 0;
  const size_t count = panic_snapshot::capture(
      0xFFFFFFF0, output.data(), output.size(), [](uint32_t) { return true; }, [](uint32_t) { return true; },
      [&](uint32_t) { return ++reads; });
  EXPECT_EQ(count, 0u);
  EXPECT_EQ(reads, 0u);
}
}  // namespace
