#include "KeyboardEntryActivity.h"

#include <I18n.h>

#include <vector>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void KeyboardEntryActivity::onEnter() {
  Activity::onEnter();
  cursorPos = text.length();
  requestUpdate();
}

void KeyboardEntryActivity::onExit() { Activity::onExit(); }

int KeyboardEntryActivity::getContentRowCount() const { return ABC_ROWS; }

int KeyboardEntryActivity::getTotalRowCount() const { return getContentRowCount() + 1; }

int KeyboardEntryActivity::getBottomKeyCount() const { return isPassword ? 6 : 5; }

bool KeyboardEntryActivity::isBottomRow(const int row) const { return row == getContentRowCount(); }

const char* KeyboardEntryActivity::getShiftLabel() const {
  const StrId labelId = shiftState > 0 ? StrId::STR_SHIFT_CAPS : StrId::STR_SHIFT;
  return I18n::getInstance().get(labelId);
}

char KeyboardEntryActivity::getSelectedChar() const {
  const KeyDef(*layout)[COLS] = symMode ? symLayout : abcLayout;

  if (selectedRow < 0 || selectedRow >= getContentRowCount()) return '\0';
  if (selectedCol < 0 || selectedCol >= COLS) return '\0';

  const KeyDef& key = layout[selectedRow][selectedCol];
  const bool useSecondary = !symMode && shiftState > 0 && key.secondary != '\0';
  return useSecondary ? key.secondary : key.primary;
}

char KeyboardEntryActivity::getAlternativeChar() const {
  if (symMode) return '\0';

  const KeyDef(*layout)[COLS] = abcLayout;

  if (selectedRow < 0 || selectedRow >= getContentRowCount()) return '\0';
  if (selectedCol < 0 || selectedCol >= COLS) return '\0';

  const KeyDef& key = layout[selectedRow][selectedCol];
  const char current = getSelectedChar();
  if (current == key.primary && key.secondary != '\0') return key.secondary;
  if (current == key.secondary) return key.primary;
  return '\0';
}

bool KeyboardEntryActivity::insertChar(char c) {
  if (c == '\0') return true;
  if (maxLength != 0 && text.length() >= maxLength) return true;

  text.insert(cursorPos, 1, c);
  cursorPos++;
  return true;
}

SpecialKeyType KeyboardEntryActivity::getBottomSpecialKey(int index) const {
  if (isPassword) {
    switch (index) {
      case 0:
        return SpecShift;
      case 1:
        return SpecMode;
      case 2:
        return SpecReveal;
      case 3:
        return SpecSpace;
      case 4:
        return SpecDel;
      case 5:
      default:
        return SpecOk;
    }
  }

  switch (index) {
    case 0:
      return SpecShift;
    case 1:
      return SpecMode;
    case 2:
      return SpecSpace;
    case 3:
      return SpecDel;
    case 4:
    default:
      return SpecOk;
  }
}

bool KeyboardEntryActivity::handleKeyPress() {
  if (isBottomRow(selectedRow)) {
    switch (getBottomSpecialKey(selectedCol)) {
      case SpecShift:
        if (symMode) return true;
        shiftState = (shiftState + 1) % 2;
        return true;
      case SpecMode: {
        symMode = !symMode;
        int maxRow = getTotalRowCount() - 1;
        if (selectedRow > maxRow) selectedRow = maxRow;
        if (isBottomRow(selectedRow)) {
          int bottomCount = getBottomKeyCount();
          if (selectedCol >= bottomCount) selectedCol = bottomCount - 1;
        } else {
          if (selectedCol >= COLS) selectedCol = COLS - 1;
        }
        return true;
      }
      case SpecReveal:
        passwordVisible = !passwordVisible;
        return true;
      case SpecSpace:
        return insertChar(' ');
      case SpecDel:
        if (cursorPos > 0 && !text.empty()) {
          text.erase(cursorPos - 1, 1);
          cursorPos--;
        }
        return true;
      case SpecOk:
        onComplete(text);
        return false;
      default:
        return true;
    }
  }

  return insertChar(getSelectedChar());
}

void KeyboardEntryActivity::loop() {
  const int totalRows = getTotalRowCount();

  if (!cursorMode && mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    upHeld = true;
    upLongHandled = false;
  }

  if (upHeld && !upLongHandled && mappedInput.isPressed(MappedInputManager::Button::Up) &&
      mappedInput.getHeldTime() > LONG_PRESS_MS) {
    cursorMode = true;
    upLongHandled = true;
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (upHeld && !upLongHandled && !cursorMode) {
      bool wasBottom = isBottomRow(selectedRow);
      selectedRow = ButtonNavigator::previousIndex(selectedRow, totalRows);
      if (wasBottom && !isBottomRow(selectedRow)) {
        selectedCol = selectedCol * 2;
      } else if (!wasBottom && isBottomRow(selectedRow)) {
        selectedCol = selectedCol / 2;
      }
      int maxCol = isBottomRow(selectedRow) ? getBottomKeyCount() - 1 : COLS - 1;
      if (selectedCol > maxCol) selectedCol = maxCol;
      requestUpdate();
    }
    upHeld = false;
    upLongHandled = false;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    downHeld = true;
    if (cursorMode) {
      cursorMode = false;
      downLongHandled = true;
      requestUpdate();
    } else {
      downLongHandled = false;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (downHeld && !downLongHandled && !cursorMode) {
      bool wasBottom = isBottomRow(selectedRow);
      selectedRow = ButtonNavigator::nextIndex(selectedRow, totalRows);
      if (wasBottom && !isBottomRow(selectedRow)) {
        selectedCol = selectedCol * 2;
      } else if (!wasBottom && isBottomRow(selectedRow)) {
        selectedCol = selectedCol / 2;
      }
      int maxCol = isBottomRow(selectedRow) ? getBottomKeyCount() - 1 : COLS - 1;
      if (selectedCol > maxCol) selectedCol = maxCol;
      requestUpdate();
    }
    downHeld = false;
    downLongHandled = false;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] {
    if (cursorMode) {
      if (cursorPos > 0) {
        cursorPos--;
        requestUpdate();
      }
      return;
    }
    int maxCol = isBottomRow(selectedRow) ? getBottomKeyCount() - 1 : COLS - 1;
    selectedCol = ButtonNavigator::previousIndex(selectedCol, maxCol + 1);
    requestUpdate();
  });

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] {
    if (cursorMode) {
      if (cursorPos < text.length()) {
        cursorPos++;
        requestUpdate();
      }
      return;
    }
    int maxCol = isBottomRow(selectedRow) ? getBottomKeyCount() - 1 : COLS - 1;
    selectedCol = ButtonNavigator::nextIndex(selectedCol, maxCol + 1);
    requestUpdate();
  });

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    confirmHeld = true;
    confirmLongHandled = false;
  }

  if (confirmHeld && !confirmLongHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() > LONG_PRESS_MS) {
    char alt = getAlternativeChar();
    if (alt != '\0') {
      insertChar(alt);
      requestUpdate();
      confirmLongHandled = true;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (confirmHeld && !confirmLongHandled) {
      if (handleKeyPress()) {
        requestUpdate();
      }
    }
    confirmHeld = false;
    confirmLongHandled = false;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onCancel();
  }
}

void KeyboardEntryActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, true, true);

  GUI.drawHeader(renderer, Rect{contentRect.x, metrics.topPadding, contentRect.width, metrics.headerHeight},
                 title.c_str());

  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int inputStartY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing +
                          metrics.verticalSpacing * 4 + metrics.keyboardVerticalOffset;
  int inputHeight = 0;

  std::string displayText;
  if (isPassword && !passwordVisible) {
    displayText = std::string(text.length(), '*');
  } else {
    displayText = text;
  }

  int lineStartIdx = 0;
  int lineEndIdx = displayText.length();
  int textWidth = 0;
  int cursorPixelX = metrics.contentSidePadding;
  int cursorLineY = inputStartY;
  bool cursorDrawn = false;

  while (true) {
    std::string lineText = displayText.substr(lineStartIdx, lineEndIdx - lineStartIdx);
    textWidth = renderer.getTextWidth(UI_12_FONT_ID, lineText.c_str());
    if (textWidth <= pageWidth - 2 * metrics.contentSidePadding) {
      if (!cursorDrawn && cursorPos >= lineStartIdx && cursorPos <= lineEndIdx) {
        std::string beforeCursor = displayText.substr(lineStartIdx, cursorPos - lineStartIdx);
        int beforeWidth = renderer.getTextWidth(UI_12_FONT_ID, beforeCursor.c_str());
        if (metrics.keyboardCenteredText) {
          cursorPixelX = (pageWidth - textWidth) / 2 + beforeWidth;
        } else {
          cursorPixelX = metrics.contentSidePadding + beforeWidth;
        }
        cursorLineY = inputStartY + inputHeight;
        cursorDrawn = true;
      }

      if (metrics.keyboardCenteredText) {
        const int centeredX = contentRect.x + (contentRect.width - textWidth) / 2;
        renderer.drawText(UI_12_FONT_ID, centeredX, inputStartY + inputHeight, lineText.c_str());
      } else {
        renderer.drawText(UI_12_FONT_ID, contentRect.x + metrics.contentSidePadding, inputStartY + inputHeight,
                          lineText.c_str());
      }
      if (lineEndIdx == displayText.length()) {
        break;
      }

      inputHeight += lineHeight;
      lineStartIdx = lineEndIdx;
      lineEndIdx = displayText.length();
    } else {
      lineEndIdx -= 1;
    }
  }

  GUI.drawTextField(renderer, Rect{contentRect.x, inputStartY, contentRect.width, inputHeight}, textWidth);

  const int cursorCharWidth = renderer.getTextWidth(UI_12_FONT_ID, "_");
  if (cursorMode) {
    renderer.fillRect(cursorPixelX, cursorLineY, cursorCharWidth, lineHeight, true);
    if (cursorPos < displayText.length()) {
      const char buf[2] = {displayText[cursorPos], '\0'};
      renderer.drawText(UI_12_FONT_ID, cursorPixelX, cursorLineY, buf, false);
    }
  } else {
    renderer.drawLine(cursorPixelX, cursorLineY + lineHeight - 1, cursorPixelX + cursorCharWidth,
                      cursorLineY + lineHeight - 1, 2, true);
  }

  const int keyHeight = metrics.keyboardKeyHeight;
  const int bottomKeyHeight = metrics.keyboardBottomKeyHeight;
  const int keySpacing = metrics.keyboardKeySpacing;
  const int keyWidth = (pageWidth * 95 / 100 - (COLS - 1) * keySpacing) / COLS;
  const int leftMargin = (pageWidth - (COLS * keyWidth + (COLS - 1) * keySpacing)) / 2;

  const int bottomRowGap = metrics.keyboardBottomKeySpacing > 0 ? 4 : 0;
  const int keyboardStartY = metrics.keyboardBottomAligned
                                 ? pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing -
                                       (keyHeight + keySpacing) * getContentRowCount() - bottomKeyHeight -
                                       bottomRowGap + metrics.keyboardVerticalOffset
                                 : inputStartY + inputHeight + lineHeight + metrics.verticalSpacing;
  const KeyDef(*layout)[COLS] = symMode ? symLayout : abcLayout;
  const int contentRows = getContentRowCount();

  for (int row = 0; row < contentRows; row++) {
    const int rowY = keyboardStartY + row * (keyHeight + keySpacing);

    for (int col = 0; col < COLS; col++) {
      const KeyDef& key = layout[row][col];
      const int keyX = leftMargin + col * (keyWidth + keySpacing);
      const bool isSelected = row == selectedRow && col == selectedCol;

      char primaryChar = key.primary;
      char secondaryChar = key.secondary;

      if (!symMode && shiftState > 0 && key.secondary != '\0') {
        primaryChar = key.secondary;
        secondaryChar = key.primary;
      }

      const char primaryBuf[2] = {primaryChar, '\0'};
      const char secondaryBuf[2] = {secondaryChar, '\0'};
      const bool showSecondary = !symMode && row == 0 && secondaryChar != '\0';
      const bool activeKeySelected = isSelected && !cursorMode;
      GUI.drawKeyboardKey(renderer, Rect{keyX, rowY, keyWidth, keyHeight}, primaryBuf, activeKeySelected,
                          showSecondary ? secondaryBuf : nullptr);
    }
  }

  const int bottomRowY = keyboardStartY + contentRows * (keyHeight + keySpacing) + bottomRowGap;
  const int bkSpacing = metrics.keyboardBottomKeySpacing;
  const int contentTotalWidth = COLS * keyWidth + (COLS - 1) * keySpacing;
  const int bottomKeyCount = getBottomKeyCount();
  const int bottomKeyWidth = (contentTotalWidth - (bottomKeyCount - 1) * bkSpacing) / bottomKeyCount;
  const bool bottomSelected = isBottomRow(selectedRow);

  struct BottomKeyInfo {
    KeyboardKeyType themeType;
    const char* label;
  };

  std::vector<BottomKeyInfo> bottomKeys;
  bottomKeys.reserve(6);
  bottomKeys.push_back({KeyboardKeyType::Shift, getShiftLabel()});
  bottomKeys.push_back({KeyboardKeyType::Mode, symMode ? tr(STR_ABC) : "#@!"});
  if (isPassword) {
    bottomKeys.push_back({KeyboardKeyType::Reveal, passwordVisible ? tr(STR_HIDE) : tr(STR_SHOW)});
  }
  bottomKeys.push_back({KeyboardKeyType::Space, nullptr});
  bottomKeys.push_back({KeyboardKeyType::Del, nullptr});
  bottomKeys.push_back({KeyboardKeyType::Ok, tr(STR_OK_BUTTON)});

  for (int i = 0; i < bottomKeyCount; i++) {
    const int keyX = leftMargin + i * (bottomKeyWidth + bkSpacing);
    const bool isSelected = bottomSelected && i == selectedCol;

    const bool activeKeySelected = isSelected && !cursorMode;
    GUI.drawKeyboardKey(renderer, Rect{keyX, bottomRowY, bottomKeyWidth, bottomKeyHeight}, bottomKeys[i].label,
                        activeKeySelected, nullptr, bottomKeys[i].themeType);
  }

  if (cursorMode) {
    int selKeyX, selKeyY, selKeyW, selKeyH;
    if (isBottomRow(selectedRow)) {
      selKeyX = leftMargin + selectedCol * (bottomKeyWidth + bkSpacing);
      selKeyY = bottomRowY;
      selKeyW = bottomKeyWidth;
      selKeyH = bottomKeyHeight;
    } else {
      selKeyX = leftMargin + selectedCol * (keyWidth + keySpacing);
      selKeyY = keyboardStartY + selectedRow * (keyHeight + keySpacing);
      selKeyW = keyWidth;
      selKeyH = keyHeight;
    }
    renderer.drawRect(selKeyX - 1, selKeyY - 1, selKeyW + 2, selKeyH + 2, 2, true);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  GUI.drawSideButtonHints(renderer, ">", "<");

  renderer.displayBuffer();
}

void KeyboardEntryActivity::onComplete(std::string text) {
  setResult(KeyboardResult{std::move(text)});
  finish();
}

void KeyboardEntryActivity::onCancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}
