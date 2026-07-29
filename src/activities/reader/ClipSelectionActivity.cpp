#include "ClipSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <FontCacheManager.h>
#include <Epub/Page.h>

#include <algorithm>
#include <cstdlib>

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
  if (words.empty()) {
    LOG_ERR("CLIP", "No words available for selection");
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }
  savedSectionPage = section.currentPage;
  requestUpdate();
}

void ClipSelectionActivity::onExit() {
  section.currentPage = savedSectionPage;
  Activity::onExit();
}

bool ClipSelectionActivity::renderBasePage(const int relativePage) {
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
  displayedRelativePage = relativePage;
  return true;
}

void ClipSelectionActivity::moveCursor(const int next) {
  if (next < 0 || next >= static_cast<int>(words.size()) || next == cursor) return;
  cursor = next;
  requestUpdate();
}

void ClipSelectionActivity::moveCursorByLine(const int direction) {
  if (direction == 0 || words.empty()) return;

  const int currentPage = words[cursor].pageIndex;
  const int currentY = words[cursor].y;
  const int currentCenter = words[cursor].x + words[cursor].w / 2;
  int lineWord = cursor + (direction > 0 ? 1 : -1);
  while (lineWord >= 0 && lineWord < static_cast<int>(words.size()) &&
         words[lineWord].pageIndex == currentPage && words[lineWord].y == currentY) {
    lineWord += direction > 0 ? 1 : -1;
  }
  if (lineWord < 0 || lineWord >= static_cast<int>(words.size()) || words[lineWord].pageIndex != currentPage) return;

  const int targetY = words[lineWord].y;
  int best = lineWord;
  int bestDistance = std::abs(words[lineWord].x + words[lineWord].w / 2 - currentCenter);
  for (int i = lineWord + (direction > 0 ? 1 : -1);
       i >= 0 && i < static_cast<int>(words.size()) && words[i].pageIndex == currentPage && words[i].y == targetY;
       i += direction > 0 ? 1 : -1) {
    const int distance = std::abs(words[i].x + words[i].w / 2 - currentCenter);
    if (distance < bestDistance) {
      best = i;
      bestDistance = distance;
    }
  }
  moveCursor(best);
}

void ClipSelectionActivity::confirmSelection() {
  if (selectionStart < 0) {
    selectionStart = cursor;
    requestUpdate();
    return;
  }

  const int from = std::min(selectionStart, cursor);
  const int to = std::max(selectionStart, cursor);
  auto result = ClipTextBuilder::build(words, from, to, startPage, section.pageCount);
  if (const auto paragraph = section.getParagraphIndexForPage(result.sectionPage)) {
    result.paragraphIndex = *paragraph;
  }
  setResult(std::move(result));
  finish();
}

void ClipSelectionActivity::cancelSelection() {
  if (selectionStart >= 0) {
    selectionStart = -1;
    requestUpdate();
    return;
  }

  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void ClipSelectionActivity::loop() {
  using Button = MappedInputManager::Button;
  ButtonEventManager::ButtonEvent event;
  while (buttonEvents.consumeEvent(event)) {
    if (event.type != ButtonEventManager::PressType::Short) continue;

    if (event.button == Button::Back) {
      cancelSelection();
      return;
    }
    if (event.button == Button::Confirm || event.button == Button::Power) {
      confirmSelection();
      return;
    }
    if (event.button == Button::Left) {
      moveCursor(cursor - 1);
      continue;
    }
    if (event.button == Button::Right) {
      moveCursor(cursor + 1);
      continue;
    }
    if (event.button == Button::PageBack) {
      moveCursorByLine(-1);
      continue;
    }
    if (event.button == Button::PageForward) {
      moveCursorByLine(1);
      continue;
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
  if (!renderBasePage(words[cursor].pageIndex)) return;

  if (selectionStart >= 0) {
    const int from = std::min(selectionStart, cursor);
    const int to = std::max(selectionStart, cursor);
    for (int i = from; i <= to; ++i) {
      if (words[i].pageIndex != displayedRelativePage) continue;
      WordRef paintedWord = words[i];
      if (i < to && words[i + 1].pageIndex == paintedWord.pageIndex && words[i + 1].y == paintedWord.y) {
        paintedWord.w = std::max(paintedWord.w, words[i + 1].x - paintedWord.x);
      }
      drawWord(paintedWord, false);
    }
  }
  if (words[cursor].pageIndex == displayedRelativePage) drawWord(words[cursor], true);

  const char* confirm = selectionStart < 0 ? tr(STR_SELECT) : tr(STR_DONE);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirm, tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
