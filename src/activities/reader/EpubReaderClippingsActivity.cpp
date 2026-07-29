#include "EpubReaderClippingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
#include "components/UITheme.h"
#include "fontIds.h"

void EpubReaderClippingsActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

std::string EpubReaderClippingsActivity::itemLabel(const int index) const {
  const Clipping& clipping = store.getAll()[index];
  std::string preview;
  preview.reserve(80);
  for (const char c : clipping.text) {
    preview += (c == '\r' || c == '\n' || c == '\t') ? ' ' : c;
    if (preview.size() >= 72) {
      preview += "...";
      break;
    }
  }
  return std::to_string(index + 1) + ". " + preview;
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
  GUI.drawHeader(renderer, Rect{contentRect.x, metrics.topPadding, contentRect.width, metrics.headerHeight},
                 tr(STR_CLIPPINGS));
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = contentRect.height - contentTop - metrics.verticalSpacing;
  const int total = static_cast<int>(store.getAll().size());
  if (total == 0) {
    renderer.drawText(UI_10_FONT_ID, contentRect.x + metrics.contentSidePadding, contentTop + 20,
                      tr(STR_NO_CLIPPINGS));
  } else {
    GUI.drawList(renderer, Rect{contentRect.x, contentTop, contentRect.width, contentHeight}, total, selectedIndex,
                 [this](const int index) { return itemLabel(index); });
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), total > 0 ? tr(STR_OPEN) : "", "",
                                            total > 0 ? tr(STR_DELETE) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  renderer.displayBuffer();
}
