#include "RecentBooksActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>
#include <PngToBmpConverter.h>
#include <Xtc.h>

#include <algorithm>
#include <string>

#include "../ActivityManager.h"
#include "../util/ConfirmationActivity.h"
#include "BookInfoActivity.h"
#include "CrossPointState.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Mirror of HomeActivity::convertSidecarToBmp — converts a JPG/PNG sidecar cover to a
// 1-bit BMP at the requested WxH size and caches it under /.crosspoint/sidecar_<hash>/.
std::string convertSidecarToBmp(const std::string& bookPath, const std::string& sidecarPath, int width, int height,
                                const std::string& fileName) {
  const std::string cacheDir = "/.crosspoint/sidecar_" + std::to_string(std::hash<std::string>{}(bookPath));
  Storage.mkdir(cacheDir.c_str());
  const std::string bmpPath = cacheDir + "/" + fileName;
  if (Storage.exists(bmpPath.c_str())) return bmpPath;

  FsFile src;
  if (!Storage.openFileForRead("RBA", sidecarPath, src)) return "";
  FsFile dst;
  if (!Storage.openFileForWrite("RBA", bmpPath, dst)) {
    src.close();
    return "";
  }

  bool ok = false;
  if (FsHelpers::hasJpgExtension(sidecarPath)) {
    ok = JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(src, dst, width, height);
  } else if (FsHelpers::hasPngExtension(sidecarPath)) {
    ok = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(src, dst, width, height);
  }
  src.close();
  dst.close();
  if (!ok) {
    Storage.remove(bmpPath.c_str());
    return "";
  }
  return bmpPath;
}

// Compute the grid thumbnail width from the available content width.
int gridThumbWidth(int contentWidth) {
  const int margin = RecentBooksActivity::GRID_THUMB_MARGIN;
  const int cols = RecentBooksActivity::GRID_COLS;
  return (contentWidth - (cols + 1) * margin) / cols;
}

// Resolve the [HEIGHT] placeholder in a cover BMP path to the WxH thumbnail path.
std::string gridThumbPath(const std::string& coverBmpPath, int tw, int th) {
  return UITheme::getCoverThumbPath(coverBmpPath, tw, th);
}
}  // namespace

void RecentBooksActivity::loadRecentBooks() {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(books.size());
  for (const auto& book : books) {
    if (!Storage.exists(book.path.c_str())) continue;
    recentBooks.push_back(book);
  }
}

bool RecentBooksActivity::loadNextCover() {
  const Rect contentRect = UITheme::getContentRect(renderer, true, true);
  const int tw = gridThumbWidth(contentRect.width);
  const int th = GRID_THUMB_HEIGHT;

  for (; nextCoverIndex < recentBooks.size(); nextCoverIndex++) {
    RecentBook& book = recentBooks[nextCoverIndex];
    if (book.coverBmpPath.empty()) continue;

    const bool isSidecar =
        FsHelpers::hasJpgExtension(book.coverBmpPath) || FsHelpers::hasPngExtension(book.coverBmpPath);

    if (isSidecar) {
      if (!Storage.exists(book.coverBmpPath.c_str())) {
        RECENT_BOOKS.updateBook(book.path, book.title, book.author, book.series, "");
        book.coverBmpPath = "";
        continue;
      }
      const std::string cacheBase =
          "/.crosspoint/sidecar_" + std::to_string(std::hash<std::string>{}(book.path));
      const std::string placeholder = cacheBase + "/[HEIGHT].bmp";
      const std::string name = std::to_string(tw) + "x" + std::to_string(th) + ".bmp";
      const std::string result = convertSidecarToBmp(book.path, book.coverBmpPath, tw, th, name);
      if (!result.empty()) {
        RECENT_BOOKS.updateBook(book.path, book.title, book.author, book.series, placeholder);
        book.coverBmpPath = placeholder;
      }
      nextCoverIndex++;
      return false;  // yield — one conversion per loop tick
    }

    const std::string thumbPath = gridThumbPath(book.coverBmpPath, tw, th);
    if (!Storage.exists(thumbPath.c_str())) {
      bool ok = false;
      if (FsHelpers::hasEpubExtension(book.path)) {
        Epub epub(book.path, "/.crosspoint");
        epub.load(false, true);
        ok = epub.generateThumbBmp(tw, th);
      } else if (FsHelpers::hasXtcExtension(book.path)) {
        Xtc xtc(book.path, "/.crosspoint");
        if (xtc.load()) ok = xtc.generateThumbBmp(tw, th);
      }
      if (!ok) {
        RECENT_BOOKS.updateBook(book.path, book.title, book.author, book.series, "");
        book.coverBmpPath = "";
      }
      nextCoverIndex++;
      return false;  // yield — one generation per loop tick
    }
  }

  return true;  // all thumbnails are ready
}

void RecentBooksActivity::onEnter() {
  Activity::onEnter();

  loadRecentBooks();

  selectorIndex = 0;
  if (initialFocusIndex >= 0 && initialFocusIndex < static_cast<int>(recentBooks.size())) {
    selectorIndex = initialFocusIndex;
  }
  initialFocusIndex = -1;

  coversLoaded = false;
  coversLoading = false;
  firstRenderDone = false;
  nextCoverIndex = 0;

  requestUpdate();
}

void RecentBooksActivity::onExit() {
  Activity::onExit();
  recentBooks.clear();
}

void RecentBooksActivity::loop() {
  const bool gridView = APP_STATE.recentBooksGridView;

  // --- Consume named button events first ---
  ButtonEventManager::ButtonEvent ev;
  while (buttonEvents.consumeEvent(ev)) {
    // Open book (Confirm short/long)
    if (ev.button == MappedInputManager::Button::Confirm &&
        (ev.type == ButtonEventManager::PressType::Short || ev.type == ButtonEventManager::PressType::Long)) {
      if (recentBooks.empty() || selectorIndex >= static_cast<int>(recentBooks.size())) return;
      const bool longPress = (ev.type == ButtonEventManager::PressType::Long) && KOREADER_STORE.hasCredentials();
      const std::string& selectedPath = recentBooks[selectorIndex].path;
      const bool isEpubBook = FsHelpers::hasEpubExtension(selectedPath);
      LOG_DBG("RBA", "Selected recent book: %s (sync=%d epub=%d)", selectedPath.c_str(), longPress ? 1 : 0,
              isEpubBook ? 1 : 0);
      if (longPress && isEpubBook) {
        auto& sync = APP_STATE.koReaderSyncSession;
        sync.autoPullEpubPath = selectedPath;
        sync.exitToHomeAfterSync = false;
        APP_STATE.saveToFile();
      }
      ReturnHint hint;
      hint.target = ReturnTo::RecentBooks;
      hint.selectIndex = selectorIndex;
      activityManager.replaceWithReader(recentBooks[selectorIndex].path, std::move(hint));
      return;
    }

    // Back → home
    if (ev.button == MappedInputManager::Button::Back && ev.type == ButtonEventManager::PressType::Short) {
      onGoHome();
      return;
    }

    // Left short: switch view or (in list view) remove book
    if (ev.button == MappedInputManager::Button::Left && ev.type == ButtonEventManager::PressType::Short) {
      if (!gridView) {
        // List view → switch to grid view
        APP_STATE.recentBooksGridView = true;
        APP_STATE.saveToFile();
        coversLoaded = false;
        coversLoading = false;
        firstRenderDone = false;
        nextCoverIndex = 0;
        requestUpdate(true);
        return;
      }
      // Grid view: Left navigates to the previous item (column nav). The event is already
      // consumed from the queue, so handle it directly here.
      if (recentBooks.empty()) return;
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, static_cast<int>(recentBooks.size()));
      requestUpdate();
      return;
    }

    // Right short (list view only): show book info
    if (ev.button == MappedInputManager::Button::Right && ev.type == ButtonEventManager::PressType::Short) {
      if (!gridView) {
        if (recentBooks.empty() || selectorIndex >= static_cast<int>(recentBooks.size())) return;
        const std::string& path = recentBooks[selectorIndex].path;
        if (FsHelpers::hasEpubExtension(path) || FsHelpers::hasXtcExtension(path)) {
          startActivityForResult(std::make_unique<BookInfoActivity>(renderer, mappedInput, path),
                                 [this](const ActivityResult&) { requestUpdate(); });
          return;
        }
      } else {
        // Grid view: Right → move to next item (column navigation)
        if (recentBooks.empty()) return;
        selectorIndex = ButtonNavigator::nextIndex(selectorIndex, static_cast<int>(recentBooks.size()));
        requestUpdate();
        return;
      }
    }

    // Left long (list view only): remove book
    if (ev.button == MappedInputManager::Button::Left && ev.type == ButtonEventManager::PressType::Long) {
      if (!gridView) {
        if (recentBooks.empty() || selectorIndex >= static_cast<int>(recentBooks.size())) return;
        const std::string bookPath = recentBooks[selectorIndex].path;
        const std::string bookTitle = recentBooks[selectorIndex].title;

        auto handler = [this, bookPath](const ActivityResult& res) {
          if (!res.isCancelled) {
            LOG_DBG("RBA", "Removing from recent books: %s", bookPath.c_str());
            RECENT_BOOKS.removeBook(bookPath);
            loadRecentBooks();
            if (recentBooks.empty()) {
              selectorIndex = 0;
            } else if (selectorIndex >= static_cast<int>(recentBooks.size())) {
              selectorIndex = static_cast<int>(recentBooks.size()) - 1;
            }
            requestUpdate(true);
          }
        };
        std::string heading = tr(STR_REMOVE) + std::string("? ");
        startActivityForResult(
            std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, bookTitle), handler);
        return;
      }
    }
  }

  // --- ButtonNavigator for Up/Down (both views), and list-view Remove shortcut ---
  const int listSize = static_cast<int>(recentBooks.size());

  if (gridView) {
    // Up/Down navigate by full row (GRID_COLS items at a time)
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      if (!recentBooks.empty()) {
        selectorIndex = std::max(0, selectorIndex - GRID_COLS);
        requestUpdate();
      }
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      if (!recentBooks.empty()) {
        selectorIndex = std::min(listSize - 1, selectorIndex + GRID_COLS);
        requestUpdate();
      }
    }

    // Lazy cover loading after first render
    if (firstRenderDone && !coversLoaded && !coversLoading) {
      coversLoading = true;
      if (loadNextCover()) {
        coversLoaded = true;
      } else {
        requestUpdate();
      }
      coversLoading = false;
    }
  } else {
    // List view: Up/Down with double-click and long-press special behaviour
    buttonNavigator.onNextList({MappedInputManager::Button::Down}, selectorIndex, listSize,
                               [this] { requestUpdate(); });
    buttonNavigator.onPreviousList({MappedInputManager::Button::Up}, selectorIndex, listSize,
                                   [this] { requestUpdate(); });
  }
}

void RecentBooksActivity::render(RenderLock&& lock) {
  if (APP_STATE.recentBooksGridView) {
    renderGridView(std::move(lock));
    if (!firstRenderDone) {
      firstRenderDone = true;
      requestUpdate();
    }
  } else {
    renderListView(std::move(lock));
  }
}

void RecentBooksActivity::renderListView(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, true);

  GUI.drawHeader(renderer, Rect{contentRect.x, metrics.topPadding, contentRect.width, metrics.headerHeight},
                 tr(STR_MENU_RECENT_BOOKS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = contentRect.height - contentTop - metrics.verticalSpacing;

  if (recentBooks.empty()) {
    renderer.drawText(UI_10_FONT_ID, contentRect.x + metrics.contentSidePadding, contentTop + 20,
                      tr(STR_NO_RECENT_BOOKS));
  } else {
    GUI.drawList(
        renderer, Rect{contentRect.x, contentTop, contentRect.width, contentHeight},
        static_cast<int>(recentBooks.size()), selectorIndex,
        [this](int index) { return recentBooks[index].title; },
        [this](int index) {
          const auto& book = recentBooks[index];
          if (!book.author.empty() && !book.series.empty()) return book.author + "\n" + book.series;
          if (!book.series.empty()) return book.series;
          return book.author;
        },
        [this](int index) { return UITheme::getFileIcon(recentBooks[index].path); });
  }

  const bool hasInfo = !recentBooks.empty() && selectorIndex < static_cast<int>(recentBooks.size()) &&
                       (FsHelpers::hasEpubExtension(recentBooks[selectorIndex].path) ||
                        FsHelpers::hasXtcExtension(recentBooks[selectorIndex].path));
  const bool hasBooks = !recentBooks.empty();
  // Left short switches to grid view; label it accordingly. Long Left = remove.
  const auto labels =
      mappedInput.mapLabels(tr(STR_HOME), hasBooks ? tr(STR_OPEN) : "", tr(STR_VIEW_GRID),
                            hasInfo ? tr(STR_INFO) : "");

  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP), tr(STR_DIR_DOWN));

  renderer.displayBuffer();
}

void RecentBooksActivity::renderGridView(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, true);

  GUI.drawHeader(renderer, Rect{contentRect.x, metrics.topPadding, contentRect.width, metrics.headerHeight},
                 tr(STR_MENU_RECENT_BOOKS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = contentRect.height - contentTop - metrics.verticalSpacing;

  if (recentBooks.empty()) {
    renderer.drawText(UI_10_FONT_ID, contentRect.x + metrics.contentSidePadding, contentTop + 20,
                      tr(STR_NO_RECENT_BOOKS));
    const auto labels = mappedInput.mapLabels(tr(STR_HOME), "", tr(STR_VIEW_LIST), "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const int margin = GRID_THUMB_MARGIN;
  const int tw = gridThumbWidth(contentRect.width);
  const int th = GRID_THUMB_HEIGHT;
  const int cellHeight = th + GRID_LABEL_HEIGHT + margin;

  const int visibleRows = std::max(1, contentHeight / cellHeight);
  const int totalRows = (static_cast<int>(recentBooks.size()) + GRID_COLS - 1) / GRID_COLS;

  const int selectedRow = selectorIndex / GRID_COLS;
  const int pageStartRow = (selectedRow / visibleRows) * visibleRows;

  const int startIndex = pageStartRow * GRID_COLS;
  const int endIndex = std::min(startIndex + visibleRows * GRID_COLS, static_cast<int>(recentBooks.size()));

  for (int i = startIndex; i < endIndex; i++) {
    const int row = (i / GRID_COLS) - pageStartRow;
    const int col = i % GRID_COLS;

    const int cellX = contentRect.x + margin + col * (tw + margin);
    const int cellY = contentTop + row * cellHeight;

    const bool selected = (i == selectorIndex);

    // Draw thumbnail border (double rect when selected)
    if (selected) {
      renderer.drawRect(cellX - 2, cellY - 2, tw + 4, th + 4);
      renderer.drawRect(cellX - 1, cellY - 1, tw + 2, th + 2);
    } else {
      renderer.drawRect(cellX, cellY, tw, th);
    }

    // Draw cover BMP thumbnail
    const auto& book = recentBooks[i];
    bool coverDrawn = false;
    if (!book.coverBmpPath.empty()) {
      const std::string thumbPath = gridThumbPath(book.coverBmpPath, tw, th);
      FsFile file;
      if (Storage.openFileForRead("RBA", thumbPath, file)) {
        Bitmap bmp(file);
        if (bmp.parseHeaders() == BmpReaderError::Ok) {
          renderer.drawBitmap1Bit(bmp, cellX, cellY, tw, th);
          coverDrawn = true;
        }
        file.close();
      }
    }

    if (!coverDrawn) {
      // Empty placeholder — just a white box inside the border
      renderer.fillRect(cellX + 1, cellY + 1, tw - 2, th - 2, false);
    }

    // Label: title on line 1, author on line 2
    const int labelY = cellY + th + 3;
    const int labelW = tw - 4;
    const bool invert = !selected;

    std::string titleStr = renderer.truncatedText(SMALL_FONT_ID, book.title.c_str(), labelW);
    renderer.drawText(SMALL_FONT_ID, cellX + 2, labelY, titleStr.c_str(), invert);

    if (!book.author.empty()) {
      std::string authorStr = renderer.truncatedText(SMALL_FONT_ID, book.author.c_str(), labelW);
      renderer.drawText(SMALL_FONT_ID, cellX + 2, labelY + 17, authorStr.c_str(), invert);
    }
  }

  // Scroll arrows when content spans multiple pages
  if (totalRows > visibleRows) {
    constexpr int arrowSize = 6;
    const int centerX = contentRect.x + contentRect.width / 2;

    if (pageStartRow > 0) {
      const int arrowY = contentTop + 2;
      for (int j = 0; j < arrowSize; ++j) {
        const int half = arrowSize - 1 - j;
        renderer.drawLine(centerX - half, arrowY + j, centerX + half, arrowY + j);
      }
    }
    if (pageStartRow + visibleRows < totalRows) {
      const int arrowY = contentTop + contentHeight - arrowSize - 2;
      for (int j = 0; j < arrowSize; ++j) {
        renderer.drawLine(centerX - j, arrowY + j, centerX + j, arrowY + j);
      }
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_VIEW_LIST), "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP), tr(STR_DIR_DOWN));

  renderer.displayBuffer();
}
