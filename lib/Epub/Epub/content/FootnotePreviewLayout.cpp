#include "FootnotePreviewLayout.h"

#include <GfxRenderer.h>

#include <string>
#include <vector>

#include "CompiledContent.h"

namespace compiled {

void abbreviateFootnotePreviews(Block& block, GfxRenderer& renderer, const int fontId, const uint16_t viewportWidth) {
  if (block.footnotePreviews.empty()) return;

  const int maxAdvance = static_cast<int>(viewportWidth) * 2;
  const int spaceAdvance = renderer.getSpaceWidth(fontId);

  std::vector<Word> newWords;
  std::string newText;
  newWords.reserve(block.words.size());

  // Helper to copy a word (its text + metadata) into the rebuilt vectors, re-basing textOff.
  auto pushWord = [&](const Word& w, const std::string& text) {
    Word nw = w;
    nw.textOff = static_cast<uint32_t>(newText.size());
    newWords.push_back(nw);
    newText.append(text);
    newText.push_back('\0');
  };

  size_t wi = 0;
  size_t runIdx = 0;
  while (wi < block.words.size()) {
    const bool inRun = runIdx < block.footnotePreviews.size() && wi == block.footnotePreviews[runIdx].startWord;
    if (!inRun) {
      pushWord(block.words[wi], std::string(&block.text[block.words[wi].textOff]));
      ++wi;
      continue;
    }

    // A preview run is stored as the tokenization of " (" + note + ")": the '(' is joined onto the
    // first note word and ')' onto the last. Reconstruct the bare note words to measure, then
    // re-emit " (" kept... "…)" (or "...)" without ellipsis if all fit).
    const PreviewRun& run = block.footnotePreviews[runIdx];
    const size_t first = run.startWord;
    const size_t last = run.startWord + run.count - 1;

    int usedAdvance = 0;
    size_t keptCount = 0;  // number of run words kept (before any ellipsis)
    for (size_t k = 0; k < run.count; ++k) {
      std::string tok(&block.text[block.words[first + k].textOff]);
      // Strip the joined parens to recover the bare note word for measurement.
      if (k == 0 && !tok.empty() && tok.front() == '(') tok.erase(tok.begin());
      if (k == run.count - 1 && !tok.empty() && tok.back() == ')') tok.pop_back();
      const int wordAdvance = renderer.getTextWidth(fontId, tok.c_str());
      const int separatorAdvance = keptCount == 0 ? 0 : spaceAdvance;
      if (keptCount > 0 && usedAdvance + separatorAdvance + wordAdvance > maxAdvance) break;
      usedAdvance += separatorAdvance + wordAdvance;
      ++keptCount;
    }
    if (keptCount == 0) keptCount = 1;  // always keep at least the first word

    // Re-emit the kept run words, re-joining '(' on the first and the closing ')'/'...)' on the last
    // kept word, so the emitted tokens are again " (" + abbrev + ")".
    for (size_t k = 0; k < keptCount; ++k) {
      std::string tok(&block.text[block.words[first + k].textOff]);
      if (k == keptCount - 1 && last != first + k) {
        // Truncated: the original last word (which carried the closing ')') was dropped. Rebuild
        // the closer on this last kept word: "word" + "..." + ")".
        if (!tok.empty() && tok.back() == ')') tok.pop_back();  // defensive
        tok += "...)";
      }
      pushWord(block.words[first + k], tok);
    }

    wi = last + 1;
    ++runIdx;
  }

  block.words = std::move(newWords);
  block.text = std::move(newText);
  block.footnotePreviews.clear();
}

}  // namespace compiled
