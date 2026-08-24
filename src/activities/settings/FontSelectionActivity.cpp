#include "FontSelectionActivity.h"

#include <FontCacheManager.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SdCardFontGlobals.h"
#include "activities/RenderLock.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int PREVIEW_HEIGHT_PERCENT = 30;
constexpr int PREVIEW_PADDING = 12;
constexpr int PREVIEW_LABEL_GAP = 4;
constexpr const char* ELLIPSIS_UTF8 = "\xe2\x80\xa6";
}  // namespace

void FontSelectionActivity::onEnter() {
  Activity::onEnter();
  fontCount = fontFamilyOptionCount();
  selectedIndex =
      static_cast<int>(target == Target::TXT ? txtFontFamilyDynamicGetter(nullptr) : fontFamilyDynamicGetter(nullptr));
  if (selectedIndex >= fontCount) selectedIndex = 0;
  previewIndex = selectedIndex;
  loadPreview(previewIndex);
  requestUpdate();
}

void FontSelectionActivity::onExit() { Activity::onExit(); }

void FontSelectionActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    RenderLock lock;
    restoreActiveFont();
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    // The first press previews the highlighted font; pressing Confirm again
    // applies the font and returns to settings. The active settings fields are
    // never mutated by previewing, so Back is always a true cancel.
    if (selectedIndex == previewIndex) {
      handleSelection();
    } else {
      RenderLock lock;
      GUI.drawPopup(renderer, tr(STR_LOADING_FONT));
      loadPreview(selectedIndex);
      requestUpdate();
    }
    return;
  }

  buttonNavigator.onNextList(selectedIndex, fontCount, [this] { requestUpdate(); });
  buttonNavigator.onPreviousList(selectedIndex, fontCount, [this] { requestUpdate(); });
}

void FontSelectionActivity::handleSelection() {
  if (target == Target::TXT) {
    txtFontFamilyDynamicSetter(nullptr, static_cast<uint8_t>(selectedIndex));
  } else {
    fontFamilyDynamicSetter(nullptr, static_cast<uint8_t>(selectedIndex));
  }
  finish();
}

uint8_t FontSelectionActivity::targetFontSize() const {
  return target == Target::TXT ? SETTINGS.txtFontSize : SETTINGS.fontSize;
}

void FontSelectionActivity::loadPreview(const int index) {
  if (index < 0 || index >= fontCount) return;

  previewIndex = index;
  const uint8_t size = targetFontSize();
  if (index < CrossPointSettings::BUILTIN_FONT_COUNT) {
    // Only one SD family can be resident. Unload a previous SD preview before
    // drawing the built-in family; this also returns its heap immediately.
    sdFontSystem.ensureLoaded(renderer, "", size);
    previewFontId = CrossPointSettings::getBuiltinReaderFontId(static_cast<uint8_t>(index), size);
    return;
  }

  const auto& families = sdFontSystem.registry().getFamilies();
  const int sdIndex = index - CrossPointSettings::BUILTIN_FONT_COUNT;
  if (sdIndex < 0 || sdIndex >= static_cast<int>(families.size())) {
    previewFontId = CrossPointSettings::getBuiltinReaderFontId(CrossPointSettings::NOTOSANS, size);
    return;
  }

  const char* familyName = families[sdIndex].name.c_str();
  sdFontSystem.ensureLoadedForPreview(renderer, familyName, size);
  previewFontId = resolveSdCardFontId(familyName, size);
  if (previewFontId == 0) {
    previewFontId = CrossPointSettings::getBuiltinReaderFontId(CrossPointSettings::NOTOSANS, size);
  }
}

void FontSelectionActivity::restoreActiveFont() {
  const char* familyName = target == Target::TXT ? SETTINGS.txtSdFontFamilyName : SETTINGS.sdFontFamilyName;
  sdFontSystem.ensureLoadedForPreview(renderer, familyName, targetFontSize());
}

void FontSelectionActivity::renderPreviewPane(const int top, const int height, const int fontId,
                                              const char* fontName) const {
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);
  const int left = contentRect.x + PREVIEW_PADDING;
  const int width = contentRect.width - PREVIEW_PADDING * 2;
  if (width <= 0 || height <= 0) return;

  const int labelHeight = renderer.getTextHeight(UI_10_FONT_ID);
  const int labelReserved = labelHeight + PREVIEW_LABEL_GAP + PREVIEW_PADDING;
  char label[128];
  snprintf(label, sizeof(label), "%s \"%s\"", tr(STR_PREVIEW), fontName ? fontName : "");
  renderer.drawText(UI_10_FONT_ID, left, top + height - PREVIEW_PADDING - labelHeight, label);

  if (fontId == 0) return;
  const int lineHeight = renderer.getTextHeight(fontId);
  if (lineHeight <= 0) return;

  const int textHeight = height - PREVIEW_PADDING - labelReserved;
  const int maxLines = std::max(1, textHeight / (lineHeight + 2));
  const char* previewText = I18N.get(StrId::STR_FONT_PREVIEW_TEXT);
  if (auto* cache = renderer.getFontCacheManager()) {
    char warmText[256];
    snprintf(warmText, sizeof(warmText), "%s %s", previewText, ELLIPSIS_UTF8);
    cache->prewarmCache(fontId, warmText, 0x01);
  }

  const auto lines = renderer.wrappedText(fontId, previewText, width, maxLines);
  int y = top + PREVIEW_PADDING;
  const int bottom = top + height - labelReserved;
  for (const auto& line : lines) {
    if (y + lineHeight > bottom) break;
    renderer.drawText(fontId, left, y, line.c_str());
    y += lineHeight + 2;
  }
}

void FontSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);

  const StrId headerStr = target == Target::TXT ? StrId::STR_TXT_FONT_FAMILY : StrId::STR_FONT_FAMILY;
  GUI.drawHeader(renderer, Rect{contentRect.x, metrics.topPadding, contentRect.width, metrics.headerHeight},
                 I18N.get(headerStr));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = contentRect.height - contentTop - metrics.verticalSpacing;

  const int previewHeight = contentHeight * PREVIEW_HEIGHT_PERCENT / 100;
  const int listTop = contentTop + previewHeight + metrics.verticalSpacing;
  const int listHeight = contentHeight - previewHeight - metrics.verticalSpacing;

  const char* previewName = nullptr;
  std::string previewNameStorage;
  if (previewIndex >= 0 && previewIndex < fontCount) {
    previewNameStorage = fontFamilyOptionLabel(static_cast<uint8_t>(previewIndex));
    previewName = previewNameStorage.c_str();
  }
  renderPreviewPane(contentTop, previewHeight, previewFontId, previewName);
  const int separatorY = listTop - metrics.verticalSpacing / 2;
  renderer.drawLine(contentRect.x, separatorY, contentRect.x + contentRect.width, separatorY);

  const uint8_t activeIndex = static_cast<uint8_t>(target == Target::TXT ? txtFontFamilyDynamicGetter(nullptr)
                                                                         : fontFamilyDynamicGetter(nullptr));
  GUI.drawList(
      renderer, Rect{contentRect.x, listTop, contentRect.width, listHeight}, fontCount, selectedIndex,
      [](int index) { return fontFamilyOptionLabel(static_cast<uint8_t>(index)); }, nullptr, nullptr,
      [this, activeIndex](int index) -> std::string {
        if (index == previewIndex && index != activeIndex) return tr(STR_PREVIEW);
        return index == activeIndex ? tr(STR_SELECTED) : "";
      },
      true);

  const char* confirmLabel = selectedIndex == previewIndex ? tr(STR_SELECT) : tr(STR_PREVIEW);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
