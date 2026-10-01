#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "BufferedByteReader.h"

namespace {
class CountingByteReader final : public IByteReader {
 public:
  explicit CountingByteReader(size_t size = 24000) : bytes(size, '\0') {
    for (size_t i = 0; i < size; ++i) bytes[i] = static_cast<char>((i * 17 + i / 251) % 256);
  }
  size_t read(void* dst, size_t len) override {
    requests.push_back(len);
    if (failRead) return 0;
    const size_t got = std::min({len, maxRead, bytes.size() - pos});
    std::memcpy(dst, bytes.data() + pos, got);
    pos += got;
    return got;
  }
  bool seek(uint64_t offset) override {
    ++seeks;
    if (failSeek || offset > bytes.size()) return false;
    pos = static_cast<size_t>(offset);
    return true;
  }
  uint64_t tell() const override { return pos; }
  uint64_t size() const override { return bytes.size(); }
  std::string bytes;
  size_t pos = 0, seeks = 0;
  size_t maxRead = std::numeric_limits<size_t>::max();
  bool failSeek = false, failRead = false;
  std::vector<size_t> requests;
};

void expectRead(BufferedByteReader& reader, const CountingByteReader& source, size_t len) {
  const size_t start = reader.tell();
  std::string data(len, '\0');
  const size_t expected = std::min(len, source.bytes.size() - start);
  ASSERT_EQ(reader.read(data.data(), len), expected);
  EXPECT_EQ(data.substr(0, expected), source.bytes.substr(start, expected));
  EXPECT_EQ(reader.tell(), start + expected);
}

TEST(BufferedByteReaderTest, TitleThenBodyReusesFourKiBWithoutAnotherStorageRead) {
  CountingByteReader source;
  BufferedByteReader reader(source);
  ASSERT_TRUE(reader.seek(137));
  expectRead(reader, source, 2048);
  ASSERT_TRUE(reader.seek(137));
  expectRead(reader, source, 4096);
  EXPECT_EQ(source.requests, std::vector<size_t>({4096}));
  EXPECT_EQ(source.seeks, 1U);
}

TEST(BufferedByteReaderTest, CrossingCacheBoundaryPreservesBytesAndBackwardsSeek) {
  CountingByteReader source;
  BufferedByteReader reader(source);
  expectRead(reader, source, 2048);
  ASSERT_TRUE(reader.seek(3500));
  expectRead(reader, source, 2048);
  EXPECT_EQ(source.requests.size(), 2U);
  ASSERT_TRUE(reader.seek(4200));
  expectRead(reader, source, 1024);
  EXPECT_EQ(source.requests.size(), 2U);
  ASSERT_TRUE(reader.seek(100));
  expectRead(reader, source, 4096);
  EXPECT_EQ(source.requests.size(), 3U);
}

TEST(BufferedByteReaderTest, LargeScanReadsBypassReadAhead) {
  CountingByteReader source;
  BufferedByteReader reader(source);
  expectRead(reader, source, 8192);
  expectRead(reader, source, 8192);
  EXPECT_EQ(source.requests, std::vector<size_t>({8192, 8192}));
  EXPECT_EQ(source.seeks, 0U);
  ASSERT_TRUE(reader.seek(10));
  expectRead(reader, source, 2048);
  EXPECT_EQ(source.requests.back(), 4096U);
}

TEST(BufferedByteReaderTest, LargeReadConsumesCacheThenReadsRemainderDirectly) {
  CountingByteReader source;
  BufferedByteReader reader(source);
  expectRead(reader, source, 2048);
  expectRead(reader, source, 8192);
  EXPECT_EQ(source.requests, std::vector<size_t>({4096, 6144}));
  EXPECT_EQ(source.seeks, 0U);
}

TEST(BufferedByteReaderTest, ShortBackendReadsRefillWithoutSkippingBytes) {
  CountingByteReader source;
  source.maxRead = 613;
  BufferedByteReader reader(source);
  expectRead(reader, source, 4096);
  EXPECT_GT(source.requests.size(), 1U);
  ASSERT_TRUE(reader.seek(100));
  expectRead(reader, source, 2048);
}

TEST(BufferedByteReaderTest, ShortDirectReadReturnsOnlyDeliveredBytesIncludingCachePrefix) {
  CountingByteReader source;
  BufferedByteReader reader(source);
  expectRead(reader, source, 2048);
  source.maxRead = 300;
  std::string data(8192, '\0');
  EXPECT_EQ(reader.read(data.data(), data.size()), 2348U);
  EXPECT_EQ(data.substr(0, 2348), source.bytes.substr(2048, 2348));
  EXPECT_EQ(reader.tell(), 4396U);
  expectRead(reader, source, 2048);
}

TEST(BufferedByteReaderTest, FailedDeferredSeekKeepsLogicalPositionAndAllowsRetry) {
  CountingByteReader source;
  BufferedByteReader reader(source);
  expectRead(reader, source, 2048);
  ASSERT_TRUE(reader.seek(12000));
  source.failSeek = true;
  char data[4096];
  EXPECT_EQ(reader.read(data, sizeof(data)), 0U);
  EXPECT_EQ(reader.tell(), 12000U);
  source.failSeek = false;
  expectRead(reader, source, 4096);
}

TEST(BufferedByteReaderTest, BackendFailureReturnsCachedPrefixAndAllowsRetry) {
  CountingByteReader source;
  BufferedByteReader reader(source);
  expectRead(reader, source, 2048);
  source.failRead = true;
  char data[4096];
  EXPECT_EQ(reader.read(data, sizeof(data)), 2048U);
  EXPECT_EQ(std::string(data, 2048), source.bytes.substr(2048, 2048));
  EXPECT_EQ(reader.tell(), 4096U);
  source.failRead = false;
  expectRead(reader, source, 4096);
}

TEST(BufferedByteReaderTest, EndOfFileInvalidSeekAndEmptyReads) {
  CountingByteReader source(5000);
  BufferedByteReader reader(source);
  EXPECT_EQ(reader.read(nullptr, 100), 0U);
  char unused;
  EXPECT_EQ(reader.read(&unused, 0), 0U);
  EXPECT_TRUE(source.requests.empty());
  ASSERT_TRUE(reader.seek(4500));
  expectRead(reader, source, 2048);
  EXPECT_EQ(reader.read(&unused, 1), 0U);
  EXPECT_FALSE(reader.seek(5001));
  EXPECT_EQ(reader.tell(), 5000U);
  ASSERT_TRUE(reader.seek(4500));
  expectRead(reader, source, 500);
  EXPECT_EQ(source.requests.size(), 1U);
}
}  // namespace
