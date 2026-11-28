#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "I18nOffset.h"

TEST(I18nOffset, LocalZeroStartsAtLocalBlob) {
  const char local[] = "local-zero\0local-next";
  const char english[] = "english-zero\0english-next";
  EXPECT_EQ(resolveI18nOffset(local, english, uint32_t{0}), local);
  EXPECT_STREQ(resolveI18nOffset(local, english, uint32_t{0}), "local-zero");
}

TEST(I18nOffset, FallbackZeroStartsAtEnglishBlob) {
  const char local[] = "local-zero";
  const char english[] = "english-zero";
  EXPECT_EQ(resolveI18nOffset(local, english, uint32_t{0x80000000}), english);
  EXPECT_STREQ(resolveI18nOffset(local, english, uint32_t{0x80000000}), "english-zero");
}

TEST(I18nOffset, LocalOffsetBeyondOldFallbackBitRemainsLocal) {
  std::string local(40000, '.');
  local += "local-after-32767";
  std::string english(40000, '!');
  english += "wrong-language";
  EXPECT_EQ(resolveI18nOffset(local.c_str(), english.c_str(), uint32_t{40000}), local.c_str() + 40000);
  EXPECT_STREQ(resolveI18nOffset(local.c_str(), english.c_str(), uint32_t{40000}), "local-after-32767");
}

TEST(I18nOffset, LocalOffsetBeyondUint16RemainsLocal) {
  std::string local(70000, '.');
  local += "local-after-65535";
  std::string english(70000, '!');
  english += "wrong-language";
  EXPECT_EQ(resolveI18nOffset(local.c_str(), english.c_str(), uint32_t{70000}), local.c_str() + 70000);
  EXPECT_STREQ(resolveI18nOffset(local.c_str(), english.c_str(), uint32_t{70000}), "local-after-65535");
}

TEST(I18nOffset, EnglishFallbackPreservesLargeOffset) {
  std::string local(90000, '.');
  local += "wrong-language";
  std::string english(90000, '!');
  english += "english-after-65535";
  const uint32_t offset = uint32_t{0x80000000} | uint32_t{90000};
  EXPECT_EQ(resolveI18nOffset(local.c_str(), english.c_str(), offset), english.c_str() + 90000);
  EXPECT_STREQ(resolveI18nOffset(local.c_str(), english.c_str(), offset), "english-after-65535");
}

TEST(I18nOffset, BitFifteenInsideFallbackOffsetIsNotStripped) {
  std::string english(32768, '!');
  english += "english-at-old-flag";
  EXPECT_STREQ(resolveI18nOffset("local", english.c_str(), uint32_t{0x80008000}), "english-at-old-flag");
}
