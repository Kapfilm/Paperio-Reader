#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "BufferedFb2ScanStorage.h"
#include "Fb2Parser.h"

namespace {
class CountingStorage final : public Fb2ScanStorage {
 public:
  std::vector<uint8_t> bytes[2];
  size_t reads = 0, writes = 0;
  bool failRead = false, failWrite = false;
  bool read(bool strings, uint32_t offset, void* data, size_t size) override {
    ++reads;
    if (failRead || offset > bytes[strings].size() || size > bytes[strings].size() - offset) return false;
    std::memcpy(data, bytes[strings].data() + offset, size);
    return true;
  }
  bool write(bool strings, uint32_t offset, const void* data, size_t size) override {
    ++writes;
    if (failWrite || offset > bytes[strings].size()) return false;
    bytes[strings].resize(std::max(bytes[strings].size(), offset + size));
    std::memcpy(bytes[strings].data() + offset, data, size);
    return true;
  }
};
class MemorySource final : public IByteReader {
 public:
  explicit MemorySource(const std::string& bytes) : bytes_(bytes) {}
  size_t read(void* dst, size_t size) override {
    size = std::min(size, bytes_.size() - position_);
    std::memcpy(dst, bytes_.data() + position_, size);
    position_ += size;
    return size;
  }
  bool seek(uint64_t offset) override {
    if (offset > bytes_.size()) return false;
    position_ = offset;
    return true;
  }
  uint64_t tell() const override { return position_; }
  uint64_t size() const override { return bytes_.size(); }
 private:
  const std::string& bytes_;
  size_t position_ = 0;
};

void verifyScan(const std::string& xml) {
  auto referenceStorage = std::make_shared<CountingStorage>();
  auto backing = std::make_shared<CountingStorage>();
  auto buffered = std::make_shared<BufferedFb2ScanStorage>(backing);
  Fb2ScanResult reference, actual;
  reference.sections.storage = referenceStorage;
  actual.sections.storage = buffered;
  MemorySource refSource(xml), actualSource(xml);
  Fb2Parser parser;
  ASSERT_TRUE(parser.scan(refSource, reference));
  ASSERT_TRUE(parser.scan(actualSource, actual));
  ASSERT_TRUE(buffered->flush());
  ASSERT_EQ(reference.sections.size(), actual.sections.size());
  for (size_t i = 0; i < reference.sections.size(); ++i) {
    const auto a = reference.sections[i];
    const auto b = actual.sections[i];
    EXPECT_EQ(a.innerStartOffset, b.innerStartOffset);
    EXPECT_EQ(a.level, b.level);
    EXPECT_EQ(a.bodyIndex, b.bodyIndex);
    EXPECT_EQ(a.approxTextBytes, b.approxTextBytes);
    EXPECT_EQ(a.imageRefCount, b.imageRefCount);
    EXPECT_EQ(a.fallbackTitle, b.fallbackTitle);
    EXPECT_EQ(reference.sectionTitle(a), actual.sectionTitle(b));
    EXPECT_EQ(reference.sectionId(a), actual.sectionId(b));
  }
  ASSERT_TRUE(actual.good());
  ASSERT_TRUE(buffered->flush());
  // The synthetic/real books have thousands of short records. Buffering must
  // reduce actual backend calls, not just retain a complete index in memory.
  EXPECT_LT(backing->reads + backing->writes, (referenceStorage->reads + referenceStorage->writes) / 3);
  EXPECT_EQ(referenceStorage->bytes[0], backing->bytes[0]);
  EXPECT_EQ(referenceStorage->bytes[1], backing->bytes[1]);
  std::cout << "sections=" << actual.sections.size() << " unbuffered IO="
            << referenceStorage->reads + referenceStorage->writes << " buffered IO="
            << backing->reads + backing->writes << std::endl;
}
}  // namespace

TEST(BufferedFb2ScanStorageTest, CrossPageReadModifyAppendAndSeparateFiles) {
  auto backing = std::make_shared<CountingStorage>();
  BufferedFb2ScanStorage cache(backing);
  std::vector<uint8_t> expected(12000);
  for (size_t i = 0; i < expected.size(); ++i) expected[i] = i % 251;
  ASSERT_TRUE(cache.write(false, 0, expected.data(), expected.size()));
  const std::string title = "A separate title";
  ASSERT_TRUE(cache.write(true, 0, title.data(), title.size()));
  const std::vector<uint8_t> patch(50, 77);
  ASSERT_TRUE(cache.write(false, 4080, patch.data(), patch.size()));
  std::copy(patch.begin(), patch.end(), expected.begin() + 4080);
  const uint8_t last = 201;
  ASSERT_TRUE(cache.write(false, expected.size(), &last, 1));
  expected.push_back(last);
  std::vector<uint8_t> got(expected.size());
  ASSERT_TRUE(cache.read(false, 0, got.data(), got.size()));
  EXPECT_EQ(got, expected);
  ASSERT_TRUE(cache.flush());
  EXPECT_EQ(backing->bytes[0], expected);
  EXPECT_EQ(std::string(backing->bytes[1].begin(), backing->bytes[1].end()), title);
  EXPECT_FALSE(cache.read(false, expected.size(), &got[0], 1));
  EXPECT_FALSE(cache.write(false, expected.size() + 1, &last, 1));
}

TEST(BufferedFb2ScanStorageTest, DelayedWriteFailureIsReportedAndSticky) {
  auto backing = std::make_shared<CountingStorage>();
  BufferedFb2ScanStorage cache(backing);
  const uint8_t value = 7;
  ASSERT_TRUE(cache.write(false, 0, &value, 1));
  EXPECT_EQ(backing->writes, 0U);
  backing->failWrite = true;
  EXPECT_FALSE(cache.flush());
  EXPECT_FALSE(cache.good());
  backing->failWrite = false;
  EXPECT_FALSE(cache.write(false, 1, &value, 1));
  EXPECT_FALSE(cache.flush());
}

TEST(BufferedFb2ScanStorageTest, BackendReadFailureIsReportedAndSticky) {
  auto backing = std::make_shared<CountingStorage>();
  BufferedFb2ScanStorage cache(backing);
  std::vector<uint8_t> values(9000, 9);
  ASSERT_TRUE(cache.write(false, 0, values.data(), values.size()));
  backing->failRead = true;
  EXPECT_FALSE(cache.read(false, 0, values.data(), 1));
  EXPECT_FALSE(cache.good());
}

TEST(BufferedFb2ScanStorageTest, NestedScanPreservesParentsTitlesAndOffsets) {
  std::string xml = "<FictionBook><body>";
  for (size_t i = 0; i < 100; ++i) {
    xml += "<section id='parent" + std::to_string(i) + "'><title><p>Parent " + std::to_string(i) + "</p></title>";
    for (size_t j = 0; j < 40; ++j) {
      xml += "<section id='child" + std::to_string(i * 40 + j) + "'><title><p>Child " + std::to_string(j) + "</p></title><p>text</p></section>";
    }
    xml += "<p>Parent closing text</p></section>";
  }
  xml += "</body></FictionBook>";
  verifyScan(xml);
}

TEST(BufferedFb2ScanStorageTest, OptionalLocalBookMatchesUnbufferedIndex) {
  const char* path = std::getenv("FB2_REGRESSION_BOOK");
  if (!path) GTEST_SKIP() << "Set FB2_REGRESSION_BOOK to test the local book";
  std::ifstream file(path, std::ios::binary);
  ASSERT_TRUE(file.good());
  const std::string xml{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  verifyScan(xml);
}
