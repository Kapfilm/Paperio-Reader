#include "EpubReaderClippingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
std::string displayText(const std::string& text) {
  std::string result;
  result.reserve(text.size());
  bool previousWasSpace = false;
  for (const char c : text) {
    const bool isSpace = c == ' ' || c == '\r' || c == '\n' || c == '\t';
    if (isSpace) {
      if (!result.empty() && !previousWasSpace) result.push_back(' ');
    } else {
      result.push_back(c);
    }
    previousWasSpace = isSpace;
  }
  if (!result.empty() && result.back() == ' ') result.pop_back();
  return result;
}
}  // namespace

void EpubReaderClippingsActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void EpubReaderClippingsActivity::loop() {
  const int total = static_cast<int>(store.getAll().size());
  ButtonEventManager::ButtonEvent event;
  while (buttonEvents.consumeEvent(event)) {
    if (event.button == MappedInputManager::Button::Back &&
        event.type == ButtonEventManager::PressType::Short) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
      return;
    }
    if (total > 0 && event.button == MappedInputManager::Button::Confirm &&
        event.type == ButtonEventManager::PressType::Short) {
      const Clipping& clipping = store.getAll()[selectedIndex];
      setResult(ClippingJumpResult{clipping.spineIndex, clipping.startPage, clipping.pageCount,
                                   clipping.paragraphIndex,
                                   static_cast<uint16_t>(selectedIndex)});
      finish();
      return;
    }
    // Each physical side button is exposed through two logical names
    // (Up/PageBack or Down/PageForward). Handle only Up/Down so one tap advances
    // exactly once; the PageBack/PageForward alias is intentionally discarded.
    if (total > 0 &&
        (event.button == MappedInputManager::Button::Up ||
         event.button == MappedInputManager::Button::Down) &&
        event.type == ButtonEventManager::PressType::Short) {
      const int delta = event.button == MappedInputManager::Button::Down ? 1 : -1;
      selectedIndex = (selectedIndex + delta + total) % total;
      requestUpdate();
      continue;
    }
    if (total > 0 && event.button == MappedInputManager::Button::Right &&
        event.type == ButtonEventManager::PressType::Short) {
      store.removeAt(static_cast<size_t>(selectedIndex));
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
  const std::string position =
      total > 0 ? std::to_string(selectedIndex + 1) + " / " + std::to_string(total) : std::string();
  GUI.drawHeader(renderer, Rect{contentRect.x, metrics.topPadding, contentRect.width, metrics.headerHeight},
                 tr(STR_CLIPPINGS), position.empty() ? nullptr : position.c_str());
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = contentRect.height - contentTop - metrics.verticalSpacing;
  if (total == 0) {
    renderer.drawText(UI_10_FONT_ID, contentRect.x + metrics.contentSidePadding, contentTop + 20,
                      tr(STR_NO_CLIPPINGS));
  } else {
    const Clipping& clipping = store.getAll()[selectedIndex];
    const int textX = contentRect.x + metrics.contentSidePadding;
    const int textWidth = contentRect.width - metrics.contentSidePadding * 2;
    int textY = contentTop;

    if (clipping.chapterTitle[0] != '\0') {
      const std::string chapter =
          renderer.truncatedText(UI_10_FONT_ID, clipping.chapterTitle, textWidth, EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, textX, textY, chapter.c_str(), true, EpdFontFamily::BOLD);
      textY += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;
    }

    const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const int maxLines = std::max(1, (contentTop + contentHeight - textY) / lineHeight);
    const std::string fullText = displayText(clipping.text);
    const auto lines = renderer.wrappedText(SMALL_FONT_ID, fullText.c_str(), textWidth, maxLines);
    for (const std::string& line : lines) {
      renderer.drawText(SMALL_FONT_ID, textX, textY, line.c_str());
      textY += lineHeight;
    }
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), total > 0 ? tr(STR_OPEN) : "", "",
                                            total > 0 ? tr(STR_DELETE) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  renderer.displayBuffer();
}
