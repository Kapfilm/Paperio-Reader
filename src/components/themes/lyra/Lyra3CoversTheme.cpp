#include "Lyra3CoversTheme.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"

namespace {
constexpr int selectionGap = 6;
constexpr int selectionWidth = 4;

Rect fitCover(const Rect& box, const int width, const int height) {
  // Integer contain-fit, with the same scale on both axes. The outer box is
  // only a maximum: wide or unusually tall source covers are never cropped.
  // GfxRenderer does not enlarge a 2-bit bitmap, so keep the frame at the
  // native size too when a source image is smaller than the available box.
  int fittedWidth = std::min(box.width, width);
  int fittedHeight = std::max(1, static_cast<int>((static_cast<int64_t>(height) * fittedWidth + width - 1) / width));
  if (fittedHeight > box.height) {
    fittedHeight = box.height;
    fittedWidth = std::max(1, static_cast<int>((static_cast<int64_t>(width) * fittedHeight + height - 1) / height));
  }
  return Rect{box.x + (box.width - fittedWidth) / 2, box.y + (box.height - fittedHeight) / 2, fittedWidth,
              fittedHeight};
}
}  // namespace

void Lyra3CoversTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                           bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  constexpr int sidePadding = Lyra3CoversMetrics::values.contentSidePadding;
  constexpr int inset = Lyra3CoversMetrics::coverInset;
  const int tileWidth = (rect.width - 2 * sidePadding) / 3;
  const int coverHeight = Lyra3CoversMetrics::coverHeightForTile(rect.height);
  if (tileWidth <= 2 * inset || rect.height <= 2 * inset) return;
  if (recentBooks.empty()) {
    renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
    coverRendered = false;
    coverBufferStored = false;
    coverRects_ = {Rect{}, Rect{}, Rect{}};
    cachedBookCount_ = 0;
    coverArea_ = rect;
    drawEmptyRecents(renderer, rect);
    return;
  }

  const int bookCount = std::min(static_cast<int>(recentBooks.size()), 3);
  const bool geometryChanged = rect.x != coverArea_.x || rect.y != coverArea_.y || rect.width != coverArea_.width ||
                               rect.height != coverArea_.height || bookCount != cachedBookCount_;
  if (!coverRendered || !bufferRestored || geometryChanged) {
    // The cache contains only artwork and its thin border. Clear the whole row
    // before rebuilding, including any old selection frame or removed book.
    renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
    bool anyPending = false;
    coverRects_ = {Rect{}, Rect{}, Rect{}};
    for (int i = 0; i < bookCount; ++i) {
      const Rect box{rect.x + sidePadding + tileWidth * i + inset, rect.y + inset, tileWidth - 2 * inset, coverHeight};
      Rect artwork = fitCover(box, std::max(1, coverHeight * 3 / 5), coverHeight);
      bool hasCover = false;
      bool pending = false;
      if (!recentBooks[i].coverBmpPath.empty()) {
        const std::string path = UITheme::getCoverThumbPath(recentBooks[i].coverBmpPath, coverHeight);
        FsFile file;
        if (Storage.openFileForRead("HOME", path, file)) {
          Bitmap bitmap(file);
          if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
            artwork = fitCover(box, bitmap.getWidth(), bitmap.getHeight());
            // cropX/cropY stay zero: show every part of each original cover.
            renderer.drawBitmap(bitmap, artwork.x, artwork.y, artwork.width, artwork.height, 0, 0, true);
            hasCover = true;
          }
          file.close();
        } else {
          pending = true;
          anyPending = true;
        }
      }

      if (!hasCover) {
        if (pending) {
          const std::string label =
              renderer.truncatedText(SMALL_FONT_ID, tr(STR_LOADING), std::max(1, artwork.width - 8));
          const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, label.c_str());
          const int textHeight = renderer.getLineHeight(SMALL_FONT_ID);
          if (textHeight <= artwork.height)
            renderer.drawText(SMALL_FONT_ID, artwork.x + (artwork.width - textWidth) / 2,
                              artwork.y + (artwork.height - textHeight) / 2, label.c_str(), true);
        } else {
          renderer.fillRect(artwork.x, artwork.y + artwork.height / 3, artwork.width, 2 * artwork.height / 3, true);
          if (artwork.width >= 48 && artwork.height >= 64)
            renderer.drawIcon(CoverIcon, artwork.x + (artwork.width - 32) / 2, artwork.y + 12, 32, 32);
        }
      }
      renderer.drawRect(artwork.x, artwork.y, artwork.width, artwork.height, true);
      coverRects_[i] = artwork;
    }
    coverArea_ = rect;
    cachedBookCount_ = bookCount;
    coverBufferStored = !anyPending && storeCoverBuffer();
    coverRendered = coverBufferStored;
  }

  if (selectorIndex >= 0 && selectorIndex < bookCount) {
    const Rect& artwork = coverRects_[selectorIndex];
    renderer.drawRoundedRect(artwork.x - selectionGap, artwork.y - selectionGap, artwork.width + 2 * selectionGap,
                             artwork.height + 2 * selectionGap, selectionWidth, 4, true);
  }
  // No titles, authors, percentages, or progress bars: only the three covers.
}
