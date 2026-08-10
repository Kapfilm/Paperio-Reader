#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "PreviewBlockLocator.h"

namespace {

bool isBlockTag(const char* name) {
  static const char* const tags[] = {"h1", "h2", "h3", "h4", "h5", "h6", "p", "li", "div", "br", "blockquote", "pre"};
  for (const char* tag : tags) {
    if (strcmp(name, tag) == 0) return true;
  }
  return false;
}

uint32_t locate(const std::string& xml, const char* anchorId, const size_t chunkSize = 8) {
  PreviewBlockLocator locator(anchorId, isBlockTag);
  EXPECT_TRUE(locator.ok());
  for (size_t offset = 0; offset < xml.size() && !locator.done();) {
    const size_t len = std::min(chunkSize, xml.size() - offset);
    if (!locator.feed(reinterpret_cast<const uint8_t*>(xml.data() + offset), len)) break;
    offset += len;
  }
  if (!locator.done()) EXPECT_TRUE(locator.finalize());
  return locator.startOrdinal();
}

constexpr const char* kNote = R"(<?xml version='1.0' encoding='utf-8'?>
<html xmlns="http://www.w3.org/1999/xhtml"><head><title>note</title></head><body>
<p>Leading text <span id="target"></span>continued note text.</p>
</body></html>)";

TEST(PreviewBlockLocatorTest, InlineAnchorResolvesToEnclosingParagraph) {
  // html=1, head=2, title=3, body=4, p=5
  EXPECT_EQ(locate(kNote, "target"), 5u);
}

TEST(PreviewBlockLocatorTest, BlockAnchorResolvesToItself) {
  EXPECT_EQ(locate("<html><body><div><p>before</p><p id='note'>target</p></div></body></html>", "note"), 5u);
}

TEST(PreviewBlockLocatorTest, PicksInnermostEnclosingBlock) {
  EXPECT_EQ(locate("<html><body><div><blockquote><span id='a'/></blockquote></div></body></html>", "a"), 4u);
}

TEST(PreviewBlockLocatorTest, ClosedBlocksAreNotUsedAsAncestors) {
  EXPECT_EQ(locate("<html><body><div><p>done</p><span id='a'/></div></body></html>", "a"), 3u);
}

TEST(PreviewBlockLocatorTest, LegacyAnchorNameIsSupported) {
  EXPECT_EQ(locate("<html><body><p>before<a name='note'/>after</p></body></html>", "note"), 3u);
}

TEST(PreviewBlockLocatorTest, MissingAnchorReportsNotFound) { EXPECT_EQ(locate(kNote, "missing"), 0u); }

TEST(PreviewBlockLocatorTest, EmptyAnchorIsRejected) {
  PreviewBlockLocator locator("", isBlockTag);
  EXPECT_FALSE(locator.ok());
}

TEST(PreviewBlockLocatorTest, ChunkSizeDoesNotAffectResult) {
  for (const size_t chunkSize : {1u, 3u, 64u, 4096u}) {
    EXPECT_EQ(locate(kNote, "target", chunkSize), 5u) << chunkSize;
  }
}

}  // namespace
