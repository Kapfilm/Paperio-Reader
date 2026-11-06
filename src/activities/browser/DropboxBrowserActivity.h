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
  bool hasFastBackExit() const override { return true; }
  bool onLongBack() override;
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
  // Deep navigation stores only metadata. Recently viewed pages have a separate,
  // small cache so memory usage does not grow with folder depth.
  std::vector<FolderFrame> folderHistory_;
  struct CachedPage {
    std::string folderId;
    std::string cursor;
    DropboxClient::Page page;
    unsigned long fetchedAt;
    size_t bytes;
  };
  std::vector<CachedPage> cachedPages_;
  unsigned long pageFetchedAt_ = 0;
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
  void cacheCurrentPage(const std::string& destinationId, const std::string& destinationCursor);
  bool restoreCachedPage(const std::string& cursor);
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
  void drawMessage(const Rect& content, const char* text, const std::string& detail = {},
                   bool withProgress = false) const;
};
