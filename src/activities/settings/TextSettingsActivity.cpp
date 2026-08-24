#include "TextSettingsActivity.h"

#include <FontCacheManager.h>
#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SdCardFontGlobals.h"
#include "activities/RenderLock.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int PREVIEW_HEIGHT_PERCENT = 32;
constexpr int PREVIEW_PADDING = 12;
constexpr int PREVIEW_LABEL_GAP = 4;

constexpr StrId TAB_IDS[] = {StrId::STR_FONT_TAB, StrId::STR_SIZE_TAB, StrId::STR_LAYOUT_TAB, StrId::STR_STYLE_TAB};
constexpr StrId SIZE_IDS[] = {StrId::STR_SMALL, StrId::STR_MEDIUM, StrId::STR_LARGE, StrId::STR_X_LARGE};
constexpr StrId ALIGNMENT_IDS[] = {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER,
                                   StrId::STR_ALIGN_RIGHT, StrId::STR_BOOK_S_STYLE};
constexpr StrId DARKNESS_IDS[] = {StrId::STR_NORMAL, StrId::STR_DARK, StrId::STR_EXTRA_DARK,
                                  StrId::STR_MAX_DARK};

constexpr int LAYOUT_ROW_COUNT = 4;
constexpr int STYLE_BASE_ROW_COUNT = 6;
}  // namespace

void TextSettingsActivity::onEnter() {
  Activity::onEnter();
  fontCount = fontFamilyOptionCount();
  selectedRow = -1;
  renderer.setTextDarkness(SETTINGS.textDarkness);
  renderer.setFastGrayscaleLut(SETTINGS.fastAntiAliasing != 0);
  reloadReaderFont();
  requestUpdate();
}

void TextSettingsActivity::onExit() {
  SETTINGS.saveToFile();
  Activity::onExit();
}

const char* TextSettingsActivity::tabLabel(const Tab value) const {
  return I18N.get(TAB_IDS[static_cast<int>(value)]);
}

int TextSettingsActivity::rowCount() const {
  switch (tab) {
    case Tab::Font:
      return fontCount;
    case Tab::Size:
      return CrossPointSettings::FONT_SIZE_COUNT;
    case Tab::Layout:
      return LAYOUT_ROW_COUNT;
    case Tab::Style:
      return STYLE_BASE_ROW_COUNT + (gpio.deviceIsX3() ? 1 : 0);
    default:
      return 0;
  }
}

void TextSettingsActivity::switchTab() {
  const int next = (static_cast<int>(tab) + 1) % static_cast<int>(Tab::Count);
  tab = static_cast<Tab>(next);
  selectedRow = -1;
  requestUpdate();
}

void TextSettingsActivity::reloadReaderFont() {
  sdFontSystem.ensureLoadedForPreview(renderer, SETTINGS.sdFontFamilyName, SETTINGS.fontSize);
  previewFontId = SETTINGS.getReaderFontId();
}

void TextSettingsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    SETTINGS.saveToFile();
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedRow < 0) {
      switchTab();
    } else {
      activateRow(selectedRow);
    }
    return;
  }

  buttonNavigator.onNextRelease([this] {
    const int count = rowCount();
    const int position = (selectedRow + 2) % (count + 1);
    selectedRow = position - 1;
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    const int count = rowCount();
    const int position = (selectedRow + count + 1) % (count + 1);
    selectedRow = position - 1;
    requestUpdate();
  });
}

void TextSettingsActivity::activateRow(const int row) {
  bool reloadFont = false;
  switch (tab) {
    case Tab::Font:
      if (row >= 0 && row < fontCount && row != fontFamilyDynamicGetter(nullptr)) {
        RenderLock lock;
        fontFamilyDynamicSetter(nullptr, static_cast<uint8_t>(row));
        reloadReaderFont();
      }
      break;
    case Tab::Size:
      if (row >= 0 && row < CrossPointSettings::FONT_SIZE_COUNT && row != SETTINGS.fontSize) {
        SETTINGS.fontSize = static_cast<uint8_t>(row);
        reloadFont = true;
      }
      break;
    case Tab::Layout:
      switch (row) {
        case 0:
          SETTINGS.paragraphAlignment =
              static_cast<uint8_t>((SETTINGS.paragraphAlignment + 1) % CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT);
          break;
        case 1:
          SETTINGS.screenMargin = SETTINGS.screenMargin >= 40 ? 5 : static_cast<uint8_t>(SETTINGS.screenMargin + 5);
          break;
        case 2: {
          constexpr uint8_t step = 5;
          const int next = SETTINGS.lineHeightPercent + step;
          SETTINGS.lineHeightPercent = next > CrossPointSettings::MAX_LINE_HEIGHT_PERCENT
                                           ? CrossPointSettings::MIN_LINE_HEIGHT_PERCENT
                                           : static_cast<uint8_t>(next);
          break;
        }
        case 3:
          SETTINGS.extraParagraphSpacing = !SETTINGS.extraParagraphSpacing;
          break;
      }
      break;
    case Tab::Style:
      switch (row) {
        case 0:
          SETTINGS.bionicReading = !SETTINGS.bionicReading;
          break;
        case 1:
          SETTINGS.hyphenationEnabled = !SETTINGS.hyphenationEnabled;
          break;
        case 2:
          SETTINGS.embeddedStyle = !SETTINGS.embeddedStyle;
          break;
        case 3:
          SETTINGS.textAntiAliasing = !SETTINGS.textAntiAliasing;
          break;
        case 4:
          SETTINGS.textDarkness =
              static_cast<uint8_t>((SETTINGS.textDarkness + 1) % CrossPointSettings::TEXT_DARKNESS_COUNT);
          renderer.setTextDarkness(SETTINGS.textDarkness);
          break;
        case 5:
          SETTINGS.fontSizeNormalization = !SETTINGS.fontSizeNormalization;
          break;
        case 6:
          SETTINGS.fastAntiAliasing = !SETTINGS.fastAntiAliasing;
          renderer.setFastGrayscaleLut(SETTINGS.fastAntiAliasing != 0);
          break;
      }
      break;
    default:
      break;
  }

  if (reloadFont) {
    RenderLock lock;
    reloadReaderFont();
  }
  SETTINGS.saveToFile();
  requestUpdate();
}

std::string TextSettingsActivity::rowTitle(const int row) const {
  switch (tab) {
    case Tab::Font:
      return fontFamilyOptionLabel(static_cast<uint8_t>(row));
    case Tab::Size:
      return I18N.get(SIZE_IDS[row]);
    case Tab::Layout: {
      constexpr StrId ids[] = {StrId::STR_PARA_ALIGNMENT, StrId::STR_SCREEN_MARGIN, StrId::STR_LINE_SPACING,
                               StrId::STR_EXTRA_SPACING};
      return I18N.get(ids[row]);
    }
    case Tab::Style: {
      constexpr StrId ids[] = {StrId::STR_BIONIC_READING, StrId::STR_HYPHENATION, StrId::STR_EMBEDDED_STYLE,
                               StrId::STR_TEXT_AA, StrId::STR_TEXT_DARKNESS, StrId::STR_FONT_SIZE_NORMALIZATION,
                               StrId::STR_FAST_AA};
      return I18N.get(ids[row]);
    }
    default:
      return {};
  }
}

std::string TextSettingsActivity::rowValue(const int row) const {
  const auto state = [](const bool enabled) {
    return std::string(I18N.get(enabled ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF));
  };
  switch (tab) {
    case Tab::Font:
      return row == fontFamilyDynamicGetter(nullptr) ? tr(STR_SELECTED) : "";
    case Tab::Size:
      return row == SETTINGS.fontSize ? tr(STR_SELECTED) : "";
    case Tab::Layout:
      switch (row) {
        case 0:
          return I18N.get(ALIGNMENT_IDS[std::min<int>(SETTINGS.paragraphAlignment,
                                                      CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT - 1)]);
        case 1:
          return std::to_string(SETTINGS.screenMargin) + " px";
        case 2:
          return std::to_string(SETTINGS.lineHeightPercent) + "%";
        case 3:
          return state(SETTINGS.extraParagraphSpacing);
      }
      break;
    case Tab::Style:
      switch (row) {
        case 0:
          return state(SETTINGS.bionicReading);
        case 1:
          return state(SETTINGS.hyphenationEnabled);
        case 2:
          return state(SETTINGS.embeddedStyle);
        case 3:
          return state(SETTINGS.textAntiAliasing);
        case 4:
          return I18N.get(DARKNESS_IDS[std::min<int>(SETTINGS.textDarkness,
                                                     CrossPointSettings::TEXT_DARKNESS_COUNT - 1)]);
        case 5:
          return state(SETTINGS.fontSizeNormalization);
        case 6:
          return state(SETTINGS.fastAntiAliasing);
      }
      break;
    default:
      break;
  }
  return {};
}

void TextSettingsActivity::renderPreview(const int top, const int height) const {
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);
  const int left = contentRect.x + PREVIEW_PADDING;
  const int paneWidth = contentRect.width - PREVIEW_PADDING * 2;
  if (previewFontId == 0 || paneWidth <= 0 || height <= 0) return;

  const int labelHeight = renderer.getTextHeight(SMALL_FONT_ID);
  const int labelReserved = labelHeight + PREVIEW_LABEL_GAP + PREVIEW_PADDING;
  const uint8_t activeFamily = fontFamilyDynamicGetter(nullptr);
  const std::string family = fontFamilyOptionLabel(activeFamily);
  char label[128];
  snprintf(label, sizeof(label), "%s \"%s, %s\"", tr(STR_PREVIEW), family.c_str(), I18N.get(SIZE_IDS[SETTINGS.fontSize]));
  renderer.drawText(SMALL_FONT_ID, left, top + height - PREVIEW_PADDING - labelHeight, label);

  const char* sample = I18N.get(StrId::STR_FONT_PREVIEW_TEXT);
  if (auto* cache = renderer.getFontCacheManager()) cache->prewarmCache(previewFontId, sample, 0x01);
  renderPreviewSample(top, height, labelReserved);
}

void TextSettingsActivity::renderPreviewSample(const int top, const int height, const int labelReserved) const {
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);
  const int left = contentRect.x + PREVIEW_PADDING;
  const int paneWidth = contentRect.width - PREVIEW_PADDING * 2;
  if (previewFontId == 0 || paneWidth <= 0 || height <= 0) return;

  const int textLeft = left + SETTINGS.screenMargin;
  const int textWidth = paneWidth - SETTINGS.screenMargin * 2;
  if (textWidth <= 0) return;
  const char* sample = I18N.get(StrId::STR_FONT_PREVIEW_TEXT);

  const int lineHeight = renderer.getTextHeight(previewFontId);
  const int lineAdvance = std::max(
      1, static_cast<int>(renderer.getLineHeight(previewFontId) * SETTINGS.getReaderLineCompression() + 0.5f));
  const int textBottom = top + height - labelReserved;
  const int maxLines = std::max(1, (textBottom - top - PREVIEW_PADDING) / lineAdvance);
  const auto lines = renderer.wrappedText(previewFontId, sample, textWidth, maxLines);
  int y = top + PREVIEW_PADDING;
  for (const auto& line : lines) {
    if (y + lineHeight > textBottom) break;
    int x = textLeft;
    const int width = renderer.getTextWidth(previewFontId, line.c_str());
    if (SETTINGS.paragraphAlignment == CrossPointSettings::CENTER_ALIGN) x += std::max(0, (textWidth - width) / 2);
    if (SETTINGS.paragraphAlignment == CrossPointSettings::RIGHT_ALIGN) x += std::max(0, textWidth - width);
    renderer.drawText(previewFontId, x, y, line.c_str());
    y += lineAdvance;
  }
}

void TextSettingsActivity::renderTabBar(const Rect& rect) const {
  constexpr int count = static_cast<int>(Tab::Count);
  const int cellWidth = rect.width / count;
  const int fontId = UI_10_FONT_ID;
  const int textHeight = renderer.getTextHeight(fontId);

  for (int i = 0; i < count; i++) {
    const int x = rect.x + i * cellWidth;
    const int width = (i == count - 1) ? rect.x + rect.width - x : cellWidth;
    const bool active = i == static_cast<int>(tab);
    const bool focused = active && selectedRow < 0;
    if (focused) {
      renderer.fillRect(x + 1, rect.y + 1, width - 2, rect.height - 2);
    } else if (active) {
      renderer.drawLine(x + 3, rect.y + rect.height - 3, x + width - 4, rect.y + rect.height - 3, 2, true);
    }

    const char* label = tabLabel(static_cast<Tab>(i));
    const int textWidth = renderer.getTextWidth(fontId, label);
    const int textX = x + std::max(0, (width - textWidth) / 2);
    const int textY = rect.y + std::max(0, (rect.height - textHeight) / 2);
    renderer.drawText(fontId, textX, textY, label, !focused);
    if (i > 0) renderer.drawLine(x, rect.y + 4, x, rect.y + rect.height - 5);
  }
  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1);
}

void TextSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);
  GUI.drawHeader(renderer, Rect{contentRect.x, metrics.topPadding, contentRect.width, metrics.headerHeight},
                 tr(STR_TEXT_SETTINGS));

  const int afterHeader = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int usableHeight = contentRect.height - afterHeader - metrics.verticalSpacing;
  const int previewHeight = usableHeight * PREVIEW_HEIGHT_PERCENT / 100;
  renderPreview(afterHeader, previewHeight);

  const int tabTop = afterHeader + previewHeight;
  renderer.drawLine(contentRect.x, tabTop - 1, contentRect.x + contentRect.width - 1, tabTop - 1);
  renderTabBar(Rect{contentRect.x, tabTop, contentRect.width, metrics.tabBarHeight});

  const int listTop = tabTop + metrics.tabBarHeight + metrics.verticalSpacing;
  const int listHeight = contentRect.height - listTop - metrics.verticalSpacing;
  GUI.drawList(renderer, Rect{contentRect.x, listTop, contentRect.width, listHeight}, rowCount(), selectedRow,
               [this](int row) { return rowTitle(row); }, nullptr, nullptr,
               [this](int row) { return rowValue(row); }, true);

  const char* confirm = selectedRow < 0
                            ? tabLabel(static_cast<Tab>((static_cast<int>(tab) + 1) % static_cast<int>(Tab::Count)))
                            : tr(STR_TOGGLE);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirm, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);

  // Use the same two-bit glyph pipeline as the reader so text-darkness is an
  // exact physical preview. renderGrayscalePlanesSequential() applies the
  // fading compensation after the gray waveform, keeping the white panel
  // background stable while the user compares the four levels.
  if (tab == Tab::Style && SETTINGS.textAntiAliasing &&
      SETTINGS.textDarkness < CrossPointSettings::DARKNESS_MAXIMUM) {
    const int labelHeight = renderer.getTextHeight(SMALL_FONT_ID);
    const int labelReserved = labelHeight + PREVIEW_LABEL_GAP + PREVIEW_PADDING;
    renderer.renderGrayscalePlanesSequential(
        [this, afterHeader, previewHeight, labelReserved](GfxRenderer::RenderMode) {
          renderPreviewSample(afterHeader, previewHeight, labelReserved);
        },
        [] { return false; });
  }
}
