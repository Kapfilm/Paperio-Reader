#include "HangmanWordEntryActivity.h"

#include <I18n.h>

#include <algorithm>
#include <memory>

#include "HangmanGameActivity.h"
#include "HangmanMenuActivity.h"
#include "activities/games/GameUi.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

size_t utf8CharBytes(const uint8_t lead) {
  if ((lead & 0x80) == 0) return 1;
  if ((lead & 0xE0) == 0xC0) return 2;
  if ((lead & 0xF0) == 0xE0) return 3;
  if ((lead & 0xF8) == 0xF0) return 4;
  return 1;
}

}  // namespace

void HangmanWordEntryActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  state = State::Entering;
  word.clear();
  selected = 0;
  requestUpdate();
}

int HangmanWordEntryActivity::itemCount() const { return static_cast<int>(hangmanAlphabetSize(language)) + 2; }

void HangmanWordEntryActivity::loop() {
  if (state == State::Ready) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      activityManager.replaceActivity(
          std::make_unique<HangmanGameActivity>(renderer, mappedInput, language, std::move(word)));
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = State::Entering;
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    moveSelection(0, -1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    moveSelection(0, 1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    moveSelection(-1, 0);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    moveSelection(1, 0);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.replaceActivity(std::make_unique<HangmanMenuActivity>(renderer, mappedInput));
  }
}

void HangmanWordEntryActivity::moveSelection(const int dx, const int dy) {
  const int count = itemCount();
  const int rows = (count + kColumns - 1) / kColumns;
  if (dx != 0) {
    selected = (selected + dx + count) % count;
  } else {
    const int column = selected % kColumns;
    int row = selected / kColumns;
    row = (row + dy + rows) % rows;
    selected = std::min(row * kColumns + column, count - 1);
  }
  requestUpdate();
}

void HangmanWordEntryActivity::activateSelection() {
  const int alphabetSize = static_cast<int>(hangmanAlphabetSize(language));
  if (selected < alphabetSize) {
    if (hangmanUtf8Length(word.c_str()) < kMaxWordLength) {
      word += hangmanAlphabetLetter(language, static_cast<size_t>(selected));
      requestUpdate();
    }
    return;
  }
  if (selected == alphabetSize) {
    eraseLastLetter();
    requestUpdate();
    return;
  }
  if (hangmanUtf8Length(word.c_str()) >= kMinWordLength) {
    state = State::Ready;
    requestUpdate();
  }
}

void HangmanWordEntryActivity::eraseLastLetter() {
  if (word.empty()) return;
  size_t offset = word.size() - 1;
  while (offset > 0 && (static_cast<uint8_t>(word[offset]) & 0xC0) == 0x80) --offset;
  word.erase(offset, utf8CharBytes(static_cast<uint8_t>(word[offset])));
}

void HangmanWordEntryActivity::render(RenderLock&&) {
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.clearScreen();
  if (state == State::Ready) {
    renderReady();
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  } else {
    renderEntry();
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
}

void HangmanWordEntryActivity::renderEntry() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_HANGMAN_ENTER_WORD));

  const int wordY = metrics.topPadding + metrics.headerHeight + 38;
  const char* shownWord = word.empty() ? tr(STR_HANGMAN_WORD_PLACEHOLDER) : word.c_str();
  renderer.drawCenteredText(NOTOSANS_14_FONT_ID, wordY, shownWord, true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, wordY + 48, tr(STR_HANGMAN_WORD_LENGTH_HINT));

  const int alphabetSize = static_cast<int>(hangmanAlphabetSize(language));
  const int count = itemCount();
  constexpr int side = 24;
  constexpr int gap = 5;
  constexpr int cellH = 48;
  const int cellW = (width - side * 2 - gap * (kColumns - 1)) / kColumns;
  const int rows = (count + kColumns - 1) / kColumns;
  const int gridH = rows * cellH + (rows - 1) * gap;
  const int gridY = wordY + 104 + std::max(0, (height - metrics.buttonHintsHeight - (wordY + 104) - gridH) / 2);
  const int textH = renderer.getTextHeight(UI_12_FONT_ID);

  for (int i = 0; i < count; ++i) {
    const int row = i / kColumns;
    const int column = i % kColumns;
    const int x = side + column * (cellW + gap);
    const int y = gridY + row * (cellH + gap);
    const bool active = i == selected;
    if (active) {
      renderer.fillRect(x, y, cellW, cellH, true);
    } else {
      renderer.drawRect(x, y, cellW, cellH, 1, true);
    }
    std::string label;
    if (i < alphabetSize) {
      label = hangmanAlphabetLetter(language, static_cast<size_t>(i));
    } else if (i == alphabetSize) {
      label = "X";
    } else {
      label = "OK";
    }
    const int textW = renderer.getTextWidth(UI_12_FONT_ID, label.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, x + (cellW - textW) / 2, y + gameCenterY(cellH, textH), label.c_str(), !active,
                      EpdFontFamily::BOLD);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void HangmanWordEntryActivity::renderReady() {
  const int height = renderer.getScreenHeight();
  const int titleH = renderer.getTextHeight(NOTOSANS_16_FONT_ID);
  const int bodyH = renderer.getTextHeight(UI_12_FONT_ID);
  const int top = gameCenteredBlockY(0, height, titleH + bodyH * 2 + 44);
  renderer.drawCenteredText(NOTOSANS_16_FONT_ID, top, tr(STR_HANGMAN_PASS_DEVICE), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_12_FONT_ID, top + titleH + 26, tr(STR_HANGMAN_WORD_HIDDEN));
  renderer.drawCenteredText(UI_12_FONT_ID, top + titleH + bodyH + 42, tr(STR_HANGMAN_PRESS_START));
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_HANGMAN_START), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
