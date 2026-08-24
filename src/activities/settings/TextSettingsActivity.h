#pragma once

#include <GfxRenderer.h>

#include <cstdint>
#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class MappedInputManager;
struct Rect;

// Unified EPUB text settings with a live preview and four tabs.
class TextSettingsActivity final : public Activity {
 public:
  enum class Tab : uint8_t { Font, Size, Layout, Style, Count };

  TextSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("TextSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  int rowCount() const;
  std::string rowTitle(int row) const;
  std::string rowValue(int row) const;
  void activateRow(int row);
  void switchTab();
  void reloadReaderFont();
  void renderPreview(int top, int height) const;
  void renderPreviewSample(int top, int height, int labelReserved) const;
  void renderTabBar(const Rect& rect) const;
  const char* tabLabel(Tab tab) const;

  ButtonNavigator buttonNavigator;
  Tab tab = Tab::Font;
  int selectedRow = -1;  // -1 focuses the tab bar
  int previewFontId = 0;
  uint8_t fontCount = 0;
};
