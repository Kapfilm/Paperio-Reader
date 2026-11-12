#include "EpubReaderChapterSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

bool EpubReaderChapterSelectionActivity::hasChildren(const int tocIndex) const {
  return tocIndex + 1 < static_cast<int>(tocLevels.size()) && tocLevels[tocIndex + 1] > tocLevels[tocIndex];
}

void EpubReaderChapterSelectionActivity::updateVisibleRows(const int selectedTocIndex) {
  visibleRows.clear();
  selectorIndex = 0;
  int foldedLevel = -1;
  for (int i = 0; i < static_cast<int>(tocLevels.size()); ++i) {
    const int level = tocLevels[i];
    if (foldedLevel >= 0 && level > foldedLevel) continue;
    foldedLevel = !expandedRows[i] && hasChildren(i) ? level : -1;
    if (i == selectedTocIndex) selectorIndex = static_cast<int>(visibleRows.size());
    visibleRows.push_back(i);
  }
}

int EpubReaderChapterSelectionActivity::getPageItems() const {
  constexpr int lineHeight = 30;
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);
  const int startY = 60 + contentRect.y;
  const int availableHeight = contentRect.y + contentRect.height - startY - lineHeight;
  // Clamp to at least one item to avoid division by zero and empty paging.
  return std::max(1, availableHeight / lineHeight);
}

void EpubReaderChapterSelectionActivity::onEnter() {
  Activity::onEnter();

  if (!epub) {
    return;
  }

  tocLevels = epub->readTocLevels();
  expandedRows.assign(tocLevels.size(), false);
  const int tocCount = static_cast<int>(tocLevels.size());
  const int selectedTocIndex = currentTocIndex >= 0 && currentTocIndex < tocCount
                                   ? currentTocIndex
                                   : epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (selectedTocIndex >= 0 && selectedTocIndex < tocCount) {
    // Open just the ancestors of the current chapter.
    auto level = tocLevels[selectedTocIndex];
    for (int i = selectedTocIndex - 1; i >= 0 && level > 0; --i) {
      if (tocLevels[i] < level) {
        expandedRows[i] = true;
        level = tocLevels[i];
      }
    }
  }
  updateVisibleRows(selectedTocIndex);

  // Trigger first update
  requestUpdate();
}

void EpubReaderChapterSelectionActivity::onExit() { Activity::onExit(); }

void EpubReaderChapterSelectionActivity::loop() {
  const int totalItems = static_cast<int>(visibleRows.size());

  ButtonEventManager::ButtonEvent ev;
  while (buttonEvents.consumeEvent(ev)) {
    // Only Up/Down: PageBack/PageForward are aliases emitted for the same press.
    if (ev.type == ButtonEventManager::PressType::Short && totalItems > 0 &&
        (ev.button == MappedInputManager::Button::Up || ev.button == MappedInputManager::Button::Down)) {
      const int delta = ev.button == MappedInputManager::Button::Down ? 5 : -5;
      selectorIndex = std::clamp(selectorIndex + delta, 0, totalItems - 1);
      requestUpdate();
      continue;
    }
    if (ev.button == MappedInputManager::Button::Confirm && totalItems > 0) {
      const int tocIndex = visibleRows[selectorIndex];
      const bool branch = hasChildren(tocIndex);
      if (branch && ev.type == ButtonEventManager::PressType::Short) {
        expandedRows[tocIndex] = !expandedRows[tocIndex];
        updateVisibleRows(tocIndex);
        requestUpdate();
        return;
      }
      if (ev.type != (branch ? ButtonEventManager::PressType::Long : ButtonEventManager::PressType::Short)) continue;
      const int spineIndex = epub->getTocItem(tocIndex).spineIndex;
      if (spineIndex >= 0) {
        setResult(ChapterResult{spineIndex, tocIndex});
        finish();
      }
      return;
    }
    if (ev.button == MappedInputManager::Button::Back && ev.type == ButtonEventManager::PressType::Short) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
      return;
    }
  }

  buttonNavigator.onNextList({MappedInputManager::Button::Right}, selectorIndex, totalItems,
                             [this] { requestUpdate(); });
  buttonNavigator.onPreviousList({MappedInputManager::Button::Left}, selectorIndex, totalItems,
                                 [this] { requestUpdate(); });
}

void EpubReaderChapterSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const Rect contentRect = UITheme::getContentRect(renderer, true, false);
  const int pageItems = getPageItems();
  const int totalItems = static_cast<int>(visibleRows.size());

  // Manual centering to honor content gutters.
  const int titleX =
      contentRect.x +
      (contentRect.width - renderer.getTextWidth(UI_12_FONT_ID, tr(STR_SELECT_CHAPTER), EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentRect.y, tr(STR_SELECT_CHAPTER), true, EpdFontFamily::BOLD);

  const auto pageStartIndex = selectorIndex / pageItems * pageItems;
  // Highlight only the content area, not the hint gutters.
  if (totalItems > 0) {
    renderer.fillRect(contentRect.x, 60 + contentRect.y + (selectorIndex % pageItems) * 30 - 2, contentRect.width - 1,
                      30);
  }

  for (int i = 0; i < pageItems; i++) {
    int itemIndex = pageStartIndex + i;
    if (itemIndex >= totalItems) break;
    const int displayY = 60 + contentRect.y + i * 30;
    const bool isSelected = (itemIndex == selectorIndex);

    const int tocIndex = visibleRows[itemIndex];
    const auto item = epub->getTocItem(tocIndex);

    // Indent per TOC level while keeping content within the gutter-safe region.
    const int indent = 20 + std::clamp(static_cast<int>(item.level) - 1, 0, 6) * 15;
    const int textX = contentRect.x + indent;
    const std::string chapterName =
        renderer.truncatedText(UI_10_FONT_ID, item.title.c_str(), contentRect.width - indent - 20);

    if (hasChildren(tocIndex)) {
      renderer.drawText(UI_10_FONT_ID, textX - 14, displayY, expandedRows[tocIndex] ? "-" : "+", !isSelected);
    }
    renderer.drawText(UI_10_FONT_ID, textX, displayY, chapterName.c_str(), !isSelected);
  }

  const char* confirm = "";
  if (totalItems > 0) {
    const int tocIndex = visibleRows[selectorIndex];
    confirm = hasChildren(tocIndex) ? (expandedRows[tocIndex] ? tr(STR_HIDE) : tr(STR_SHOW)) : tr(STR_SELECT);
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirm, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
