#include <gtest/gtest.h>

#include <array>
#include "src/util/BookProgressPages.h"

namespace {
using BookProgressPages::Format;
using Bytes = std::array<uint8_t, 11>;
void put32(Bytes& bytes, int offset, uint32_t value) {
  for (int i = 0; i < 4; ++i) bytes[offset + i] = (value >> (8 * i)) & 0xff;
}
void expectPages(Format format, const Bytes& bytes, int size, int expectedPage, int expectedTotal) {
  int page = -1, total = -1;
  ASSERT_TRUE(BookProgressPages::decode(format, bytes.data(), size, page, total));
  EXPECT_EQ(page, expectedPage);
  EXPECT_EQ(total, expectedTotal);
}
void expectInvalid(Format format, const Bytes& bytes, int size) {
  int page = 99, total = 99;
  EXPECT_FALSE(BookProgressPages::decode(format, bytes.data(), size, page, total));
  EXPECT_EQ(page, 0);
  EXPECT_EQ(total, 0);
}
TEST(BookProgressPages, EpubAndFb2UseBookEstimateRatherThanChapterNumbers) {
  Bytes data{7, 0, 2, 0, 8, 0, 20, 44, 1, 232, 3};
  expectPages(Format::Epub, data, 11, 300, 1000);
}
TEST(BookProgressPages, TextUsesOneBasedPageAndTailTotal) {
  Bytes data{43, 1, 255, 255, 255, 255, 30, 232, 3, 0, 0};
  expectPages(Format::Text, data, 9, 300, 1000);
}
TEST(BookProgressPages, XtcPreserves32BitCounts) {
  Bytes data{};
  put32(data, 0, 100000);
  put32(data, 5, 200000);
  expectPages(Format::Xtc, data, 9, 100001, 200000);
}
TEST(BookProgressPages, AllLegacyAndTruncatedRecordsHidePagePair) {
  Bytes data{};
  for (auto format : {Format::Epub, Format::Text, Format::Xtc}) {
    for (int size = -1; size < (format == Format::Epub ? 11 : 9); ++size) {
      expectInvalid(format, data, size);
    }
  }
}
TEST(BookProgressPages, NullInputIsRejected) {
  int page = 1, total = 1;
  EXPECT_FALSE(BookProgressPages::decode(Format::Epub, nullptr, 11, page, total));
  EXPECT_EQ(page, 0);
  EXPECT_EQ(total, 0);
}
TEST(BookProgressPages, EmptyAndUnknownTotalsAreRejected) {
  Bytes data{};
  for (auto format : {Format::Epub, Format::Text, Format::Xtc}) expectInvalid(format, data, 11);
  data[9] = 1;
  expectInvalid(Format::Epub, data, 11);  // EPUB current page must be one-based too.
}
TEST(BookProgressPages, FirstAndLastPageAreAccepted) {
  Bytes data{};
  data[7] = 1;
  data[9] = 1;
  expectPages(Format::Epub, data, 11, 1, 1);
  data = {};
  data[7] = 1;
  expectPages(Format::Text, data, 9, 1, 1);
  data = {};
  data[5] = 1;
  expectPages(Format::Xtc, data, 9, 1, 1);
}
TEST(BookProgressPages, PagesBeyondTotalAreRejected) {
  Bytes data{};
  data[7] = 2; data[9] = 1;
  expectInvalid(Format::Epub, data, 11);
  data = {}; data[0] = 1; data[7] = 1;
  expectInvalid(Format::Text, data, 9);
  data = {}; data[0] = 1; data[5] = 1;
  expectInvalid(Format::Xtc, data, 9);
}
TEST(BookProgressPages, Maximum16BitTotalsDoNotWrap) {
  Bytes data{};
  data[7] = data[8] = data[9] = data[10] = 255;
  expectPages(Format::Epub, data, 11, 65535, 65535);
  data = {}; data[0] = 254; data[1] = data[7] = data[8] = 255;
  expectPages(Format::Text, data, 9, 65535, 65535);
  data[0] = 255;
  expectInvalid(Format::Text, data, 9);
}
TEST(BookProgressPages, XtcIntegerLimitsAndOverflowAreChecked) {
  Bytes data{};
  put32(data, 0, INT_MAX - 1); put32(data, 5, INT_MAX);
  expectPages(Format::Xtc, data, 9, INT_MAX, INT_MAX);
  put32(data, 0, INT_MAX);
  expectInvalid(Format::Xtc, data, 9);
  put32(data, 0, 0); put32(data, 5, uint32_t(INT_MAX) + 1);
  expectInvalid(Format::Xtc, data, 9);
  put32(data, 0, UINT32_MAX); put32(data, 5, UINT32_MAX);
  expectInvalid(Format::Xtc, data, 9);
}
}  // namespace
