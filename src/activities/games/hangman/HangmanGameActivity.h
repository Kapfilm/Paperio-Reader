#pragma once

#include <cstdint>
#include <string>

#include "HangmanStore.h"
#include "activities/Activity.h"
#include "activities/games/GameSaveDebouncer.h"

class HangmanGameActivity final : public Activity {
 public:
  HangmanGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, HangmanLanguage language,
                      HangmanCategory category, HangmanDifficulty difficulty);
  HangmanGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, HangmanLanguage language,
                      std::string customWord);
  HangmanGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool resume);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool shouldSkipPeriodicUpdate() const override { return true; }

 private:
  enum class State : uint8_t { Playing, Won, Lost };

  State state = State::Playing;
  HangmanLanguage language = HangmanLanguage::English;
  HangmanCategory category = HangmanCategory::Animals;
  HangmanDifficulty difficulty = HangmanDifficulty::Easy;
  std::string customWord;
  uint8_t wordIndex = 0;
  uint8_t misses = 0;
  uint8_t cursor = 0;
  uint64_t guessedMask = 0;
  bool hintUsed = false;
  bool resumeRequested = false;
  bool multiplayer = false;
  GameSaveDebouncer saveDebouncer;

  void startNewGame();
  bool resumeGame();
  void moveCursor(int dx, int dy);
  void activateCursor();
  void guessLetter(size_t alphabetIndex);
  void useHint();
  bool wordContains(size_t alphabetIndex) const;
  bool isSolved() const;
  int alphabetIndexFor(uint32_t codepoint) const;
  void finishGame(State result);
  void scheduleSave();
  void flushSave();

  void renderPlaying();
  void renderResult();
  void drawGallows(int x, int y, int width, int height) const;
  void drawWord(int y, bool revealAll) const;
  void drawAlphabet(int top);
  const char* categoryName() const;
  const char* difficultyName() const;
  uint8_t maxMisses() const;
  bool hintAvailable() const;
  const char* currentWord() const;
};
