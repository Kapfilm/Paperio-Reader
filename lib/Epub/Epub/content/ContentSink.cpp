#include "ContentSink.h"

#include "Epub/FootnoteEntry.h"

namespace compiled {
namespace {

// Serialized size of one word record, mirroring writeWords() in CompiledContent.cpp:
// textOff u32 + styleSpan u8 + sizePct u8 + bidiLevel u8.
constexpr size_t kWordRecordBytes = sizeof(uint32_t) + 3 * sizeof(uint8_t);

// Fixed per-TEXT-record overhead, mirroring writeContentBin(): type u8 + styleId u16 +
// flags u8 + charOffset u32 + wordCount u32 + text-length u32 + hasInline u8. Inline-image
// fields are only present on the (non-split) first record; a conservative constant is fine
// since the cap is a soft bound on read-time allocation, not an exact byte budget.
constexpr size_t kTextRecordOverhead =
    sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) +
    sizeof(uint8_t);

// Codepoints (non-continuation bytes) in a NUL-terminated word — for continuation-record
// charOffset accounting; matches stage1CountCodepoints in the producer.
uint32_t countCodepoints(const char* s) {
  uint32_t n = 0;
  for (const char* p = s; *p != '\0'; ++p) {
    if ((static_cast<uint8_t>(*p) & 0xC0) != 0x80) ++n;
  }
  return n;
}

}  // namespace

void ContentSink::beginSpine() {
  content_.spines.emplace_back();
  spineOpen_ = true;
  spineHasBlock_ = false;
}

void ContentSink::appendTextSplit(Block&& block) {
  SpineContent& spine = content_.spines.back();

  // Serialized size of the whole block's word run + text (the part the cap governs).
  size_t bodyBytes = kTextRecordOverhead + block.words.size() * kWordRecordBytes + block.text.size();
  if (block.words.size() <= 1 || bodyBytes <= kMaxSerializedBody) {
    spine.blocks.push_back(std::move(block));
    return;
  }

  // Split the word list into runs that each serialize within the cap. Every word carries
  // its own text (NUL-terminated back-to-back), so a run's text is a contiguous slice of
  // block.text from the first word's textOff to the last word's end. textOff is rebased to
  // the run's local text; the first record keeps the original flags + inline image, the
  // rest are marked kContinuation with no inline image.
  const std::string& srcText = block.text;
  const std::vector<Word>& srcWords = block.words;

  size_t wordStart = 0;
  uint32_t runCharOffset = block.charOffset;  // book-absolute; advances per emitted run
  bool first = true;
  while (wordStart < srcWords.size()) {
    size_t used = kTextRecordOverhead;
    size_t wordEnd = wordStart;
    while (wordEnd < srcWords.size()) {
      // Byte length of this word's text (up to and including its NUL).
      const size_t textOff = srcWords[wordEnd].textOff;
      const size_t textEnd = srcText.find('\0', textOff);
      const size_t wordBytes = (textEnd == std::string::npos ? srcText.size() : textEnd + 1) - textOff;
      const size_t add = kWordRecordBytes + wordBytes;
      // Always take at least one word per run so a single oversized word still progresses.
      if (wordEnd != wordStart && used + add > kMaxSerializedBody) break;
      used += add;
      ++wordEnd;
    }

    Block rec;
    rec.type = BlockType::Text;
    rec.styleId = block.styleId;
    rec.charOffset = runCharOffset;
    rec.flags = first ? block.flags : static_cast<uint8_t>(block.flags | kContinuation);
    if (first) {
      rec.inlineImageEntryPath = block.inlineImageEntryPath;
      rec.inlineImageWidth = block.inlineImageWidth;
      rec.inlineImageHeight = block.inlineImageHeight;
      rec.inlineImageSide = block.inlineImageSide;
      rec.inlineImageAlt = block.inlineImageAlt;
    }

    uint32_t runCodepoints = 0;
    for (size_t i = wordStart; i < wordEnd; ++i) {
      const char* wordPtr = &srcText[srcWords[i].textOff];
      Word w = srcWords[i];
      w.textOff = static_cast<uint32_t>(rec.text.size());
      rec.words.push_back(w);
      rec.text.append(wordPtr);   // up to (not incl.) NUL
      rec.text.push_back('\0');
      runCodepoints += countCodepoints(wordPtr);
    }

    spine.blocks.push_back(std::move(rec));
    runCharOffset += runCodepoints;
    wordStart = wordEnd;
    first = false;
  }
}

void ContentSink::onBlock(Block&& block, const CssStyle& style) {
  if (!spineOpen_) beginSpine();
  SpineContent& spine = content_.spines.back();

  // Intern the block's resolved style (image/table blocks pass CssStyle{} — an empty style
  // still interns to a valid pool id, shared across all such blocks).
  block.styleId = internStyle(content_, style);

  if (!spineHasBlock_) {
    spine.firstCharOffset = block.charOffset;
    spineHasBlock_ = true;
  }

  if (block.type == BlockType::Text) {
    appendTextSplit(std::move(block));
  } else {
    spine.blocks.push_back(std::move(block));
  }
}

void ContentSink::onAnchor(const std::string& id) {
  if (!spineOpen_) beginSpine();
  SpineContent& spine = content_.spines.back();
  // Block granularity: the anchor introduces the block at the current block count
  // (charOffsetInBlock 0), matching the producer's stage1EmitPendingAnchor model.
  spine.anchors.push_back(Anchor{id, static_cast<uint32_t>(spine.blocks.size()), 0});
}

void ContentSink::onChapter(uint8_t level, const std::string& title) {
  if (!spineOpen_) beginSpine();
  const SpineContent& spine = content_.spines.back();
  // The heading block was emitted immediately before this call (stage1FlushBlock order):
  // point at the last block. An empty run would be a producer bug, but guard anyway.
  const uint32_t blockIndex = spine.blocks.empty() ? 0 : static_cast<uint32_t>(spine.blocks.size() - 1);
  content_.chapters.push_back(
      Chapter{static_cast<uint16_t>(content_.spines.size() - 1), blockIndex, level, title});
}

void ContentSink::onPageBreakLabel(const std::string& label) {
  if (!spineOpen_) beginSpine();
  const SpineContent& spine = content_.spines.back();
  labels_.push_back(PageLabel{label, static_cast<uint16_t>(content_.spines.size() - 1),
                              static_cast<uint32_t>(spine.blocks.size())});
}

void ContentSink::onFootnote(int /*wordIndex*/, const FootnoteEntry& /*entry*/) {
  // Deferred: footnotes get a dedicated content.bin scan in master-plan Phase 3 step 4.
}

void ContentSink::onSpineEnd() { spineOpen_ = false; }

}  // namespace compiled
