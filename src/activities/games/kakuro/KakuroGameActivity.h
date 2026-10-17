#pragma once

#include <cstdint>

#include "KakuroBoard.h"
#include "activities/Activity.h"
#include "activities/games/GameSaveDebouncer.h"
#include "util/ButtonNavigator.h"

class KakuroGameActivity final : public Activity {
 public:
  KakuroGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, KakuroBoard::Difficulty difficulty,
                     bool resume = false);
  ~KakuroGameActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  static const char* difficultyName(KakuroBoard::Difficulty difficulty);

 private:
  enum class State : uint8_t { Playing, GameMenu, Won };
  enum class Focus : uint8_t { Grid, Palette };

  State state = State::Playing;
  Focus focus = Focus::Grid;
  KakuroBoard board;
  KakuroBoard::Difficulty difficulty;
  uint8_t variant = 0;
  uint8_t cursorRow = 1;
  uint8_t cursorColumn = 1;
  uint8_t paletteIndex = 0;
  uint8_t lastPlacedDigit = 1;
  uint8_t mistakes = 0;
  uint8_t hintsLeft = 3;
  bool showErrors = false;
  bool resumeRequested = false;
  uint32_t elapsedMs = 0;
  uint32_t lastTickMs = 0;
  GameSaveDebouncer saveDebouncer;
  ButtonNavigator buttonNavigator;
  uint8_t gameMenuIndex = 0;

  static constexpr uint8_t MENU_ITEM_COUNT = 6;
  static constexpr int TITLE_HEIGHT = 52;
  static constexpr int GRID_AREA_BOTTOM = 508;
  static constexpr int PALETTE_Y = 524;
  static constexpr int PALETTE_CELL_WIDTH = 80;
  static constexpr int PALETTE_CELL_HEIGHT = 72;
  static constexpr int PALETTE_GAP = 8;
  static constexpr int PALETTE_WIDTH = PALETTE_CELL_WIDTH * 5 + PALETTE_GAP * 4;
  static constexpr int PALETTE_HEIGHT = PALETTE_CELL_HEIGHT * 2 + PALETTE_GAP;

  void handlePlayingInput();
  void handleGameMenuInput();
  void handleWonInput();
  void moveCursor(int rowDelta, int columnDelta);
  void movePalette(int rowDelta, int columnDelta);
  void enterPalette();
  void leavePalette(bool place);
  void placeDigit(uint8_t digit);
  void useHint();
  void restart();
  void solve();
  void runMenuItem(uint8_t index);
  void resumeFromMenu();
  void scheduleSave();
  void flushSave();

  void renderPlaying();
  void renderGameMenu();
  void renderWon();
  void drawTitleBar();
  void drawGrid();
  void drawClueCell(int x, int y, int size, uint8_t right, uint8_t down);
  void drawPalette();
  void drawFooter();
};
