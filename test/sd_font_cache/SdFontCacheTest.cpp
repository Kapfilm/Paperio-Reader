#include <FlashFontPartition.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <SdCardFont.h>
#include <SdCardFontManager.h>
#include <SdCardFontRegistry.h>
#include <gtest/gtest.h>

#include <set>

namespace {
size_t capacity = 0;
int erases = 0;
int entryQueries = 0;
bool mapped = false;
bool writesSucceed = true;
std::set<uint8_t> cached;
std::set<uint8_t> appended;
uint8_t payload = 0;
class SdFontCacheTest : public ::testing::Test {
 protected:
  GfxRenderer renderer;
  SdCardFontManager manager;
  SdCardFontFamilyInfo family{"Family", {{"/font16.cpfont", 16}, {"/font18.cpfont", 18}}};
  int notices = 0;
  void SetUp() override {
    capacity = 0;
    erases = 0;
    entryQueries = 0;
    mapped = false;
    writesSucceed = true;
    cached.clear();
    appended.clear();
    font_test::files = {{"/font16.cpfont", 1000}, {"/font18.cpfont", 1200}};
    SdCardFont::sdLoads = 0;
    SdCardFont::mmapLoads = 0;
  }
  bool load() {
    return manager.loadFamily(family, renderer, 16, [this] { ++notices; });
  }
};
}  // namespace
namespace FlashFontPartition {
size_t capacityBytes() { return capacity; }
bool hasEntry(const char*, uint8_t point) {
  ++entryQueries;
  return cached.count(point);
}
bool isMapped() { return mapped; }
void unmap() { mapped = false; }
bool beginWrite(const char*) {
  ++erases;
  appended.clear();
  return writesSucceed;
}
bool appendFile(const char*, const char*, uint8_t point) {
  appended.insert(point);
  return writesSucceed;
}
bool finaliseWrite() {
  if (writesSucceed) cached = appended;
  return writesSucceed;
}
bool mmap(const char*, uint8_t point, const uint8_t** out, size_t* size) {
  if (!cached.count(point)) return false;
  *out = &payload;
  *size = 1;
  mapped = true;
  return true;
}
}  // namespace FlashFontPartition
TEST_F(SdFontCacheTest, MissingPartitionUsesSdWithoutLoadingPopupOrFlashAttempt) {
  ASSERT_TRUE(load());
  EXPECT_EQ(notices, 0);
  EXPECT_EQ(erases, 0);
  EXPECT_EQ(entryQueries, 0);
  EXPECT_EQ(SdCardFont::sdLoads, 1);
  manager.unloadAll(renderer);
  ASSERT_TRUE(load());
  EXPECT_EQ(notices, 0);
  EXPECT_EQ(erases, 0);
  EXPECT_EQ(SdCardFont::sdLoads, 2);
}
TEST_F(SdFontCacheTest, RequestedFontTooLargeKeepsExistingCacheAndUsesSd) {
  capacity = FlashFontPartition::HEADER_BYTES + 999;
  cached.insert(18);
  ASSERT_TRUE(load());
  EXPECT_EQ(notices, 0);
  EXPECT_EQ(erases, 0);
  EXPECT_EQ(cached.count(18), 1u);
  EXPECT_EQ(SdCardFont::sdLoads, 1);
}
TEST_F(SdFontCacheTest, ColdWritableCacheShowsOnePopupThenWarmReopenMaps) {
  capacity = 8192;
  ASSERT_TRUE(load());
  EXPECT_EQ(notices, 1);
  EXPECT_EQ(erases, 1);
  EXPECT_EQ(cached.size(), 2u);
  EXPECT_EQ(SdCardFont::mmapLoads, 1);
  manager.unloadAll(renderer);
  ASSERT_TRUE(load());
  EXPECT_EQ(notices, 1);
  EXPECT_EQ(erases, 1);
  EXPECT_EQ(SdCardFont::mmapLoads, 2);
}
TEST_F(SdFontCacheTest, SmallActualPartitionCachesOnlyRequestedSize) {
  capacity = FlashFontPartition::HEADER_BYTES + 1100;
  ASSERT_TRUE(load());
  EXPECT_EQ(notices, 1);
  EXPECT_EQ(erases, 1);
  EXPECT_EQ(cached, std::set<uint8_t>({16}));
}
TEST_F(SdFontCacheTest, ExistingEntryStillMapsWhenSourceFileIsMissing) {
  capacity = 8192;
  cached.insert(16);
  font_test::files.clear();
  ASSERT_TRUE(load());
  EXPECT_EQ(notices, 0);
  EXPECT_EQ(erases, 0);
  EXPECT_EQ(SdCardFont::mmapLoads, 1);
}
TEST_F(SdFontCacheTest, FlashWriteFailureStillLoadsFromSd) {
  capacity = 8192;
  writesSucceed = false;
  ASSERT_TRUE(load());
  EXPECT_EQ(notices, 1);
  EXPECT_EQ(erases, 1);
  EXPECT_EQ(SdCardFont::sdLoads, 1);
}
TEST_F(SdFontCacheTest, MissingRequestedSourceDoesNotEraseFlash) {
  capacity = 8192;
  font_test::files.erase("/font16.cpfont");
  EXPECT_FALSE(load());
  EXPECT_EQ(notices, 0);
  EXPECT_EQ(erases, 0);
}
