#include "MinimalTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"

namespace {
constexpr int kCoverCornerRadius = 8;
constexpr int kButtonCornerRadius = 6;
constexpr int kProgressBarHeight = 6;
constexpr int kProgressBlockGap = 8;
constexpr int kProgressLabelGap = 5;
constexpr int kMenuPanelWidth = 384;
constexpr int kMenuRowHeight = 64;
constexpr int kMenuPanelTop = 210;
constexpr int kMenuPanelRadius = 7;
constexpr int kMenuPanelPadding = 8;
constexpr int kMenuIndexInset = 13;
constexpr int kMenuIndexColumnWidth = 36;
constexpr int kMenuSelectionTriangleWidth = 14;
constexpr int kMenuSelectionTriangleHeight = 20;
constexpr int kMenuSelectionTriangleRightInset = 20;

Rect coverRectForScreen(const GfxRenderer& renderer, const Rect& rect) {
  const int coverHeight = std::min(MinimalMetrics::values.homeCoverHeight, rect.height - 55);
  const int coverWidth = static_cast<int>((static_cast<int64_t>(coverHeight) * 3 + 2) / 5);
  return Rect{(renderer.getScreenWidth() - coverWidth) / 2, rect.y, coverWidth, coverHeight};
}

Rect fittedBitmapRect(const Bitmap& bitmap, const Rect& target) {
  if (bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) return target;
  const float scale =
      std::min(1.0f, std::min(static_cast<float>(target.width) / bitmap.getWidth(),
                             static_cast<float>(target.height) / bitmap.getHeight()));
  const int width = std::max(1, static_cast<int>(std::ceil(bitmap.getWidth() * scale)));
  const int height = std::max(1, static_cast<int>(std::ceil(bitmap.getHeight() * scale)));
  return Rect{target.x + (target.width - width) / 2, target.y + (target.height - height) / 2, width, height};
}

void drawMissingCover(const GfxRenderer& renderer, const Rect& coverRect, const RecentBook* book) {
  renderer.drawRoundedRect(coverRect.x, coverRect.y, coverRect.width, coverRect.height, 1, kCoverCornerRadius, true);
  const int dividerY = coverRect.y + coverRect.height / 3;
  renderer.drawLine(coverRect.x, dividerY, coverRect.x + coverRect.width - 1, dividerY);
  renderer.drawIcon(CoverIcon, coverRect.x + (coverRect.width - 32) / 2,
                    coverRect.y + (coverRect.height / 3 - 32) / 2, 32, 32);

  const char* emptyTitle = tr(STR_NO_OPEN_BOOK);
  const std::string title = book == nullptr ? emptyTitle : (book->title.empty() ? book->path : book->title);
  const std::string author = book == nullptr ? tr(STR_START_READING) : book->author;
  const int textX = coverRect.x + 18;
  const int textWidth = coverRect.width - 36;
  auto titleLines = renderer.wrappedText(UI_12_FONT_ID, title.c_str(), textWidth, 5, EpdFontFamily::BOLD);
  auto authorLines = renderer.wrappedText(UI_10_FONT_ID, author.c_str(), textWidth, 3);
  int y = dividerY + 28;
  for (const auto& line : titleLines) {
    renderer.drawText(UI_12_FONT_ID, textX, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_12_FONT_ID);
  }
  y += 18;
  for (const auto& line : authorLines) {
    renderer.drawText(UI_10_FONT_ID, textX, y, line.c_str());
    y += renderer.getLineHeight(UI_10_FONT_ID);
  }
}

void drawProgress(const GfxRenderer& renderer, const Rect& coverRect, const RecentBook& book) {
  const int progress = UITheme::getBookProgressPercent(book);
  if (progress < 0) return;
  const int barY = coverRect.y + coverRect.height + kProgressBlockGap;
  renderer.fillRectDither(coverRect.x, barY, coverRect.width, kProgressBarHeight, Color::LightGray);
  if (progress > 0) {
    renderer.fillRectDither(coverRect.x, barY, coverRect.width * std::min(progress, 100) / 100, kProgressBarHeight,
                            Color::DarkGray);
  }
  char label[8];
  snprintf(label, sizeof(label), "%d%%", progress);
  const int labelWidth = renderer.getTextWidth(UI_10_FONT_ID, label);
  renderer.drawText(UI_10_FONT_ID, coverRect.x + coverRect.width - labelWidth,
                    barY + kProgressBarHeight + kProgressLabelGap, label);
}
}  // namespace

void MinimalTheme::drawHeader(const GfxRenderer& renderer, const Rect rect, const char* title,
                              const char* subtitle) const {
  (void)subtitle;
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  const bool showPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int batteryX = rect.x + rect.width - 12 - MinimalMetrics::values.batteryWidth;
  drawBatteryRight(
      renderer,
      Rect{batteryX, rect.y + 5, MinimalMetrics::values.batteryWidth, MinimalMetrics::values.batteryHeight},
      showPercentage);

  if (title != nullptr) {
    constexpr int titleInset = 12;
    const int maxWidth = batteryX - rect.x - titleInset - MinimalMetrics::values.contentSidePadding;
    const std::string safeTitle =
        renderer.truncatedText(UI_12_FONT_ID, title, maxWidth, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, rect.x + titleInset,
                      rect.y + MinimalMetrics::values.batteryBarHeight + 3, safeTitle.c_str(), true,
                      EpdFontFamily::BOLD);
    renderer.drawLine(rect.x, rect.y + rect.height - 3, rect.x + rect.width - 1,
                      rect.y + rect.height - 3, 3, true);
  }
}

void MinimalTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                   const char* btn4) const {
  const auto originalOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageHeight = renderer.getScreenHeight();
  const int screenWidth = renderer.getScreenWidth();
  constexpr int buttonWidth = 80;
  constexpr int smallButtonHeight = 15;
  constexpr int textYOffset = 7;
  constexpr int x4Positions[] = {58, 146, 254, 342};
  constexpr int x3Positions[] = {65, 157, 291, 383};
  const int* positions = screenWidth > 500 ? x3Positions : x4Positions;
  const char* labels[] = {btn1, btn2, btn3, btn4};

  for (int i = 0; i < 4; ++i) {
    const bool hasLabel = labels[i] != nullptr && labels[i][0] != '\0';
    const int height = hasLabel ? MinimalMetrics::values.buttonHintsHeight : smallButtonHeight;
    const int y = pageHeight - height;
    renderer.fillRoundedRect(positions[i], y, buttonWidth, height, kButtonCornerRadius, Color::White);
    renderer.drawRoundedRect(positions[i], y, buttonWidth, height, 1, kButtonCornerRadius, true);
    if (hasLabel) {
      const std::string label = renderer.truncatedText(SMALL_FONT_ID, labels[i], buttonWidth - 8);
      const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, label.c_str());
      renderer.drawText(SMALL_FONT_ID, positions[i] + (buttonWidth - textWidth) / 2, y + textYOffset, label.c_str());
    }
  }
  renderer.setOrientation(originalOrientation);
}

void MinimalTheme::drawRecentBookCover(GfxRenderer& renderer, const Rect rect,
                                       const std::vector<RecentBook>& recentBooks, const int selectorIndex,
                                       bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                                       std::function<bool()> storeCoverBuffer) const {
  (void)selectorIndex;
  (void)bufferRestored;
  const Rect coverRect = coverRectForScreen(renderer, rect);

  if (recentBooks.empty()) {
    drawMissingCover(renderer, coverRect, nullptr);
    coverRendered = false;
    coverBufferStored = false;
    return;
  }

  const RecentBook& book = recentBooks.front();
  if (!coverRendered) {
    bool hasCover = false;
    if (!book.coverBmpPath.empty()) {
      const std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverRect.height);
      FsFile file;
      if (Storage.openFileForRead("HOME", coverPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          const Rect bitmapRect = fittedBitmapRect(bitmap, coverRect);
          renderer.fillRoundedRect(coverRect.x, coverRect.y, coverRect.width, coverRect.height, kCoverCornerRadius,
                                   Color::White);
          renderer.drawBitmap(bitmap, bitmapRect.x, bitmapRect.y, bitmapRect.width, bitmapRect.height);
          renderer.drawRoundedRect(bitmapRect.x, bitmapRect.y, bitmapRect.width, bitmapRect.height, 1,
                                   kCoverCornerRadius, true);
          hasCover = true;
        }
        file.close();
      }
    }
    if (!hasCover) drawMissingCover(renderer, coverRect, &book);
    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  }
  drawProgress(renderer, coverRect, book);
}

void MinimalTheme::drawButtonMenu(GfxRenderer& renderer, const Rect rect, const int buttonCount,
                                  const int selectedIndex,
                                  const std::function<std::string(int index)>& buttonLabel,
                                  const std::function<UIIcon(int index)>& rowIcon) const {
  (void)rect;
  (void)rowIcon;
  if (buttonCount <= 0) return;

  const int panelWidth = std::min(kMenuPanelWidth, renderer.getScreenWidth() - 80);
  const int panelHeight = buttonCount * kMenuRowHeight + kMenuPanelPadding * 2 + 2;
  const int panelX = (renderer.getScreenWidth() - panelWidth) / 2;
  const int panelY = std::max(MinimalMetrics::values.homeTopPadding, kMenuPanelTop - panelHeight / 4);
  renderer.drawRoundedRect(panelX, panelY, panelWidth, panelHeight, 1, kMenuPanelRadius, true);

  for (int i = 0; i < buttonCount; ++i) {
    const int rowX = panelX + kMenuPanelPadding;
    const int rowWidth = panelWidth - kMenuPanelPadding * 2;
    const int rowY = panelY + 1 + kMenuPanelPadding + i * kMenuRowHeight;
    const bool selected = i == selectedIndex;
    if (selected) {
      renderer.fillRoundedRect(rowX, rowY, rowWidth, kMenuRowHeight, 3, Color::Black);
      const int triangleX = rowX + rowWidth - kMenuSelectionTriangleRightInset - kMenuSelectionTriangleWidth;
      const int centerY = rowY + kMenuRowHeight / 2;
      const int xPoints[3] = {triangleX, triangleX, triangleX + kMenuSelectionTriangleWidth};
      const int yPoints[3] = {centerY - kMenuSelectionTriangleHeight / 2,
                              centerY + kMenuSelectionTriangleHeight / 2, centerY};
      renderer.fillPolygon(xPoints, yPoints, 3, false);
    } else if (i > 0) {
      renderer.fillRectDither(rowX, rowY, rowWidth, 1, Color::LightGray);
    }

    char indexLabel[4];
    snprintf(indexLabel, sizeof(indexLabel), "%02d", i + 1);
    const int textY = rowY + (kMenuRowHeight - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
    renderer.drawText(SMALL_FONT_ID, rowX + kMenuIndexInset,
                      rowY + (kMenuRowHeight - renderer.getLineHeight(SMALL_FONT_ID)) / 2, indexLabel, !selected);

    const int labelX = rowX + kMenuIndexInset + kMenuIndexColumnWidth;
    const int labelMaxWidth = rowWidth - kMenuIndexInset - kMenuIndexColumnWidth -
                              kMenuSelectionTriangleRightInset - kMenuSelectionTriangleWidth - 8;
    const std::string label = renderer.truncatedText(UI_12_FONT_ID, buttonLabel(i).c_str(), labelMaxWidth);
    renderer.drawText(UI_12_FONT_ID, labelX, textY, label.c_str(), !selected);
  }
}
