#pragma once

#include <Epub/Section.h>
#include <Memory.h>

#include <memory>
#include <vector>

#include "WordRef.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class ClipSelectionActivity final : public Activity {
 public:
  ClipSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::vector<WordRef> words,
                        int fontId, Section& section, int startPage, int marginTop, int marginLeft);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }

 private:
  static constexpr size_t BUFFER_CHUNK_SIZE = 4096;

  std::vector<WordRef> words;
  int fontId = 0;
  Section& section;
  int startPage = 0;
  int marginTop = 0;
  int marginLeft = 0;
  int savedSectionPage = 0;
  int displayedRelativePage = 0;
  int cursor = 0;
  int selectionStart = -1;
  bool pageSwitchPending = false;
  bool hasSnapshot = false;
  size_t snapshotSize = 0;
  std::vector<std::unique_ptr<uint8_t[]>> snapshotChunks;
  ButtonNavigator buttonNavigator;

  bool allocateSnapshot();
  void saveSnapshot();
  void restoreSnapshot() const;
  bool showPage(int relativePage);
  void moveCursor(int next);
  void drawWord(const WordRef& word, bool cursorStyle) const;
};
