#pragma once

#include <Epub/Section.h>

#include <vector>

#include "WordRef.h"
#include "activities/Activity.h"

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

  bool renderBasePage(int relativePage);
  void moveCursor(int next);
  void moveCursorByLine(int direction);
  void confirmSelection();
  void cancelSelection();
  void drawWord(const WordRef& word, bool cursorStyle) const;
};
