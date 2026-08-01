#include "EpubReaderClippingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <vector>

#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
std::string displayText(const std::string& text) {
  std::string result;
  result.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    const bool isSpace = c == ' ' || c == '\r' || c == '\n' || c == '\t';
    if (isSpace) {
      size_t next = i + 1;
      while (next < text.size() &&
             (text[next] == ' ' || text[next] == '\r' || text[next] == '\n' || text[next] == '\t')) {
        ++next;
      }
      const bool transferredWord =
          result.size() >= 2 && result.back() == '-' && result[result.size() - 2] != '-' && next < text.size();
      if (transferredWord) {
        result.pop_back();
      } else if (!result.empty() && result.back() != ' ') {
        result.push_back(' ');
      }
    } else {
      result.push_back(c);
    }
  }
  if (!result.empty() && result.back() == ' ') result.pop_back();
  return result;
}

}  // namespace

void EpubReaderClippingsActivity::onEnter() {
  Activity::onEnter();
  chapterTitles.clear();
  chapterTitles.reserve(store.getAll().size());
  for (const Clipping& clipping : store.getAll()) {
    std::string chapter = clipping.chapterTitle;
    const int tocIndex = epub.getTocIndexForSpineIndex(clipping.spineIndex);
    if (tocIndex >= 0) chapter = epub.getTocItem(tocIndex).title;
    chapterTitles.push_back(std::move(chapter));
  }
  requestUpdate();
}

void EpubReaderClippingsActivity::loop() {
  const int total = static_cast<int>(store.getAll().size());
  ButtonEventManager::ButtonEvent event;
  while (buttonEvents.consumeEvent(event)) {
    if (event.button == MappedInputManager::Button::Back && event.type == ButtonEventManager::PressType::Short) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
      return;
    }
    if (total > 0 && event.button == MappedInputManager::Button::Confirm &&
        event.type == ButtonEventManager::PressType::Short) {
      const Clipping& clipping = store.getAll()[selectedIndex];
      setResult(ClippingJumpResult{clipping.spineIndex, clipping.startPage, clipping.pageCount, clipping.paragraphIndex,
                                   static_cast<uint16_t>(selectedIndex)});
      finish();
      return;
    }
    // Each physical side button is exposed through two logical names
    // (Up/PageBack or Down/PageForward). Handle only Up/Down so one tap advances
    // exactly once; the PageBack/PageForward alias is intentionally discarded.
    if (total > 0 &&
        (event.button == MappedInputManager::Button::Up || event.button == MappedInputManager::Button::Down) &&
        event.type == ButtonEventManager::PressType::Short) {
      const int delta = event.button == MappedInputManager::Button::Down ? 1 : -1;
      const int nextIndex = std::clamp(selectedIndex + delta, 0, total - 1);
      if (nextIndex != selectedIndex) {
        selectedIndex = nextIndex;
        requestUpdate();
      }
      continue;
    }
    if (total > 0 && event.button == MappedInputManager::Button::Right &&
        event.type == ButtonEventManager::PressType::Short) {
      const int removedIndex = selectedIndex;
      if (store.removeAt(static_cast<size_t>(removedIndex)) && removedIndex < static_cast<int>(chapterTitles.size())) {
        chapterTitles.erase(chapterTitles.begin() + removedIndex);
      }
      const int remaining = static_cast<int>(store.getAll().size());
      if (remaining == 0) {
        ActivityResult result;
        result.isCancelled = true;
        setResult(std::move(result));
        finish();
        return;
      }
      selectedIndex = std::min(selectedIndex, remaining - 1);
      requestUpdate();
      return;
    }
  }
}

void EpubReaderClippingsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, true);
  const int total = static_cast<int>(store.getAll().size());
  GUI.drawHeader(renderer, Rect{contentRect.x, metrics.topPadding, contentRect.width, metrics.headerHeight},
                 tr(STR_CLIPPINGS));
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = contentRect.height - contentTop - metrics.verticalSpacing;
  if (total == 0) {
    renderer.drawText(UI_10_FONT_ID, contentRect.x + metrics.contentSidePadding, contentTop + 20, tr(STR_NO_CLIPPINGS));
  } else {
    constexpr int MAX_TITLE_LINES = 16;
    constexpr int MAX_TEXT_LINES = 64;
    const int markerWidth = 4;
    const int markerGap = 8;
    const int textX = contentRect.x + metrics.contentSidePadding + markerWidth + markerGap;
    const int textWidth = contentRect.width - metrics.contentSidePadding * 2 - markerWidth - markerGap;
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const int itemGap = metrics.verticalSpacing * 2;
    const int contentBottom = contentTop + contentHeight;

    // Do not retain a wrapped copy of every clipping. A 64-item collection could
    // otherwise hold thousands of strings while this screen is open. Measure
    // items only until the visible viewport is found, then wrap one item at a time.
    const auto measureClipping = [&](int index) {
      const Clipping& clipping = store.getAll()[index];
      const std::string& chapter =
          index < static_cast<int>(chapterTitles.size()) ? chapterTitles[index] : clipping.chapterTitle;
      std::vector<std::string> titleLines;
      if (!chapter.empty()) {
        titleLines =
            renderer.wrappedText(UI_10_FONT_ID, chapter.c_str(), textWidth, MAX_TITLE_LINES, EpdFontFamily::BOLD);
      }
      const std::string fullText = displayText(clipping.text);
      const auto textLines = renderer.wrappedText(UI_10_FONT_ID, fullText.c_str(), textWidth, MAX_TEXT_LINES);
      int height = static_cast<int>(titleLines.size() + textLines.size()) * lineHeight;
      if (!titleLines.empty() && !textLines.empty()) height += metrics.verticalSpacing;
      return height + itemGap + 1;
    };

    int firstVisible = selectedIndex;
    int usedHeight = measureClipping(selectedIndex);
    while (firstVisible > 0 && usedHeight + measureClipping(firstVisible - 1) <= contentHeight) {
      --firstVisible;
      usedHeight += measureClipping(firstVisible);
    }

    int textY = contentTop;
    for (int index = firstVisible; index < total && textY < contentBottom; ++index) {
      const int itemTop = textY;
      const Clipping& clipping = store.getAll()[index];
      const std::string& chapter =
          index < static_cast<int>(chapterTitles.size()) ? chapterTitles[index] : clipping.chapterTitle;
      const auto titleLines = chapter.empty() ? std::vector<std::string>()
                                              : renderer.wrappedText(UI_10_FONT_ID, chapter.c_str(), textWidth,
                                                                     MAX_TITLE_LINES, EpdFontFamily::BOLD);
      const std::string fullText = displayText(clipping.text);
      const auto textLines = renderer.wrappedText(UI_10_FONT_ID, fullText.c_str(), textWidth, MAX_TEXT_LINES);
      for (const std::string& line : titleLines) {
        if (textY + lineHeight > contentBottom) break;
        renderer.drawText(UI_10_FONT_ID, textX, textY, line.c_str(), true, EpdFontFamily::BOLD);
        textY += lineHeight;
      }
      if (!titleLines.empty() && !textLines.empty()) textY += metrics.verticalSpacing;
      for (const std::string& line : textLines) {
        if (textY + lineHeight > contentBottom) break;
        renderer.drawText(UI_10_FONT_ID, textX, textY, line.c_str());
        textY += lineHeight;
      }
      textY += itemGap;
      if (index == selectedIndex) {
        renderer.drawLine(textX - markerGap, itemTop, textX - markerGap, std::min(textY - 1, contentBottom - 1),
                          markerWidth, true);
      }
      if (textY < contentBottom) renderer.drawLine(textX, textY, textX + textWidth, textY);
      ++textY;
    }
  }
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), total > 0 ? tr(STR_OPEN) : "", "", total > 0 ? tr(STR_DELETE) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  renderer.displayBuffer();
}
