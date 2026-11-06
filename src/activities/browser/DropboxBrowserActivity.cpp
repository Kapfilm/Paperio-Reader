#include "DropboxBrowserActivity.h"

#include <HalClock.h>
#include <I18n.h>
#include <WiFi.h>

#include <algorithm>
#include <cctype>
#include <utility>

#include "BrowserListLayout.h"
#include "DropboxStore.h"
#include "MappedInputManager.h"
#include "UiFonts.h"
#include "activities/NetworkMemoryTrim.h"
#include "activities/network/CrossPointWebServerActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr size_t cacheBudget = 6 * 1024;
constexpr size_t maxCachedPages = 3;
constexpr unsigned long pageLifetimeMs = 90000;

// Keep the format in a separate right-aligned column even for short titles.
std::string bookFormat(const std::string& name) {
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lower.size() >= 8 && lower.compare(lower.size() - 8, 8, ".fb2.zip") == 0) return "fb2.zip";
  const auto dot = lower.find_last_of('.');
  return dot == std::string::npos ? "" : lower.substr(dot + 1);
}
}  // namespace

void DropboxBrowserActivity::onEnter() {
  Activity::onEnter();
  auto& store = DropboxStore::instance();
  if (!store.load()) {
    showError(DropboxClient::Error::File);
    return;
  }
  if (!store.configured() || !store.linked()) {
    requestUpdate();
    return;
  }
  queueFetch("");
}

void DropboxBrowserActivity::onExit() {
  Activity::onExit();
  // The web-server activity assumes ownership of the active Wi-Fi session and
  // restores the display when it exits. Keep the active Wi-Fi session for it.
  if (handingOffToWeb_ || !memoryTrimmed_) return;
  if (WiFi.getMode() != WIFI_MODE_NULL) HalClock::wifiOff(true);
  page_ = {};
  std::vector<CachedPage>().swap(cachedPages_);
  std::vector<FolderFrame>().swap(folderHistory_);
  restoreDisplayAfterNetworkSession(renderer);
}

int DropboxBrowserActivity::nextRow() const { return page_.hasMore ? static_cast<int>(page_.books.size()) : -1; }

int DropboxBrowserActivity::rowCount() const { return static_cast<int>(page_.books.size()) + (page_.hasMore ? 1 : 0); }

void DropboxBrowserActivity::queueFetch(const std::string& cursor, const int restoreSelection) {
  {
    RenderLock lock(*this);
    pendingCursor_ = cursor;
    restoreSelection_ = restoreSelection;
    if (!havePage_ && restoreCachedPage(pendingCursor_)) {
      pending_ = PendingAction::None;
      selectedIndex_ = std::clamp(restoreSelection_, 0, std::max(0, rowCount() - 1));
      state_ = State::Browsing;
      requestUpdate();
      return;
    }
    pending_ = PendingAction::Fetch;
    state_ = State::Loading;
  }
}

void DropboxBrowserActivity::cacheCurrentPage(const std::string& destinationId, const std::string& destinationCursor) {
  const auto now = millis();
  std::erase_if(cachedPages_, [now](const CachedPage& item) { return now - item.fetchedAt >= pageLifetimeMs; });
  if (!havePage_ || now - pageFetchedAt_ >= pageLifetimeMs) return;
  size_t bytes = sizeof(CachedPage) + currentFolderId_.size() + currentCursor_.size() + page_.cursor.capacity() +
                 page_.books.capacity() * sizeof(DropboxClient::Entry);
  for (const auto& entry : page_.books) bytes += entry.id.capacity() + entry.name.capacity();
  if (bytes > cacheBudget) return;
  size_t used = 0;
  for (const auto& item : cachedPages_) used += item.bytes;
  while (cachedPages_.size() >= maxCachedPages || used + bytes > cacheBudget) {
    const auto evict = std::find_if(cachedPages_.begin(), cachedPages_.end(), [&](const CachedPage& item) {
      return item.folderId != destinationId || item.cursor != destinationCursor;
    });
    if (evict == cachedPages_.end()) return;
    used -= evict->bytes;
    cachedPages_.erase(evict);
  }
  cachedPages_.push_back({currentFolderId_, currentCursor_, std::move(page_), pageFetchedAt_, bytes});
}

bool DropboxBrowserActivity::restoreCachedPage(const std::string& cursor) {
  const auto now = millis();
  std::erase_if(cachedPages_, [now](const CachedPage& item) { return now - item.fetchedAt >= pageLifetimeMs; });
  const auto it = std::find_if(cachedPages_.begin(), cachedPages_.end(), [&](const CachedPage& item) {
    return item.folderId == currentFolderId_ && item.cursor == cursor;
  });
  if (it == cachedPages_.end()) return false;
  page_ = std::move(it->page);
  pageFetchedAt_ = it->fetchedAt;
  currentCursor_ = cursor;
  havePage_ = true;
  cachedPages_.erase(it);
  return true;
}

void DropboxBrowserActivity::openFolder(const DropboxClient::Entry& folder) {
  constexpr size_t maxDepth = 32;
  constexpr size_t maxHistoryBytes = 16 * 1024;
  size_t historyBytes = currentFolderId_.size() + currentFolderName_.size() + currentCursor_.size();
  for (const auto& frame : folderHistory_) historyBytes += frame.id.size() + frame.name.size() + frame.cursor.size();
  const size_t nextFolderBytes = folder.id.size() + folder.name.size();
  if (folderHistory_.size() >= maxDepth || historyBytes > maxHistoryBytes ||
      nextFolderBytes > maxHistoryBytes - historyBytes) {
    {
      RenderLock lock(*this);
      state_ = State::Message;
      status_ = tr(STR_DROPBOX_DEPTH_LIMIT);
      detail_.clear();
    }
    requestUpdate();
    return;
  }
  // Copy first: folder is a reference into the page that is discarded below.
  std::string id = folder.id;
  std::string name = folder.name;
  {
    RenderLock lock(*this);
    cacheCurrentPage(id, "");
    folderHistory_.push_back({currentFolderId_, currentFolderName_, currentCursor_, selectedIndex_});
    currentFolderId_ = std::move(id);
    currentFolderName_ = std::move(name);
    currentCursor_.clear();
    page_ = {};
    havePage_ = false;
    selectedIndex_ = 0;
  }
  queueFetch("");
}

void DropboxBrowserActivity::goToParentFolder() {
  if (folderHistory_.empty()) {
    finish();
    return;
  }
  FolderFrame parent;
  {
    RenderLock lock(*this);
    parent = std::move(folderHistory_.back());
    folderHistory_.pop_back();
    cacheCurrentPage(parent.id, parent.cursor);
    currentFolderId_ = std::move(parent.id);
    currentFolderName_ = std::move(parent.name);
    currentCursor_.clear();
    page_ = {};
    havePage_ = false;
    selectedIndex_ = 0;
  }
  // Commit the upward move before fetching. If the parent vanished or Wi-Fi
  // fails, Back can still go farther up (and exit at root), rather than trap
  // the user in a failed child request. Retry stays on this parent and cursor.
  queueFetch(parent.cursor, parent.selection);
}

void DropboxBrowserActivity::trimNetworkMemory() {
  // Render/status work must finish before releasing display/font allocations;
  // never hold this mutex across association or TLS calls.
  requestUpdateAndWait();
  RenderLock lock(*this);
  trimMemoryForNetworkSession(renderer, "DBXUI");
  memoryTrimmed_ = true;
}

void DropboxBrowserActivity::launchWifiSelection() {
  {
    RenderLock lock(*this);
    state_ = State::WifiSelection;
  }
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             pending_ = PendingAction::None;
                             showError(DropboxClient::Error::Network);
                             return;
                           }
                           {
                             RenderLock lock(*this);
                             state_ = State::Loading;
                           }
                           requestUpdate();
                         });
}

void DropboxBrowserActivity::executePending() {
  const bool connected = WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
  if (!memoryTrimmed_ || !connected) {
    trimNetworkMemory();  // Show the initial loading screen before association.
  } else if (pending_ == PendingAction::Fetch) {
    // Keep the current screen visible until the new folder is ready. An
    // intermediate e-ink refresh would block the start of every request.
    RenderLock lock(*this);
    trimMemoryForNetworkSession(renderer, "DBXUI");
  }
  if (!connected) {
    launchWifiSelection();
    return;
  }
  const auto action = pending_;
  pending_ = PendingAction::None;
  if (action == PendingAction::Fetch) fetchPage();
  if (action == PendingAction::Download) downloadBook();
}

void DropboxBrowserActivity::fetchPage() {
  DropboxClient::Page resultPage;
  const auto result = client_.list(resultPage, pendingCursor_, currentFolderId_);
  if (!result.ok()) {
    showError(result.error);
    return;
  }
  if (!resultPage.hasMore) std::string().swap(resultPage.cursor);
  {
    RenderLock lock(*this);
    page_ = std::move(resultPage);
    pageFetchedAt_ = millis();
    currentCursor_ = pendingCursor_;
    havePage_ = true;
    selectedIndex_ = std::max(0, std::min(restoreSelection_, rowCount() - 1));
    state_ = State::Browsing;
  }
  requestUpdate();
}

void DropboxBrowserActivity::downloadBook() {
  if (pendingBook_ < 0 || pendingBook_ >= static_cast<int>(page_.books.size())) {
    returnToBooks();
    return;
  }
  const auto& entry = page_.books[pendingBook_];
  {
    RenderLock lock(*this);
    status_ = entry.name;
    detail_.clear();
    downloaded_ = 0;
    total_ = static_cast<size_t>(entry.size);
    state_ = State::Downloading;
  }
  requestUpdateAndWait();
  // The download's initial status render may repopulate the font cache.
  {
    RenderLock lock(*this);
    trimMemoryForNetworkSession(renderer, "DBXUI");
    cachedPages_.clear();
  }
  uint32_t lastUpdate = millis();
  std::string savedPath;
  const auto result = client_.download(entry, savedPath, [this, &lastUpdate](size_t downloaded, size_t total) {
    mappedInput.update();
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      ignoreBackUntilRelease_ = true;
      return false;
    }
    const uint32_t now = millis();
    if (now - lastUpdate >= 3000) {
      lastUpdate = now;
      {
        RenderLock lock(*this);
        downloaded_ = downloaded;
        total_ = total;
      }
      requestUpdateAndWait();
    }
    return true;
  });
  if (result.error == DropboxClient::Error::Cancelled) {
    returnToBooks();
  } else if (!result.ok()) {
    showError(result.error);
  } else {
    {
      RenderLock lock(*this);
      state_ = State::Message;
      status_ = tr(STR_DROPBOX_SAVED);
      detail_ = std::move(savedPath);
    }
    requestUpdate();
  }
}

void DropboxBrowserActivity::openWebSetup() {
  // The standard web activity supplies Wi-Fi selection, its URL/QR display,
  // full framebuffer release, and a clean restart on exit.
  handingOffToWeb_ = true;
  activityManager.replaceActivity(std::make_unique<CrossPointWebServerActivity>(renderer, mappedInput));
}

void DropboxBrowserActivity::returnToBooks() {
  {
    RenderLock lock(*this);
    state_ = havePage_ ? State::Browsing : State::Setup;
    selectedIndex_ = std::max(0, std::min(selectedIndex_, rowCount() - 1));
  }
  requestUpdate();
}

void DropboxBrowserActivity::showError(const DropboxClient::Error error) {
  {
    RenderLock lock(*this);
    lastError_ = error;
    state_ = State::Error;
  }
  requestUpdate();
}

const char* DropboxBrowserActivity::errorText() const {
  switch (lastError_) {
    case DropboxClient::Error::NotConfigured:
    case DropboxClient::Error::NotLinked:
    case DropboxClient::Error::Auth:
      return tr(STR_DROPBOX_AUTH_REQUIRED);
    case DropboxClient::Error::Network:
      return tr(STR_DROPBOX_NETWORK_ERROR);
    case DropboxClient::Error::RateLimited:
      return tr(STR_DROPBOX_RATE_LIMITED);
    case DropboxClient::Error::TooLarge:
      return tr(STR_DROPBOX_TOO_LARGE);
    case DropboxClient::Error::InvalidData:
      return tr(STR_DROPBOX_INVALID_RESPONSE);
    case DropboxClient::Error::File:
      return tr(STR_DROPBOX_SD_ERROR);
    case DropboxClient::Error::AlreadyExists:
      return tr(STR_DROPBOX_ALREADY_EXISTS);
    case DropboxClient::Error::Busy:
      return tr(STR_DROPBOX_BUSY);
    case DropboxClient::Error::Api:
      return tr(STR_DROPBOX_API_ERROR);
    case DropboxClient::Error::Cancelled:
    case DropboxClient::Error::Ok:
      return tr(STR_DOWNLOAD_FAILED);
  }
  return tr(STR_DOWNLOAD_FAILED);
}

void DropboxBrowserActivity::selectRow() {
  if (selectedIndex_ < static_cast<int>(page_.books.size())) {
    if (page_.books[selectedIndex_].isFolder) {
      openFolder(page_.books[selectedIndex_]);
      return;
    }
    pendingBook_ = selectedIndex_;
    pendingCursor_ = currentCursor_;
    pending_ = PendingAction::Download;
    {
      RenderLock lock(*this);
      state_ = State::Loading;
    }
  } else if (selectedIndex_ == nextRow()) {
    queueFetch(page_.cursor);
  }
}

bool DropboxBrowserActivity::onLongBack() {
  if (!ignoreBackUntilRelease_) activityManager.goHome();
  return true;
}

void DropboxBrowserActivity::loop() {
  if (ignoreBackUntilRelease_) {
    if (mappedInput.isPressed(MappedInputManager::Button::Back)) return;
    ignoreBackUntilRelease_ = false;
    buttonEvents.drain();
    return;
  }
  ButtonEventManager::ButtonEvent event{};
  while (buttonEvents.consumeEvent(event)) {
    if (event.button != MappedInputManager::Button::Back) continue;
    if (event.type == ButtonEventManager::PressType::Long) continue;
    pending_ = PendingAction::None;
    if ((state_ == State::Message || state_ == State::Error) && havePage_)
      returnToBooks();
    else
      goToParentFolder();
    return;
  }
  if (state_ == State::WifiSelection || state_ == State::Downloading) return;
  if (pending_ != PendingAction::None) {
    executePending();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (state_ == State::Setup)
      openWebSetup();
    else if (state_ == State::Browsing)
      selectRow();
    else if (state_ == State::Message)
      returnToBooks();
    else if (state_ == State::Error)
      queueFetch(pendingCursor_, restoreSelection_);
    return;
  }
  if (state_ == State::Browsing) {
    navigator_.onNextList(selectedIndex_, rowCount(), [this] { requestUpdate(); });
    navigator_.onPreviousList(selectedIndex_, rowCount(), [this] { requestUpdate(); });
  }
}

void DropboxBrowserActivity::drawMessage(const Rect& content, const char* text, const std::string& detail,
                                         bool withProgress) const {
  constexpr int progressHeight = 16;
  constexpr int progressGap = 8;
  const int width = content.width - 40;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int progressSpace = withProgress ? progressGap + progressHeight : 0;
  const int detailGap = detail.empty() ? 0 : 16;
  const int maxLines = std::max(0, (content.height - progressSpace - detailGap) / lineHeight);
  const auto lines =
      renderer.wrappedText(UI_10_FONT_ID, text, width, std::min(10, std::max(0, maxLines - (detail.empty() ? 0 : 1))));
  const auto detailLines = renderer.wrappedText(UI_10_FONT_ID, detail.c_str(), width,
                                                std::min(4, maxLines - static_cast<int>(lines.size())));
  const int blockHeight = static_cast<int>(lines.size() + detailLines.size()) * lineHeight + detailGap + progressSpace;
  int y = content.y + std::max(0, (content.height - blockHeight) / 2);
  const auto drawLines = [&](const auto& block) {
    for (const auto& line : block) {
      const int x = content.x + (content.width - renderer.getTextWidth(UI_10_FONT_ID, line.c_str())) / 2;
      renderer.drawText(UI_10_FONT_ID, x, y, line.c_str());
      y += lineHeight;
    }
  };
  drawLines(lines);
  y += detailGap;
  drawLines(detailLines);
  if (withProgress && content.width > 80 && y + progressSpace <= content.y + content.height) {
    GUI.drawProgressBar(renderer, Rect{content.x + 40, y + progressGap, content.width - 80, progressHeight},
                        downloaded_, total_);
  }
}

void DropboxBrowserActivity::drawEntries(const Rect& body) {
  const int rowHeight = BrowserList::rowHeight(renderer);
  const int visibleRows = body.height / rowHeight;
  if (visibleRows < 1 || rowCount() < 1) return;
  const int first = selectedIndex_ / visibleRows * visibleRows;
  const int left = body.x + BrowserList::leftPadding;
  // The same fixed column is used on every row, independent of title length.
  const int right = body.x + body.width - BrowserList::rightPadding;
  int formatWidth = 0;
  for (const auto& entry : page_.books) {
    if (!entry.isFolder)
      formatWidth = std::max(formatWidth, renderer.getTextWidth(UI_10_FONT_ID, bookFormat(entry.name).c_str()));
  }
  for (int index = first; index < rowCount() && index < first + visibleRows; ++index) {
    const int y = body.y + (index - first) * rowHeight;
    const bool ink = index != selectedIndex_;
    if (!ink) renderer.fillRect(body.x, y, body.width, rowHeight);
    const int textY = y + BrowserList::rowPadding;
    if (index == nextRow()) {
      renderer.drawText(UI_10_FONT_ID, left, textY, tr(STR_NEXT_PAGE), ink);
      continue;
    }
    const auto& entry = page_.books[index];
    int textLeft = left;
    std::string title = entry.name;
    if (entry.isFolder) {
      BrowserList::drawFolderIcon(renderer, left, y, rowHeight, ink);
      textLeft += BrowserList::folderTextOffset;
    } else {
      const std::string format = bookFormat(entry.name);
      const size_t suffixLength = format.size() + 1;
      if (title.size() > suffixLength) title.resize(title.size() - suffixLength);
      renderer.drawText(UI_10_FONT_ID, right - renderer.getTextWidth(UI_10_FONT_ID, format.c_str()), textY,
                        format.c_str(), ink);
    }
    const int titleRight = entry.isFolder ? right : right - formatWidth - 16;
    title = renderer.truncatedText(UI_10_FONT_ID, title.c_str(), std::max(1, titleRight - textLeft));
    renderer.drawText(UI_10_FONT_ID, textLeft, textY, title.c_str(), ink);
  }
}

void DropboxBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect content = UITheme::getContentRect(renderer, true, false);
  const int headerTop = content.y + metrics.topPadding;
  GUI.drawHeader(renderer, Rect{content.x, headerTop, content.width, metrics.headerHeight}, tr(STR_DROPBOX));
  const int top = headerTop + metrics.headerHeight + metrics.verticalSpacing;
  const Rect body{content.x, top, content.width, content.y + content.height - top};

  const char* confirm = tr(STR_SELECT);
  const char* previous = "";
  const char* next = "";
  if (state_ == State::Setup) {
    drawMessage(body, tr(STR_DROPBOX_SETUP_HELP));
    confirm = tr(STR_DROPBOX_SETUP);
  } else if (state_ == State::Error) {
    drawMessage(body, errorText());
    confirm = tr(STR_RETRY);
  } else if (state_ == State::Message) {
    drawMessage(body, status_.c_str(), detail_);
    confirm = tr(STR_DONE);
  } else if (state_ == State::Loading || state_ == State::WifiSelection) {
    drawMessage(body, tr(STR_LOADING));
    confirm = "";
  } else if (state_ == State::Downloading) {
    drawMessage(body, tr(STR_DOWNLOADING), status_, total_ > 0);
    confirm = "";
  } else {
    auto& store = DropboxStore::instance();
    const std::string folder = currentFolderId_.empty()
                                   ? (store.remoteFolder().empty() ? tr(STR_DROPBOX_APP_FOLDER) : store.remoteFolder())
                                   : currentFolderName_;
    const std::string safeFolder = renderer.truncatedText(UI_10_FONT_ID, folder.c_str(), body.width - 40);
    renderer.drawCenteredText(UI_10_FONT_ID, body.y, safeFolder.c_str());
    int listTop = body.y + renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;
    if (!folderHistory_.empty()) {
      const auto hint = renderer.truncatedText(UI_HINT_FONT_ID, tr(STR_DROPBOX_HOME_HINT), body.width - 40);
      const int hintX = body.x + (body.width - renderer.getTextWidth(UI_HINT_FONT_ID, hint.c_str())) / 2;
      renderer.drawText(UI_HINT_FONT_ID, hintX, listTop, hint.c_str());
      listTop += renderer.getLineHeight(UI_HINT_FONT_ID) + metrics.verticalSpacing;
    }
    if (page_.books.empty()) {
      renderer.drawCenteredText(UI_10_FONT_ID, listTop, tr(STR_DROPBOX_NO_BOOKS));
      listTop += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;
    }
    drawEntries(Rect{body.x, listTop, body.width, std::max(0, body.y + body.height - listTop)});
    confirm = selectedIndex_ < static_cast<int>(page_.books.size())
                  ? (page_.books[selectedIndex_].isFolder ? tr(STR_OPEN) : tr(STR_DOWNLOAD))
                  : (page_.hasMore ? tr(STR_SELECT) : "");
    previous = tr(STR_DIR_UP);
    next = tr(STR_DIR_DOWN);
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirm, previous, next);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
