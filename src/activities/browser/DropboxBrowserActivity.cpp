#include "DropboxBrowserActivity.h"

#include <HalClock.h>
#include <I18n.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "CrossPointSettings.h"
#include "DropboxStore.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/NetworkMemoryTrim.h"
#include "activities/network/CrossPointWebServerActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
std::string fileSizeLabel(const uint64_t bytes) {
  char buffer[40];
  if (bytes >= 1024 * 1024) {
    snprintf(buffer, sizeof(buffer), "%.1f MiB", static_cast<double>(bytes) / (1024 * 1024));
  } else {
    snprintf(buffer, sizeof(buffer), "%.1f KiB", static_cast<double>(bytes) / 1024);
  }
  return buffer;
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
  // restarts when it exits. Do not reboot halfway through that handoff.
  if (handingOffToWeb_ || !memoryTrimmed_) return;
  if (WiFi.getMode() != WIFI_MODE_NULL) HalClock::wifiOff(false, SETTINGS.ntpServer);
  // Network memory trimming is intentionally paired with a restart, even if
  // association was cancelled or failed before the radio became connected.
  silentRestart();
}

int DropboxBrowserActivity::nextRow() const { return page_.hasMore ? static_cast<int>(page_.books.size()) : -1; }

int DropboxBrowserActivity::refreshRow() const {
  return static_cast<int>(page_.books.size()) + (page_.hasMore ? 1 : 0);
}

int DropboxBrowserActivity::rowCount() const { return refreshRow() + 2; }

void DropboxBrowserActivity::queueFetch(const std::string& cursor) {
  {
    RenderLock lock(*this);
    pendingCursor_ = cursor;
    pending_ = PendingAction::Fetch;
    state_ = State::Loading;
  }
  requestUpdate();
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
  trimNetworkMemory();  // Before the radio starts, and again before every TLS operation.
  if (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
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
  const auto result = client_.list(resultPage, pendingCursor_);
  if (!result.ok()) {
    showError(result.error);
    return;
  }
  {
    RenderLock lock(*this);
    page_ = std::move(resultPage);
    currentCursor_ = pendingCursor_;
    havePage_ = true;
    selectedIndex_ = 0;
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
    pendingBook_ = selectedIndex_;
    pendingCursor_ = currentCursor_;
    pending_ = PendingAction::Download;
    {
      RenderLock lock(*this);
      state_ = State::Loading;
    }
    requestUpdate();
  } else if (selectedIndex_ == nextRow()) {
    queueFetch(page_.cursor);
  } else if (selectedIndex_ == refreshRow()) {
    queueFetch("");
  } else {
    openWebSetup();
  }
}

void DropboxBrowserActivity::loop() {
  if (ignoreBackUntilRelease_) {
    if (mappedInput.isPressed(MappedInputManager::Button::Back)) return;
    ignoreBackUntilRelease_ = false;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if ((state_ == State::Message || state_ == State::Error) && havePage_)
      returnToBooks();
    else
      finish();
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
      queueFetch(pendingCursor_);
    return;
  }
  if (state_ == State::Error && mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    openWebSetup();
    return;
  }
  if (state_ == State::Browsing) {
    navigator_.onNextList(selectedIndex_, rowCount(), [this] { requestUpdate(); });
    navigator_.onPreviousList(selectedIndex_, rowCount(), [this] { requestUpdate(); });
  }
}

void DropboxBrowserActivity::drawMessage(const Rect& content, const char* text, const std::string& detail) const {
  const int width = std::max(40, content.width - 40);
  const auto lines = renderer.wrappedText(UI_10_FONT_ID, text, width, 10);
  const auto detailLines = renderer.wrappedText(UI_10_FONT_ID, detail.c_str(), width, 4);
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int blockHeight = static_cast<int>(lines.size() + detailLines.size()) * lineHeight + (detail.empty() ? 0 : 16);
  int y = std::max(content.y, content.y + (content.height - blockHeight) / 2);
  for (const auto& line : lines) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, line.c_str());
    y += lineHeight;
  }
  if (!detail.empty()) y += 16;
  for (const auto& line : detailLines) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, line.c_str());
    y += lineHeight;
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
    previous = tr(STR_DROPBOX_SETUP);
  } else if (state_ == State::Message) {
    drawMessage(body, status_.c_str(), detail_);
    confirm = tr(STR_DONE);
  } else if (state_ == State::Loading || state_ == State::WifiSelection) {
    drawMessage(body, tr(STR_LOADING));
    confirm = "";
  } else if (state_ == State::Downloading) {
    drawMessage(body, tr(STR_DOWNLOADING), status_);
    if (total_ > 0) {
      const int barWidth = std::max(20, body.width - 80);
      GUI.drawProgressBar(renderer, Rect{body.x + 40, body.y + body.height * 3 / 4, barWidth, 16}, downloaded_, total_);
    }
    confirm = "";
  } else {
    auto& store = DropboxStore::instance();
    const std::string folder = store.remoteFolder().empty() ? tr(STR_DROPBOX_APP_FOLDER) : store.remoteFolder();
    const std::string safeFolder = renderer.truncatedText(UI_10_FONT_ID, folder.c_str(), body.width - 40);
    renderer.drawCenteredText(UI_10_FONT_ID, body.y, safeFolder.c_str());
    int listTop = body.y + renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;
    if (page_.books.empty()) {
      renderer.drawCenteredText(UI_10_FONT_ID, listTop, tr(STR_DROPBOX_NO_BOOKS));
      listTop += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;
    }
    GUI.drawList(
        renderer, Rect{body.x, listTop, body.width, std::max(20, body.y + body.height - listTop)}, rowCount(),
        selectedIndex_,
        [this](int index) -> std::string {
          if (index < static_cast<int>(page_.books.size())) return page_.books[index].name;
          if (index == nextRow()) return tr(STR_NEXT_PAGE);
          if (index == refreshRow()) return tr(STR_DROPBOX_REFRESH);
          return tr(STR_DROPBOX_SETUP);
        },
        [this](int index) -> std::string {
          return index < static_cast<int>(page_.books.size()) ? fileSizeLabel(page_.books[index].size) : "";
        });
    confirm = selectedIndex_ < static_cast<int>(page_.books.size()) ? tr(STR_DOWNLOAD) : tr(STR_SELECT);
    previous = tr(STR_DIR_UP);
    next = tr(STR_DIR_DOWN);
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirm, previous, next);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
