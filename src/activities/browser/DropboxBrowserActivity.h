#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "../Activity.h"
#include "network/DropboxClient.h"
#include "util/ButtonNavigator.h"

struct Rect;

// Read-only Dropbox browser rooted at the configured folder. Account setup lives in the reader's
// local web interface; this screen only lists and downloads supported books.
class DropboxBrowserActivity final : public Activity {
 public:
  explicit DropboxBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("DropboxBrowser", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  bool shouldSkipPeriodicUpdate() const override { return true; }

 private:
  enum class State { Setup, Loading, WifiSelection, Browsing, Downloading, Message, Error };
  enum class PendingAction { None, Fetch, Download };

  ButtonNavigator navigator_;
  DropboxClient client_;
  DropboxClient::Page page_;
  State state_ = State::Setup;
  PendingAction pending_ = PendingAction::None;
  std::string pendingCursor_;
  std::string currentCursor_;
  struct FolderFrame {
    std::string id;
    std::string name;
    std::string cursor;
    int selection = 0;
  };
  // No frame exists above the configured root. Store only metadata, not entire
  // parent pages; the page is fetched again when returning up one level.
  std::vector<FolderFrame> folderHistory_;
  std::string currentFolderId_;
  std::string currentFolderName_;
  int restoreSelection_ = 0;
  std::string status_;
  std::string detail_;
  DropboxClient::Error lastError_ = DropboxClient::Error::Ok;
  size_t downloaded_ = 0;
  size_t total_ = 0;
  int selectedIndex_ = 0;
  int pendingBook_ = -1;
  bool havePage_ = false;
  bool memoryTrimmed_ = false;
  bool handingOffToWeb_ = false;
  bool ignoreBackUntilRelease_ = false;

  int rowCount() const;
  int nextRow() const;
  void selectRow();
  void queueFetch(const std::string& cursor, int restoreSelection = 0);
  void openFolder(const DropboxClient::Entry& folder);
  void goToParentFolder();
  void executePending();
  void launchWifiSelection();
  void trimNetworkMemory();
  void fetchPage();
  void downloadBook();
  void openWebSetup();
  void returnToBooks();
  void showError(DropboxClient::Error error);
  const char* errorText() const;
  void drawEntries(const Rect& body);
  void drawMessage(const Rect& content, const char* text, const std::string& detail = {}) const;
};
