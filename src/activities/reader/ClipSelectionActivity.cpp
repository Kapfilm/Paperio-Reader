#include "ClipSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <FontCacheManager.h>
#include <Epub/Page.h>

#include <algorithm>
#include <cstring>

#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
#include "clippings/ClipTextBuilder.h"
#include "components/UITheme.h"

ClipSelectionActivity::ClipSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             std::vector<WordRef> words, const int fontId, Section& section,
                                             const int startPage, const int marginTop, const int marginLeft)
    : Activity("ClipSelection", renderer, mappedInput),
      words(std::move(words)),
      fontId(fontId),
      section(section),
      startPage(startPage),
      marginTop(marginTop),
      marginLeft(marginLeft) {}

void ClipSelectionActivity::onEnter() {
  Activity::onEnter();
  if (words.empty() || !allocateSnapshot()) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }
  savedSectionPage = section.currentPage;
  if (!showPage(0)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }
  requestUpdate();
}

void ClipSelectionActivity::onExit() {
  section.currentPage = savedSectionPage;
  snapshotChunks.clear();
  Activity::onExit();
}

bool ClipSelectionActivity::allocateSnapshot() {
  snapshotSize = renderer.getBufferSize();
  const size_t chunkCount = (snapshotSize + BUFFER_CHUNK_SIZE - 1) / BUFFER_CHUNK_SIZE;
  snapshotChunks.reserve(chunkCount);
  for (size_t i = 0; i < chunkCount; ++i) {
    const size_t bytes = std::min(BUFFER_CHUNK_SIZE, snapshotSize - i * BUFFER_CHUNK_SIZE);
    auto chunk = makeUniqueNoThrow<uint8_t[]>(bytes);
    if (!chunk) {
      LOG_ERR("CLIP", "OOM allocating selection snapshot chunk (%u bytes)", static_cast<unsigned>(bytes));
      snapshotChunks.clear();
      return false;
    }
    snapshotChunks.push_back(std::move(chunk));
  }
  return true;
}

void ClipSelectionActivity::saveSnapshot() {
  const uint8_t* source = renderer.getFrameBuffer();
  for (size_t i = 0; i < snapshotChunks.size(); ++i) {
    const size_t offset = i * BUFFER_CHUNK_SIZE;
    const size_t bytes = std::min(BUFFER_CHUNK_SIZE, snapshotSize - offset);
    memcpy(snapshotChunks[i].get(), source + offset, bytes);
  }
  hasSnapshot = true;
}

void ClipSelectionActivity::restoreSnapshot() const {
  uint8_t* target = renderer.getFrameBuffer();
  for (size_t i = 0; i < snapshotChunks.size(); ++i) {
    const size_t offset = i * BUFFER_CHUNK_SIZE;
    const size_t bytes = std::min(BUFFER_CHUNK_SIZE, snapshotSize - offset);
    memcpy(target + offset, snapshotChunks[i].get(), bytes);
  }
}

bool ClipSelectionActivity::showPage(const int relativePage) {
  section.currentPage = startPage + relativePage;
  auto page = section.loadPageFromSectionFile();
  if (!page) {
    LOG_ERR("CLIP", "Unable to load selection page %d", relativePage);
    return false;
  }

  renderer.clearScreen();
  if (auto* cache = renderer.getFontCacheManager()) {
    auto scope = cache->createPrewarmScope();
    page->renderTextOnly(renderer, fontId, marginLeft, marginTop);
    scope.endScanAndPrewarm();
    renderer.clearScreen();
  }
  page->render(renderer, fontId, marginLeft, marginTop, false, true);
  saveSnapshot();
  displayedRelativePage = relativePage;
  return true;
}

void ClipSelectionActivity::moveCursor(const int next) {
  if (next < 0 || next >= static_cast<int>(words.size()) || next == cursor) return;
  const int oldPage = words[cursor].pageIndex;
  cursor = next;
  pageSwitchPending = words[cursor].pageIndex != oldPage;
  requestUpdate();
}

void ClipSelectionActivity::loop() {
  using Button = MappedInputManager::Button;
  buttonNavigator.onRelease({Button::Left}, [this] { moveCursor(cursor - 1); });
  buttonNavigator.onContinuous({Button::Left}, [this] { moveCursor(cursor - 1); });
  buttonNavigator.onRelease({Button::Right}, [this] { moveCursor(cursor + 1); });
  buttonNavigator.onContinuous({Button::Right}, [this] { moveCursor(cursor + 1); });

  if (mappedInput.wasReleased(Button::Confirm)) {
    if (selectionStart < 0) {
      selectionStart = cursor;
      requestUpdate();
    } else {
      const int from = std::min(selectionStart, cursor);
      const int to = std::max(selectionStart, cursor);
      auto result = ClipTextBuilder::build(words, from, to, startPage, section.pageCount);
      if (const auto paragraph = section.getParagraphIndexForPage(result.sectionPage)) {
        result.paragraphIndex = *paragraph;
      }
      setResult(std::move(result));
      finish();
    }
    return;
  }

  if (mappedInput.wasReleased(Button::Back)) {
    if (selectionStart >= 0) {
      selectionStart = -1;
      requestUpdate();
    } else {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
    }
  }
}

void ClipSelectionActivity::drawWord(const WordRef& word, const bool cursorStyle) const {
  const int drawFontId = word.effectiveFontId > 0 ? word.effectiveFontId : fontId;
  if (cursorStyle) {
    renderer.fillRect(word.x, word.y, word.w, word.h, true);
    if (word.scale == 1.0f) {
      renderer.drawText(drawFontId, word.x, word.y, word.text.c_str(), false, word.style);
    } else {
      renderer.drawTextScaled(drawFontId, word.x, word.y, word.text.c_str(), false, word.style, word.scale);
    }
  } else {
    renderer.fillRectDither(word.x, word.y, word.w, word.h, Color::LightGray);
    if (word.scale == 1.0f) {
      renderer.drawText(drawFontId, word.x, word.y, word.text.c_str(), true, word.style);
    } else {
      renderer.drawTextScaled(drawFontId, word.x, word.y, word.text.c_str(), true, word.style, word.scale);
    }
  }
}

void ClipSelectionActivity::render(RenderLock&&) {
  if (!hasSnapshot) return;
  if (pageSwitchPending) {
    if (!showPage(words[cursor].pageIndex)) return;
    pageSwitchPending = false;
  } else {
    restoreSnapshot();
  }

  if (selectionStart >= 0) {
    const int from = std::min(selectionStart, cursor);
    const int to = std::max(selectionStart, cursor);
    for (int i = from; i <= to; ++i) {
      if (words[i].pageIndex == displayedRelativePage) drawWord(words[i], false);
    }
  }
  if (words[cursor].pageIndex == displayedRelativePage) drawWord(words[cursor], true);

  const char* confirm = selectionStart < 0 ? tr(STR_SELECT) : tr(STR_DONE);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirm, tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
