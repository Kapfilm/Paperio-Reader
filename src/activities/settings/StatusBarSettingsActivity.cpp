#include "StatusBarSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
const StrId progressBarNames[] = {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE};
const StrId progressBarThicknessNames[] = {StrId::STR_PROGRESS_BAR_THIN, StrId::STR_PROGRESS_BAR_MEDIUM,
                                           StrId::STR_PROGRESS_BAR_THICK};
const StrId itemPositionNames[] = {StrId::STR_TOP, StrId::STR_BOTTOM, StrId::STR_HIDE};
const StrId fontSizeNames[] = {StrId::STR_NORMAL, StrId::STR_LARGE};

// One menu row. Location rows combine their visibility and position settings into
// Top / Bottom / Hide. Other rows cycle their ordinary enum values.
//
// The whole menu is this single table. Adding, removing, or reordering a row is a one-line edit here —
// there is no parallel index bookkeeping to keep in sync. Rows with `requiresClock` are skipped when
// the clock feature is off, so the visible list compacts without any index remapping.
struct StatusBarItem {
  StrId label;
  uint8_t CrossPointSettings::* field;
  uint8_t valueCount;
  uint8_t defaultValue;     // value to reset to if the stored one is out of range
  const StrId* valueNames;  // nullptr → boolean Show/Hide toggle
  bool requiresClock;
  uint8_t CrossPointSettings::* positionField = nullptr;
};

template <size_t N>
constexpr StatusBarItem enumItem(StrId label, uint8_t CrossPointSettings::* field, const StrId (&names)[N],
                                 uint8_t defaultValue) {
  return {label, field, static_cast<uint8_t>(N), defaultValue, names, false};
}
bool isCenteredItem(const StatusBarItem& item) {
  return item.field == &CrossPointSettings::statusBarTitle || item.field == &CrossPointSettings::statusBarBookTitle;
}

constexpr StatusBarItem locationItem(StrId label, uint8_t CrossPointSettings::* field,
                                      uint8_t CrossPointSettings::* positionField, bool requiresClock = false) {
  const bool centered = field == &CrossPointSettings::statusBarTitle || field == &CrossPointSettings::statusBarBookTitle;
  return {label, field, static_cast<uint8_t>(centered ? 3 : 5), 0, centered ? itemPositionNames : nullptr,
          requiresClock, positionField};
}

uint8_t currentItemValue(const StatusBarItem& item) {
  if (!item.positionField) return SETTINGS.*item.field;
  const bool title = item.field == &CrossPointSettings::statusBarTitle;
  const bool hidden = title ? SETTINGS.*item.field == CrossPointSettings::HIDE_TITLE : SETTINGS.*item.field == 0;
  const bool centered = isCenteredItem(item);
  if (hidden) return centered ? 2 : 4;
  const uint8_t position = SETTINGS.*item.positionField;
  const bool top = position == 1 || position == 3 || position == 4 ||
      (position == 0 && SETTINGS.statusBarItemsPosition == CrossPointSettings::STATUS_BAR_ITEMS_TOP);
  if (centered) return top ? 0 : 1;
  const bool legacyLeft = item.field == &CrossPointSettings::statusBarBattery ||
                          item.field == &CrossPointSettings::statusBarClock;
  const bool left = position == 3 || position == 5 || (position < 3 && legacyLeft);
  return (top ? 0 : 2) + (left ? 0 : 1);
}

std::string currentItemLabel(const StatusBarItem& item) {
  const uint8_t value = currentItemValue(item);
  if (!item.positionField || isCenteredItem(item)) return I18N.get(item.valueNames[value]);
  if (value == 4) return tr(STR_HIDE);
  const char* edge = value < 2 ? tr(STR_TOP) : tr(STR_BOTTOM);
  return value % 2 == 0 ? std::string("<") + edge : std::string(edge) + ">";
}

const StatusBarItem statusBarItems[] = {
    enumItem(StrId::STR_STATUS_FONT_SIZE, &CrossPointSettings::statusBarFontSize, fontSizeNames, 0),
    locationItem(StrId::STR_CHAPTER_PAGE_COUNT, &CrossPointSettings::statusBarChapterPageCount,
                 &CrossPointSettings::statusBarChapterPageCountPosition),
    locationItem(StrId::STR_BOOK_PROGRESS_PERCENTAGE, &CrossPointSettings::statusBarBookProgressPercentage,
                 &CrossPointSettings::statusBarBookProgressPercentagePosition),
    locationItem(StrId::STR_EXAMPLE_BOOK, &CrossPointSettings::statusBarBookTitle,
                 &CrossPointSettings::statusBarBookTitlePosition),
    locationItem(StrId::STR_CHAPTER, &CrossPointSettings::statusBarTitle,
                 &CrossPointSettings::statusBarTitlePosition),
    locationItem(StrId::STR_BATTERY, &CrossPointSettings::statusBarBattery,
                 &CrossPointSettings::statusBarBatteryPosition),
    locationItem(StrId::STR_CLOCK, &CrossPointSettings::statusBarClock,
                 &CrossPointSettings::statusBarClockPosition, true),
    enumItem(StrId::STR_UPPER_PROGRESS_BAR, &CrossPointSettings::statusBarUpperProgressBar, progressBarNames,
             CrossPointSettings::STATUS_BAR_PROGRESS_BAR::HIDE_PROGRESS),
    enumItem(StrId::STR_UPPER_PROGRESS_BAR_THICKNESS, &CrossPointSettings::statusBarUpperProgressBarThickness,
             progressBarThicknessNames, CrossPointSettings::STATUS_BAR_PROGRESS_BAR_THICKNESS::PROGRESS_BAR_NORMAL),
    enumItem(StrId::STR_LOWER_PROGRESS_BAR, &CrossPointSettings::statusBarLowerProgressBar, progressBarNames,
             CrossPointSettings::STATUS_BAR_PROGRESS_BAR::HIDE_PROGRESS),
    enumItem(StrId::STR_LOWER_PROGRESS_BAR_THICKNESS, &CrossPointSettings::statusBarLowerProgressBarThickness,
             progressBarThicknessNames, CrossPointSettings::STATUS_BAR_PROGRESS_BAR_THICKNESS::PROGRESS_BAR_NORMAL),
};

// Map a visible row index (clock rows omitted when the clock is off) to its entry in statusBarItems.
const StatusBarItem& visibleItem(int visibleIndex) {
  int seen = 0;
  for (const auto& item : statusBarItems) {
    if (item.requiresClock && !SETTINGS.useClock) {
      continue;
    }
    if (seen == visibleIndex) {
      return item;
    }
    ++seen;
  }
  return statusBarItems[0];  // out-of-range guard; callers clamp the index first
}

int visibleItemCount() {
  return static_cast<int>(
      std::count_if(std::begin(statusBarItems), std::end(statusBarItems),
                    [](const StatusBarItem& item) { return !item.requiresClock || SETTINGS.useClock; }));
}

// Retained for the progress-bar preview drawing below, which references specific enum cardinalities.
constexpr int PROGRESS_BAR_ITEMS = 3;

constexpr int previewHorizontalInset = 10;
constexpr int previewHeight = 136;
constexpr int previewInnerMargin = 4;

void drawPreviewProgressBar(const GfxRenderer& renderer, const Rect& rect, const uint8_t progressBar,
                            const uint8_t thickness, const bool topEdge) {
  if (progressBar == CrossPointSettings::STATUS_BAR_PROGRESS_BAR::HIDE_PROGRESS) {
    return;
  }

  const int percent = progressBar == CrossPointSettings::STATUS_BAR_PROGRESS_BAR::BOOK_PROGRESS ? 75 : 25;
  const int barHeight = UITheme::getProgressBarHeight(progressBar, thickness);
  const int y = topEdge ? rect.y + previewInnerMargin : rect.y + rect.height - previewInnerMargin - barHeight;
  const int barWidth = (rect.width - previewInnerMargin * 2) * percent / 100;
  renderer.fillRect(rect.x + previewInnerMargin, y, barWidth, barHeight);
}

void drawPreviewStatusItems(const GfxRenderer& renderer, const Rect& rect, const ThemeMetrics&) {
  const int font = UITheme::getStatusBarFontId();
  const int lineHeight = renderer.getLineHeight(font);
  for (const bool top : {true, false}) {
    const int barHeight = top ? UITheme::getProgressBarHeight(SETTINGS.statusBarUpperProgressBar,
                                                            SETTINGS.statusBarUpperProgressBarThickness)
                             : UITheme::getProgressBarHeight(SETTINGS.statusBarLowerProgressBar,
                                                            SETTINGS.statusBarLowerProgressBarThickness);
    const int y = top ? rect.y + previewInnerMargin + barHeight + 1
                      : rect.y + rect.height - previewInnerMargin - barHeight -
                            UITheme::getStatusBarItemsHeightForEdge(false) + 1;
    struct Sample { bool visible; uint8_t position; bool defaultLeft; const char* text; };
    const Sample samples[] = {
        {SETTINGS.statusBarBattery != 0, SETTINGS.statusBarBatteryPosition, true, "75%"},
        {SETTINGS.useClock && SETTINGS.statusBarClock, SETTINGS.statusBarClockPosition, true, "12:34"},
        {SETTINGS.statusBarChapterPageCount != 0, SETTINGS.statusBarChapterPageCountPosition, false, "8/32"},
        {SETTINGS.statusBarBookProgressPercentage != 0, SETTINGS.statusBarBookProgressPercentagePosition, false, "75%"},
    };
    int left = rect.x + previewInnerMargin + 6;
    int right = rect.x + rect.width - previewInnerMargin - 6;
    for (const auto& item : samples) {
      if (!item.visible || UITheme::statusBarItemAtTop(item.position) != top) continue;
      const int width = renderer.getTextWidth(font, item.text);
      if (left + width > right) continue;
      if (UITheme::statusBarItemAtLeft(item.position, item.defaultLeft)) {
        renderer.drawText(font, left, y, item.text);
        left += width + 8;
      } else {
        right -= width;
        renderer.drawText(font, right, y, item.text);
        right -= 8;
      }
    }
    const bool book = SETTINGS.statusBarBookTitle && UITheme::statusBarItemAtTop(SETTINGS.statusBarBookTitlePosition) == top;
    const bool chapter = SETTINGS.statusBarTitle != CrossPointSettings::HIDE_TITLE &&
                         UITheme::statusBarItemAtTop(SETTINGS.statusBarTitlePosition) == top;
    auto label = [&](const char* text, int textY, int x1, int x2) {
      if (x2 <= x1) return;
      const auto fitted = renderer.truncatedText(font, text, x2 - x1);
      const int width = renderer.getTextWidth(font, fitted.c_str());
      if (width <= x2 - x1) renderer.drawText(font, std::clamp(rect.x + (rect.width - width) / 2, x1, x2 - width),
                                            textY, fitted.c_str());
    };
    if (book) label(tr(STR_EXAMPLE_BOOK), y, left, right);
    if (chapter) label(tr(STR_EXAMPLE_CHAPTER), y + (book ? lineHeight : 0),
                       book ? rect.x + 10 : left, book ? rect.x + rect.width - 10 : right);
  }
}

}  // namespace

void StatusBarSettingsActivity::onEnter() {
  Activity::onEnter();

  if (selectedIndex >= visibleItemCount()) {
    selectedIndex = 0;
  }

  // Clamp status bar settings in case of corrupt/migrated data: every field must hold a valid value
  // index (0..valueCount-1). A stray value would index past its valueNames array when rendered.
  for (const auto& item : statusBarItems) {
    const uint8_t storedValueCount = item.positionField && item.field != &CrossPointSettings::statusBarTitle
                                         ? 2 : item.valueCount;
    if (SETTINGS.*item.field >= storedValueCount) {
      SETTINGS.*item.field = item.defaultValue;
    }
  }

  requestUpdate();
}

void StatusBarSettingsActivity::onExit() { Activity::onExit(); }

void StatusBarSettingsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    requestUpdate();
    return;
  }

  // Handle navigation
  const int menuCount = visibleItemCount();
  buttonNavigator.onNextList(selectedIndex, menuCount, [this] { requestUpdate(); });
  buttonNavigator.onPreviousList(selectedIndex, menuCount, [this] { requestUpdate(); });
}

void StatusBarSettingsActivity::handleSelection() {
  const StatusBarItem& item = visibleItem(selectedIndex);
  const uint8_t value = (currentItemValue(item) + 1) % item.valueCount;
  if (item.positionField) {
    const bool title = item.field == &CrossPointSettings::statusBarTitle;
    const bool centered = isCenteredItem(item);
    if (value == (centered ? 2 : 4)) {
      SETTINGS.*item.field = title ? static_cast<uint8_t>(CrossPointSettings::HIDE_TITLE) : 0;
    } else {
      SETTINGS.*item.positionField = centered ? value + 1 : value + 3;
      SETTINGS.*item.field = title ? static_cast<uint8_t>(CrossPointSettings::CHAPTER_TITLE) : 1;
    }
  } else {
    SETTINGS.*item.field = value;
  }
  SETTINGS.saveToFile();
}

void StatusBarSettingsActivity::render(RenderLock&&) {
  const int panelTop = overlayPanel ? renderer.getScreenHeight() / 4 : 0;
  if (overlayPanel) {
    renderer.syncWriteBufferFromDisplayed();
    renderer.fillRect(0, panelTop, renderer.getScreenWidth(), renderer.getScreenHeight() - panelTop, false);
  } else {
    renderer.clearScreen();
  }

  auto metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);
  const int pageWidth = (int)renderer.getScreenWidth();
  const int pageHeight = (int)renderer.getScreenHeight();

  const int panelHeaderHeight = renderer.getLineHeight(UI_12_FONT_ID) + 30;
  if (overlayPanel) {
    renderer.fillRoundedRect(0, panelTop, pageWidth, pageHeight - panelTop, 12,
                             true, true, false, false, Color::White);
    renderer.fillRoundedRect(0, panelTop, pageWidth, panelHeaderHeight, 12,
                             true, true, false, false, Color::LightGray);
    renderer.drawRoundedRect(0, panelTop, pageWidth, pageHeight - panelTop, 2, 12,
                             true, true, false, false, true);
    const auto heading = renderer.truncatedText(UI_12_FONT_ID, tr(STR_CUSTOMISE_STATUS_BAR),
                                                pageWidth - 40, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_12_FONT_ID, panelTop + 15, heading.c_str(), true, EpdFontFamily::BOLD);
    renderer.drawLine(0, panelTop + panelHeaderHeight, pageWidth - 1, panelTop + panelHeaderHeight, 1, true);
  } else {
    GUI.drawHeader(renderer, Rect{contentRect.x, metrics.topPadding, contentRect.width, metrics.headerHeight},
                   tr(STR_CUSTOMISE_STATUS_BAR));
  }

  const int contentTop = overlayPanel ? panelTop + panelHeaderHeight + metrics.verticalSpacing
                                      : metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int previewLabelHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const bool showPreview = !overlayPanel || pageHeight >= 700;
  const int previewAreaHeight = showPreview ? previewLabelHeight + previewHeight + metrics.verticalSpacing * 2 : 0;
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - previewAreaHeight - metrics.verticalSpacing * 2;
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, visibleItemCount(), static_cast<int>(selectedIndex),
      [](int index) {
        const auto& item = visibleItem(index);
        std::string label = I18N.get(item.label);
        return label;
      }, nullptr, nullptr,
      [](int index) { return currentItemLabel(visibleItem(index)); },
      true);

  // Draw button hints
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (showPreview) {
  const int previewLabelY = contentTop + contentHeight + metrics.verticalSpacing;
  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, previewLabelY, tr(STR_PREVIEW));
  const Rect previewRect{previewHorizontalInset, previewLabelY + previewLabelHeight + metrics.verticalSpacing / 2,
                         pageWidth - previewHorizontalInset * 2, previewHeight};
  renderer.drawRect(previewRect.x, previewRect.y, previewRect.width, previewRect.height);
  drawPreviewProgressBar(renderer, previewRect, SETTINGS.statusBarUpperProgressBar,
                         SETTINGS.statusBarUpperProgressBarThickness, true);
  drawPreviewProgressBar(renderer, previewRect, SETTINGS.statusBarLowerProgressBar,
                         SETTINGS.statusBarLowerProgressBarThickness, false);
  drawPreviewStatusItems(renderer, previewRect, metrics);

  }

  if (overlayPanel) renderer.displayWindow(0, panelTop, pageWidth, pageHeight - panelTop);
  else renderer.displayBuffer();
}
