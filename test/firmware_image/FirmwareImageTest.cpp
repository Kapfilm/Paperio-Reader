#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>
#include "src/network/FirmwareFlasher.h"
#include "src/network/FirmwareImageValidator.h"
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/sha.h>
#endif

using firmware_flash::Result;
namespace {
struct Sha {
  std::vector<uint8_t> bytes;
  bool fail = false;
  bool update(const uint8_t* data, size_t size) {
    bytes.insert(bytes.end(), data, data + size);
    return !fail;
  }
  bool finish(uint8_t* digest) {
#ifdef __APPLE__
    CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest);
#else
    SHA256(bytes.data(), bytes.size(), digest);
#endif
    return !fail;
  }
};
std::vector<uint8_t> image(bool hashed = true, size_t payload = 65537) {
  std::vector<uint8_t> result(32 + payload);
  result[0] = 0xE9; result[1] = 1; result[12] = 5; result[23] = hashed;
  for (int i = 0; i < 4; ++i) result[28 + i] = (payload >> (8 * i)) & 255;
  uint8_t checksum = 0xEF;
  for (size_t i = 32; i < result.size(); ++i) {
    result[i] = static_cast<uint8_t>(i * 17); checksum ^= result[i];
  }
  result.resize(result.size() + 16 - result.size() % 16);
  result.back() = checksum;
  if (hashed) {
    Sha sha;
    sha.update(result.data(), result.size());
    std::array<uint8_t, 32> hash;
    sha.finish(hash.data());
    result.insert(result.end(), hash.begin(), hash.end());
  }
  return result;
}
Result validate(const std::vector<uint8_t>& flash, size_t* size = nullptr, size_t failOffset = SIZE_MAX,
                bool failSha = false) {
  Sha sha; sha.fail = failSha;
  uint8_t scratch[257];
  auto read = [&](size_t offset, uint8_t* bytes, size_t count) {
    EXPECT_LE(offset, flash.size());
    EXPECT_LE(count, flash.size() - offset);
    if (offset >= failOffset || offset > flash.size() || count > flash.size() - offset) return false;
    std::memcpy(bytes, flash.data() + offset, count);
    return true;
  };
  return firmware_flash::detail::validatePartitionImage<Result>(flash.size(), read, sha, scratch, sizeof(scratch), size);
}
}
TEST(FirmwareImage, ValidImageWithUnusedPartitionSpace) {
  auto flash = image(); size_t actual = flash.size();
  flash.resize(100000, 0xFF);
  size_t size = 0;
  EXPECT_EQ(validate(flash, &size), Result::OK); EXPECT_EQ(size, actual);
}
TEST(FirmwareImage, UnhashedImage) { EXPECT_EQ(validate(image(false)), Result::OK); }
TEST(FirmwareImage, AlignedPayloadStillIncludesChecksumBlock) { EXPECT_EQ(validate(image(true, 65536)), Result::OK); }
TEST(FirmwareImage, ErasedPartition) {
  EXPECT_EQ(validate(std::vector<uint8_t>(65536, 0xFF)), Result::BAD_MAGIC);
}
TEST(FirmwareImage, HeaderTruncated) { EXPECT_EQ(validate(std::vector<uint8_t>(23)), Result::TOO_SMALL); }
TEST(FirmwareImage, WrongChipAndHighChipByte) {
  auto flash = image(); flash[12] = 9; EXPECT_EQ(validate(flash), Result::BAD_CHIP);
  flash[12] = 5; flash[13] = 1; EXPECT_EQ(validate(flash), Result::BAD_CHIP);
}
TEST(FirmwareImage, InvalidSegmentCount) {
  auto flash = image(); flash[1] = 0; EXPECT_EQ(validate(flash), Result::BAD_SEGMENTS);
  flash[1] = 17; EXPECT_EQ(validate(flash), Result::BAD_SEGMENTS);
}
TEST(FirmwareImage, OversizedSegmentDoesNotWrapOrReadOutsidePartition) {
  auto flash = image(); std::fill(flash.begin() + 28, flash.begin() + 32, 0xFF);
  EXPECT_EQ(validate(flash), Result::BAD_SEGMENTS);
}
TEST(FirmwareImage, TruncatedSegmentHeader) {
  auto flash = image(); flash.resize(30); EXPECT_EQ(validate(flash), Result::BAD_SEGMENTS);
}
TEST(FirmwareImage, TruncatedPayload) {
  auto flash = image(); flash.resize(400); EXPECT_EQ(validate(flash), Result::BAD_SEGMENTS);
}
TEST(FirmwareImage, TruncatedPaddingOrDigest) {
  auto flash = image(); flash.resize(32 + 65537); EXPECT_EQ(validate(flash), Result::BAD_SIZE);
  flash = image(); flash.pop_back(); EXPECT_EQ(validate(flash), Result::BAD_SIZE);
}
TEST(FirmwareImage, CorruptPayloadAndChecksum) {
  auto flash = image(); flash[35] ^= 1; EXPECT_EQ(validate(flash), Result::BAD_CHECKSUM);
  flash = image(); flash[flash.size() - 33] ^= 1; EXPECT_EQ(validate(flash), Result::BAD_CHECKSUM);
}
TEST(FirmwareImage, XorPreservingCorruptionCaughtBySha) {
  auto flash = image(); flash[35] ^= 1; flash[36] ^= 1; EXPECT_EQ(validate(flash), Result::BAD_SHA);
}
TEST(FirmwareImage, CorruptHash) {
  auto flash = image(); flash.back() ^= 1; EXPECT_EQ(validate(flash), Result::BAD_SHA);
}
TEST(FirmwareImage, InvalidHashFlag) {
  auto flash = image(); flash[23] = 2; EXPECT_EQ(validate(flash), Result::BAD_SIZE);
}
TEST(FirmwareImage, ReadFailureClearsOutputSize) {
  size_t size = 123;
  EXPECT_EQ(validate(image(), &size, 32), Result::READ_FAIL); EXPECT_EQ(size, 0u);
}
TEST(FirmwareImage, ShaFailureIsNotAccepted) { EXPECT_EQ(validate(image(), nullptr, SIZE_MAX, true), Result::BAD_SHA); }

TEST(FirmwareImage, BuiltFirmwareWhenProvided) {
  const char* path = std::getenv("FIRMWARE_IMAGE_TEST_PATH");
  if (!path) GTEST_SKIP() << "Set FIRMWARE_IMAGE_TEST_PATH to validate a complete production BIN";
  std::ifstream file(path, std::ios::binary);
  ASSERT_TRUE(file.good());
  std::vector<uint8_t> flash((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  const size_t originalSize = flash.size();
  flash.resize(originalSize + 4096, 0xFF);
  size_t imageSize = 0;
  EXPECT_EQ(validate(flash, &imageSize), Result::OK);
  EXPECT_EQ(imageSize, originalSize);
}
