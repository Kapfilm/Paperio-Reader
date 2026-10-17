#pragma once

#include <cstdint>
#include <string>

#include "HangmanWords.h"
#include "activities/Activity.h"

class HangmanWordEntryActivity final : public Activity {
 public:
  HangmanWordEntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, HangmanLanguage language)
      : Activity("HangmanWordEntry", renderer, mappedInput), language(language) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool shouldSkipPeriodicUpdate() const override { return true; }

 private:
  enum class State : uint8_t { Entering, Ready };

  static constexpr size_t kMinWordLength = 3;
  static constexpr size_t kMaxWordLength = 14;
  static constexpr int kColumns = 7;

  HangmanLanguage language;
  State state = State::Entering;
  std::string word;
  int selected = 0;

  int itemCount() const;
  void moveSelection(int dx, int dy);
  void activateSelection();
  void eraseLastLetter();
  void renderEntry();
  void renderReady();
};
