#include "HangmanGameActivity.h"

#include <Arduino.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <memory>

#include "HangmanMenuActivity.h"
#include "HangmanWordEntryActivity.h"
#include "activities/games/GameUi.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr uint8_t kEasyMaxMisses = 6;
constexpr uint8_t kHardMaxMisses = 5;
constexpr int kAlphabetColumns = 7;
constexpr int kAlphabetCellHeight = 42;

size_t utf8CharBytes(const uint8_t lead) {
  if ((lead & 0x80) == 0) return 1;
  if ((lead & 0xE0) == 0xC0) return 2;
  if ((lead & 0xF0) == 0xE0) return 3;
  if ((lead & 0xF8) == 0xF0) return 4;
  return 1;
}

void drawWrappedText(GfxRenderer& renderer, const int font, const int x, const int y, const int width,
                     const int maxLines, const char* text) {
  if (text == nullptr || width <= 0 || maxLines <= 0) return;
  const int lineHeight = renderer.getTextHeight(font) + 4;
  std::string line;
  const char* cursor = text;
  int row = 0;
  while (*cursor != 0 && row < maxLines) {
    while (*cursor == ' ') ++cursor;
    const char* end = cursor;
    while (*end != 0 && *end != ' ') ++end;
    std::string token(cursor, static_cast<size_t>(end - cursor));
    std::string candidate = line.empty() ? token : line + " " + token;
    if (!line.empty() && renderer.getTextWidth(font, candidate.c_str()) > width) {
      renderer.drawText(font, x, y + row * lineHeight, line.c_str());
      ++row;
      line = token;
    } else {
      line = candidate;
    }
    cursor = end;
  }
  if (!line.empty() && row < maxLines) renderer.drawText(font, x, y + row * lineHeight, line.c_str());
}

}  // namespace

HangmanGameActivity::HangmanGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                         const HangmanLanguage language, const HangmanCategory category,
                                         const HangmanDifficulty difficulty)
    : Activity("Hangman", renderer, mappedInput), language(language), category(category), difficulty(difficulty) {}

HangmanGameActivity::HangmanGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                         const HangmanLanguage language, std::string customWord)
    : Activity("Hangman", renderer, mappedInput),
      language(language),
      customWord(std::move(customWord)),
      multiplayer(true) {}

HangmanGameActivity::HangmanGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const bool resume)
    : Activity("Hangman", renderer, mappedInput), resumeRequested(resume) {}

void HangmanGameActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  state = State::Playing;
  saveDebouncer.clear();
  if (!resumeRequested || !resumeGame()) startNewGame();
  requestUpdate();
}

void HangmanGameActivity::onExit() {
  flushSave();
  Activity::onExit();
}

void HangmanGameActivity::startNewGame() {
  if (!multiplayer) {
    const size_t count = hangmanWordCount(language, category, difficulty);
    wordIndex = count == 0 ? 0 : static_cast<uint8_t>(esp_random() % count);
  }
  misses = 0;
  cursor = 0;
  guessedMask = 0;
  hintUsed = false;
  state = State::Playing;
  if (!multiplayer) {
    HangmanStore::savePreferences({language, category, difficulty});
    scheduleSave();
  }
}

bool HangmanGameActivity::resumeGame() {
  HangmanSaveSlot slot;
  if (!HangmanStore::load(slot)) return false;
  language = slot.language;
  category = slot.category;
  difficulty = slot.difficulty;
  wordIndex = slot.wordIndex;
  misses = slot.misses;
  cursor = slot.cursor;
  hintUsed = slot.hintUsed;
  guessedMask = slot.guessedMask;
  return true;
}

void HangmanGameActivity::loop() {
  if (state != State::Playing) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (multiplayer) {
        activityManager.replaceActivity(std::make_unique<HangmanWordEntryActivity>(renderer, mappedInput, language));
      } else {
        activityManager.replaceActivity(
            std::make_unique<HangmanGameActivity>(renderer, mappedInput, language, category, difficulty));
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      activityManager.replaceActivity(std::make_unique<HangmanMenuActivity>(renderer, mappedInput));
    }
    return;
  }

  if (saveDebouncer.consumeIfDue(millis())) flushSave();
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    moveCursor(0, -1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    moveCursor(0, 1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    moveCursor(-1, 0);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    moveCursor(1, 0);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateCursor();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    flushSave();
    activityManager.replaceActivity(std::make_unique<HangmanMenuActivity>(renderer, mappedInput));
  }
}

void HangmanGameActivity::moveCursor(const int dx, const int dy) {
  const int itemCount = static_cast<int>(hangmanAlphabetSize(language) + (hintAvailable() ? 1 : 0));
  const int rows = (itemCount + kAlphabetColumns - 1) / kAlphabetColumns;
  int row = cursor / kAlphabetColumns;
  int column = cursor % kAlphabetColumns;
  if (dx != 0) {
    cursor = static_cast<uint8_t>((static_cast<int>(cursor) + dx + itemCount) % itemCount);
  } else {
    row = (row + dy + rows) % rows;
    int next = row * kAlphabetColumns + column;
    if (next >= itemCount) next = itemCount - 1;
    cursor = static_cast<uint8_t>(next);
  }
  scheduleSave();
  requestUpdate();
}

void HangmanGameActivity::activateCursor() {
  const size_t alphabetSize = hangmanAlphabetSize(language);
  if (hintAvailable() && cursor == alphabetSize) {
    useHint();
  } else {
    guessLetter(cursor);
  }
}

void HangmanGameActivity::guessLetter(const size_t alphabetIndex) {
  if (alphabetIndex >= hangmanAlphabetSize(language) || alphabetIndex >= 64) return;
  const uint64_t bit = uint64_t{1} << alphabetIndex;
  if ((guessedMask & bit) != 0) return;
  guessedMask |= bit;
  if (!wordContains(alphabetIndex) && misses < maxMisses()) ++misses;
  if (isSolved()) {
    finishGame(State::Won);
  } else if (misses >= maxMisses()) {
    finishGame(State::Lost);
  } else {
    scheduleSave();
  }
  requestUpdate();
}

void HangmanGameActivity::useHint() {
  if (hintUsed || !hintAvailable()) return;
  const size_t alphabetSize = hangmanAlphabetSize(language);
  uint8_t candidates[33] = {};
  size_t count = 0;
  for (size_t i = 0; i < alphabetSize && i < std::size(candidates); ++i) {
    if ((guessedMask & (uint64_t{1} << i)) == 0 && wordContains(i)) candidates[count++] = static_cast<uint8_t>(i);
  }
  hintUsed = true;
  if (count > 0) guessedMask |= uint64_t{1} << candidates[esp_random() % count];
  if (isSolved()) {
    finishGame(State::Won);
  } else {
    scheduleSave();
  }
  requestUpdate();
}

bool HangmanGameActivity::wordContains(const size_t alphabetIndex) const {
  const std::string letter = hangmanAlphabetLetter(language, alphabetIndex);
  const uint32_t wanted = hangmanUtf8Codepoint(letter.c_str());
  const auto* wordCursor = reinterpret_cast<const uint8_t*>(currentWord());
  while (*wordCursor != 0) {
    if (hangmanUtf8Codepoint(reinterpret_cast<const char*>(wordCursor)) == wanted) return true;
    wordCursor += utf8CharBytes(*wordCursor);
  }
  return false;
}

int HangmanGameActivity::alphabetIndexFor(const uint32_t codepoint) const {
  const size_t count = hangmanAlphabetSize(language);
  for (size_t i = 0; i < count; ++i) {
    const std::string letter = hangmanAlphabetLetter(language, i);
    if (hangmanUtf8Codepoint(letter.c_str()) == codepoint) return static_cast<int>(i);
  }
  return -1;
}

bool HangmanGameActivity::isSolved() const {
  const auto* wordCursor = reinterpret_cast<const uint8_t*>(currentWord());
  while (*wordCursor != 0) {
    const int index = alphabetIndexFor(hangmanUtf8Codepoint(reinterpret_cast<const char*>(wordCursor)));
    if (index >= 0 && (guessedMask & (uint64_t{1} << index)) == 0) return false;
    wordCursor += utf8CharBytes(*wordCursor);
  }
  return true;
}

void HangmanGameActivity::finishGame(const State result) {
  state = result;
  saveDebouncer.clear();
  if (!multiplayer) {
    HangmanStore::clear();
    if (result == State::Won) {
      HangmanStore::recordWin();
    } else {
      HangmanStore::recordLoss();
    }
  }
}

void HangmanGameActivity::scheduleSave() { saveDebouncer.schedule(millis()); }

void HangmanGameActivity::flushSave() {
  if (state != State::Playing || multiplayer) return;
  saveDebouncer.clear();
  HangmanSaveSlot slot;
  slot.language = language;
  slot.category = category;
  slot.difficulty = difficulty;
  slot.wordIndex = wordIndex;
  slot.misses = misses;
  slot.cursor = cursor;
  slot.hintUsed = hintUsed;
  slot.guessedMask = guessedMask;
  if (!HangmanStore::save(slot)) LOG_ERR("HNG", "Failed to save game");
}

const char* HangmanGameActivity::categoryName() const {
  switch (category) {
    case HangmanCategory::Nature:
      return tr(STR_HANGMAN_NATURE);
    case HangmanCategory::Objects:
      return tr(STR_HANGMAN_OBJECTS);
    case HangmanCategory::Animals:
    default:
      return tr(STR_HANGMAN_ANIMALS);
  }
}

const char* HangmanGameActivity::difficultyName() const {
  return difficulty == HangmanDifficulty::Hard ? tr(STR_HANGMAN_HARD) : tr(STR_HANGMAN_EASY);
}

uint8_t HangmanGameActivity::maxMisses() const {
  return difficulty == HangmanDifficulty::Hard ? kHardMaxMisses : kEasyMaxMisses;
}

bool HangmanGameActivity::hintAvailable() const { return !multiplayer && difficulty == HangmanDifficulty::Easy; }

const char* HangmanGameActivity::currentWord() const {
  if (multiplayer) return customWord.c_str();
  return hangmanWordAt(language, category, difficulty, wordIndex).word;
}

void HangmanGameActivity::render(RenderLock&&) {
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.clearScreen();
  if (state == State::Playing) {
    renderPlaying();
  } else {
    renderResult();
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void HangmanGameActivity::renderPlaying() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_HANGMAN_TITLE));

  char status[96];
  if (multiplayer) {
    snprintf(status, sizeof(status), "%s · %s: %u/%u", tr(STR_HANGMAN_TWO_PLAYER), tr(STR_HANGMAN_MISSES),
             static_cast<unsigned>(misses), static_cast<unsigned>(maxMisses()));
  } else {
    snprintf(status, sizeof(status), "%s · %s · %s: %u/%u", categoryName(), difficultyName(),
             tr(STR_HANGMAN_MISSES), static_cast<unsigned>(misses), static_cast<unsigned>(maxMisses()));
  }
  const int subY = metrics.topPadding + metrics.headerHeight;
  GUI.drawSubHeader(renderer, Rect{0, subY, width, metrics.tabBarHeight}, status);

  const int figureTop = subY + metrics.tabBarHeight + 12;
  drawGallows(28, figureTop, 168, 205);

  const int hintX = 216;
  const int hintWidth = width - hintX - 18;
  renderer.drawText(UI_10_FONT_ID, hintX, figureTop + 32, tr(STR_HANGMAN_HINT), true, EpdFontFamily::BOLD);
  if (multiplayer) {
    drawWrappedText(renderer, UI_10_FONT_ID, hintX, figureTop + 66, hintWidth, 3,
                    tr(STR_HANGMAN_NO_HINT_TWO_PLAYER));
  } else {
    const HangmanWord& word = hangmanWordAt(language, category, difficulty, wordIndex);
    drawWrappedText(renderer, UI_10_FONT_ID, hintX, figureTop + 66, hintWidth, 3, word.hint);
    if (hintUsed) renderer.drawText(UI_10_FONT_ID, hintX, figureTop + 132, tr(STR_HANGMAN_HINT_USED));
  }
  renderer.drawLine(hintX, figureTop + 58, hintX + hintWidth, figureTop + 58, true);

  drawWord(figureTop + 224, false);
  drawAlphabet(figureTop + 282);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void HangmanGameActivity::drawGallows(const int x, const int y, const int width, const int height) const {
  const int baseY = y + height;
  const int poleX = x + 30;
  const int beamY = y + 8;
  const int ropeX = x + width - 36;
  renderer.drawLine(x, baseY, x + width, baseY, 3, true);
  renderer.drawLine(poleX, baseY, poleX, beamY, 3, true);
  renderer.drawLine(poleX, beamY, ropeX, beamY, 3, true);
  renderer.drawLine(poleX, y + 42, poleX + 34, beamY, 2, true);
  renderer.drawLine(ropeX, beamY, ropeX, y + 43, 2, true);

  const int cx = ropeX;
  const int headTop = y + 43;
  if (misses >= 1) {
    renderer.drawLine(cx - 13, headTop + 4, cx - 6, headTop - 3, 2, true);
    renderer.drawLine(cx - 6, headTop - 3, cx + 6, headTop - 3, 2, true);
    renderer.drawLine(cx + 6, headTop - 3, cx + 13, headTop + 4, 2, true);
    renderer.drawLine(cx + 13, headTop + 4, cx + 13, headTop + 17, 2, true);
    renderer.drawLine(cx + 13, headTop + 17, cx + 6, headTop + 24, 2, true);
    renderer.drawLine(cx + 6, headTop + 24, cx - 6, headTop + 24, 2, true);
    renderer.drawLine(cx - 6, headTop + 24, cx - 13, headTop + 17, 2, true);
    renderer.drawLine(cx - 13, headTop + 17, cx - 13, headTop + 4, 2, true);
  }
  const int neckY = headTop + 25;
  const int hipY = neckY + 58;
  if (misses >= 2) renderer.drawLine(cx, neckY, cx, hipY, 3, true);
  if (misses >= 3) renderer.drawLine(cx, neckY + 12, cx - 26, neckY + 39, 3, true);
  if (misses >= 4) renderer.drawLine(cx, neckY + 12, cx + 26, neckY + 39, 3, true);
  if (misses >= 5) renderer.drawLine(cx, hipY, cx - 24, hipY + 38, 3, true);
  if (misses >= 6 || (difficulty == HangmanDifficulty::Hard && misses >= 5)) {
    renderer.drawLine(cx, hipY, cx + 24, hipY + 38, 3, true);
  }
}

void HangmanGameActivity::drawWord(const int y, const bool revealAll) const {
  std::string wordDisplay;
  const auto* wordCursor = reinterpret_cast<const uint8_t*>(currentWord());
  while (*wordCursor != 0) {
    const size_t bytes = utf8CharBytes(*wordCursor);
    const int index = alphabetIndexFor(hangmanUtf8Codepoint(reinterpret_cast<const char*>(wordCursor)));
    if (revealAll || (index >= 0 && (guessedMask & (uint64_t{1} << index)) != 0)) {
      wordDisplay.append(reinterpret_cast<const char*>(wordCursor), bytes);
    } else {
      wordDisplay += "_";
    }
    wordDisplay += " ";
    wordCursor += bytes;
  }
  renderer.drawCenteredText(NOTOSANS_14_FONT_ID, y, wordDisplay.c_str(), true, EpdFontFamily::BOLD);
}

void HangmanGameActivity::drawAlphabet(const int top) {
  const int width = renderer.getScreenWidth();
  constexpr int side = 24;
  constexpr int gap = 5;
  const int cellWidth = (width - side * 2 - gap * (kAlphabetColumns - 1)) / kAlphabetColumns;
  const size_t alphabetSize = hangmanAlphabetSize(language);
  const size_t itemCount = alphabetSize + (hintAvailable() ? 1 : 0);
  const int textHeight = renderer.getTextHeight(UI_12_FONT_ID);
  for (size_t i = 0; i < itemCount; ++i) {
    const int row = static_cast<int>(i) / kAlphabetColumns;
    const int column = static_cast<int>(i) % kAlphabetColumns;
    const int x = side + column * (cellWidth + gap);
    const int y = top + row * (kAlphabetCellHeight + gap);
    const bool selected = i == cursor;
    const bool guessed = i < alphabetSize && (guessedMask & (uint64_t{1} << i)) != 0;
    const bool disabledHint = i == alphabetSize && hintUsed;
    if (selected) {
      renderer.fillRect(x, y, cellWidth, kAlphabetCellHeight, true);
    } else if (guessed || disabledHint) {
      renderer.fillRectDither(x, y, cellWidth, kAlphabetCellHeight, Color::LightGray);
      renderer.drawRect(x, y, cellWidth, kAlphabetCellHeight, 1, true);
    } else {
      renderer.drawRect(x, y, cellWidth, kAlphabetCellHeight, 1, true);
    }
    const std::string label = i == alphabetSize ? "?" : hangmanAlphabetLetter(language, i);
    const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, label.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, x + (cellWidth - textWidth) / 2, y + gameCenterY(kAlphabetCellHeight, textHeight),
                      label.c_str(), !selected, EpdFontFamily::BOLD);
  }
}

void HangmanGameActivity::renderResult() {
  const int height = renderer.getScreenHeight();
  const int titleHeight = renderer.getTextHeight(NOTOSANS_16_FONT_ID);
  const int bodyHeight = renderer.getTextHeight(UI_12_FONT_ID);
  const int top = gameCenteredBlockY(0, height, titleHeight + bodyHeight * 3 + 54);
  renderer.drawCenteredText(NOTOSANS_16_FONT_ID, top, state == State::Won ? tr(STR_HANGMAN_WON) : tr(STR_HANGMAN_LOST),
                            true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_12_FONT_ID, top + titleHeight + 18, tr(STR_HANGMAN_WORD_WAS));
  drawWord(top + titleHeight + bodyHeight + 30, true);
  if (!multiplayer) {
    char score[96];
    const HangmanStats stats = HangmanStore::loadStats();
    snprintf(score, sizeof(score), "%s: %lu · %s: %lu", tr(STR_HANGMAN_WINS), static_cast<unsigned long>(stats.wins),
             tr(STR_HANGMAN_LOSSES), static_cast<unsigned long>(stats.losses));
    renderer.drawCenteredText(UI_10_FONT_ID, top + titleHeight + bodyHeight * 2 + 44, score);
  }
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), multiplayer ? tr(STR_HANGMAN_NEW_WORD) : tr(STR_GAME_NEW_GAME), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
