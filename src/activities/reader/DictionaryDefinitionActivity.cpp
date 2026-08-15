#include "DictionaryDefinitionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/Dictionary.h"
#include "util/DictionaryRegistry.h"

namespace {

constexpr size_t MAX_DRAW_BYTES = 191;
constexpr int SIDE_PADDING = 18;

void indexYield(void*) { vTaskDelay(1); }

bool isLatinQuery(const std::string& word) {
  bool hasLatin = false;
  bool hasCyrillic = false;
  for (size_t i = 0; i < word.size(); ++i) {
    const uint8_t byte = static_cast<uint8_t>(word[i]);
    if ((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z')) {
      hasLatin = true;
    } else if ((byte == 0xD0 || byte == 0xD1) && i + 1 < word.size() &&
               (static_cast<uint8_t>(word[i + 1]) & 0xC0) == 0x80) {
      hasCyrillic = true;
      ++i;
    }
  }
  return hasLatin && !hasCyrillic;
}

bool tagCreatesBreak(const char* tag, const size_t length) {
  return (length == 1 && (*tag == 'p' || *tag == 'P')) ||
         (length == 2 && (tag[0] == 'b' || tag[0] == 'B') && (tag[1] == 'r' || tag[1] == 'R')) ||
         (length == 2 && tag[0] == '/' && (tag[1] == 'p' || tag[1] == 'P')) ||
         (length == 3 && tag[0] == '/' && (tag[1] == 'l' || tag[1] == 'L') && (tag[2] == 'i' || tag[2] == 'I'));
}

bool appendUtf8(std::string& output, uint32_t codepoint) {
  if (codepoint == 0xA0 || codepoint == '\t') codepoint = ' ';
  if (codepoint == '\r') codepoint = '\n';
  if (codepoint == 0 || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) return false;

  if (codepoint <= 0x7F) {
    output.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FF) {
    output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0xFFFF) {
    output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
  return true;
}

bool decodeHtmlEntity(const std::string& input, const size_t start, std::string& output, size_t& consumed) {
  struct NamedEntity {
    const char* encoded;
    size_t length;
    char decoded;
  };
  static constexpr NamedEntity NAMED_ENTITIES[] = {
      {"&nbsp;", 6, ' '}, {"&lt;", 4, '<'},   {"&gt;", 4, '>'},
      {"&amp;", 5, '&'},  {"&quot;", 6, '"'}, {"&apos;", 6, '\''},
  };
  const auto* entity = std::find_if(NAMED_ENTITIES, NAMED_ENTITIES + std::size(NAMED_ENTITIES),
                                    [&input, start](const NamedEntity& candidate) {
                                      return input.compare(start, candidate.length, candidate.encoded) == 0;
                                    });
  if (entity != NAMED_ENTITIES + std::size(NAMED_ENTITIES)) {
    output.push_back(entity->decoded);
    consumed = entity->length;
    return true;
  }

  if (start + 3 >= input.size() || input[start] != '&' || input[start + 1] != '#') return false;
  size_t position = start + 2;
  uint32_t base = 10;
  if (position < input.size() && (input[position] == 'x' || input[position] == 'X')) {
    base = 16;
    ++position;
  }
  const size_t digitsStart = position;
  uint32_t codepoint = 0;
  while (position < input.size() && input[position] != ';') {
    const char character = input[position];
    uint32_t digit;
    if (character >= '0' && character <= '9') {
      digit = character - '0';
    } else if (base == 16 && character >= 'a' && character <= 'f') {
      digit = character - 'a' + 10;
    } else if (base == 16 && character >= 'A' && character <= 'F') {
      digit = character - 'A' + 10;
    } else {
      return false;
    }
    if (codepoint > (0x10FFFF - digit) / base) return false;
    codepoint = codepoint * base + digit;
    ++position;
  }
  if (position == digitsStart || position >= input.size() || input[position] != ';') return false;
  if (!appendUtf8(output, codepoint)) return false;
  consumed = position - start + 1;
  return true;
}

}  // namespace

void DictionaryDefinitionActivity::onEnter() {
  Activity::onEnter();
  state = State::Loading;
  requestUpdateAndWait();

  Dictionary dictionary;
  Dictionary::Result result = Dictionary::Result::NotFound;
  bool found = false;
  bool stopSearching = false;
  bool openedAny = false;
  bool indexFailed = false;

  const auto tryDictionary = [&](const char* name) {
    if (!dictionary.open(name)) return;
    openedAny = true;
    if (dictionary.needsIndex() && !dictionary.buildIndex(&indexYield, nullptr)) {
      indexFailed = true;
      return;
    }

    definition.clear();
    headword.clear();
    Dictionary::Result candidateResult = Dictionary::Result::NotFound;
    if (dictionary.lookup(query.c_str(), definition, headword, &candidateResult)) {
      dictionaryName = name;
      result = Dictionary::Result::Found;
      found = true;
      return;
    }
    if (candidateResult == Dictionary::Result::LowMemory || candidateResult == Dictionary::Result::Unsupported) {
      result = candidateResult;
      stopSearching = true;
    } else if (candidateResult == Dictionary::Result::ReadError && result == Dictionary::Result::NotFound) {
      result = candidateResult;
    }
  };

  if (SETTINGS.dictionaryName[0] != '\0') {
    tryDictionary(SETTINGS.dictionaryName);
  } else {
    std::vector<DictionaryEntry> dictionaries;
    DictionaryRegistry::discover(dictionaries);
    const bool latin = isLatinQuery(query);

    // Search the most likely dictionary group first, then fall back to every
    // remaining dictionary. Only one Dictionary object and one pair of index
    // files are alive at a time, keeping automatic mode RAM-neutral.
    for (int pass = 0; pass < 2 && !found && !stopSearching; ++pass) {
      const bool wantsLatinInput = pass == 0 ? latin : !latin;
      for (const auto& entry : dictionaries) {
        if (entry.prefersLatinInput != wantsLatinInput) continue;
        tryDictionary(entry.name.c_str());
        if (found || stopSearching) break;
      }
    }
  }

  if (!found) {
    state = State::Error;
    if (!openedAny) {
      errorMessage = tr(STR_DICT_NO_DICTIONARY);
    } else if (indexFailed && result == Dictionary::Result::NotFound) {
      errorMessage = tr(STR_DICT_INDEX_FAILED);
    } else {
      switch (result) {
        case Dictionary::Result::LowMemory:
          errorMessage = tr(STR_DICT_LOW_MEMORY);
          break;
        case Dictionary::Result::Unsupported:
          errorMessage = tr(STR_DICT_NEEDS_UNCOMPRESSED);
          break;
        case Dictionary::Result::ReadError:
          errorMessage = tr(STR_DICT_READ_ERROR);
          break;
        case Dictionary::Result::NotFound:
        default:
          errorMessage = tr(STR_DICT_NOT_FOUND);
          break;
      }
    }
    requestUpdate();
    return;
  }

  sanitizeDefinition();
  wrapDefinition();
  state = State::Ready;
  requestUpdate();
}

void DictionaryDefinitionActivity::sanitizeDefinition() {
  std::replace(definition.begin(), definition.end(), '\0', '\n');
  std::string clean;
  clean.reserve(definition.size());
  for (size_t i = 0; i < definition.size();) {
    if (definition[i] == '<') {
      const size_t end = definition.find('>', i + 1);
      if (end == std::string::npos) {
        clean.push_back(definition[i++]);
        continue;
      }
      const char* tag = definition.data() + i + 1;
      const size_t tagLength = end - i - 1;
      if (tagCreatesBreak(tag, tagLength) && !clean.empty() && clean.back() != '\n') clean.push_back('\n');
      i = end + 1;
      continue;
    }
    size_t consumed = 0;
    if (definition[i] == '&' && decodeHtmlEntity(definition, i, clean, consumed)) {
      i += consumed;
      continue;
    }
    clean.push_back(definition[i++]);
  }
  definition = std::move(clean);
}

int DictionaryDefinitionActivity::measure(const char* text, size_t length) const {
  char buffer[MAX_DRAW_BYTES + 1];
  length = std::min(length, MAX_DRAW_BYTES);
  memcpy(buffer, text, length);
  buffer[length] = '\0';
  return renderer.getTextAdvanceX(SETTINGS.getReaderFontId(), buffer, EpdFontFamily::REGULAR);
}

void DictionaryDefinitionActivity::wrapDefinition() {
  lines.clear();
  lines.reserve(definition.size() / 32 + 8);
  const int fontId = SETTINGS.getReaderFontId();
  const Rect content = UITheme::getContentRect(renderer, true, true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int bodyTop = content.y + metrics.headerHeight + metrics.verticalSpacing;
  const int bodyHeight = std::max(1, content.y + content.height - bodyTop);
  const int maxWidth = std::max(1, content.width - SIDE_PADDING * 2);
  const int lineHeight = renderer.getLineHeight(fontId);
  linesPerPage = std::max(1, bodyHeight / lineHeight);

  const char* text = definition.c_str();
  const uint32_t size = static_cast<uint32_t>(definition.size());
  uint32_t lineStart = 0;
  uint32_t lineEnd = 0;
  int lineWidth = 0;
  const int spaceWidth = renderer.getSpaceWidth(fontId, EpdFontFamily::REGULAR);
  const auto flush = [&](const uint32_t nextStart) {
    lines.push_back({lineStart, static_cast<uint16_t>(lineEnd - lineStart)});
    lineStart = nextStart;
    lineEnd = nextStart;
    lineWidth = 0;
  };

  uint32_t i = 0;
  while (i < size) {
    if (text[i] == '\n') {
      flush(i + 1);
      ++i;
      continue;
    }
    if (text[i] == ' ' || text[i] == '\t' || text[i] == '\r') {
      ++i;
      continue;
    }
    const uint32_t tokenStart = i;
    while (i < size && text[i] != ' ' && text[i] != '\t' && text[i] != '\r' && text[i] != '\n' &&
           i - tokenStart < MAX_DRAW_BYTES) {
      ++i;
    }
    while (i - tokenStart > 1 && i < size && (static_cast<uint8_t>(text[i]) & 0xC0) == 0x80) --i;
    const uint32_t tokenLength = i - tokenStart;
    if (tokenLength == 0) {
      ++i;
      continue;
    }
    const int width = measure(text + tokenStart, tokenLength);
    if (lineEnd == lineStart) {
      lineStart = tokenStart;
      lineEnd = tokenStart + tokenLength;
      lineWidth = width;
    } else if (lineWidth + spaceWidth + width <= maxWidth && tokenStart + tokenLength - lineStart <= UINT16_MAX) {
      lineEnd = tokenStart + tokenLength;
      lineWidth += spaceWidth + width;
    } else {
      flush(tokenStart);
      lineEnd = tokenStart + tokenLength;
      lineWidth = width;
    }
  }
  if (lineEnd > lineStart) flush(size);
  while (!lines.empty() && lines.back().length == 0) lines.pop_back();
  totalPages = std::max(1, (static_cast<int>(lines.size()) + linesPerPage - 1) / linesPerPage);
}

void DictionaryDefinitionActivity::loop() {
  using Button = MappedInputManager::Button;
  ButtonEventManager::ButtonEvent event;
  while (buttonEvents.consumeEvent(event)) {
    if (event.type != ButtonEventManager::PressType::Short) continue;
    if (event.button == Button::Back) {
      finish();
      return;
    }
    const bool previous = event.button == Button::Left || event.button == Button::PageBack;
    const bool next = event.button == Button::Right || event.button == Button::PageForward;
    if (previous && currentPage > 0) {
      --currentPage;
      requestUpdate();
    } else if (next && currentPage + 1 < totalPages) {
      ++currentPage;
      requestUpdate();
    }
  }
}

void DictionaryDefinitionActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const Rect content = UITheme::getContentRect(renderer, true, true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const char* title = state == State::Ready ? headword.c_str() : tr(STR_DICTIONARY);
  GUI.drawHeader(renderer, Rect{content.x, content.y, content.width, metrics.headerHeight}, title,
                 dictionaryName.empty() ? nullptr : dictionaryName.c_str());

  // This activity starts at the raw content edge rather than the usual settings
  // top padding. Repaint only the battery area a little lower and farther right;
  // keep the title and clock aligned with every other header.
  constexpr int BATTERY_OFFSET_X = 4;
  constexpr int BATTERY_OFFSET_Y = 4;
  constexpr int BATTERY_CLEAR_WIDTH = 80;
  renderer.fillRect(content.x + content.width - BATTERY_CLEAR_WIDTH, content.y + 5, BATTERY_CLEAR_WIDTH,
                    metrics.batteryHeight + 14, false);
  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int batteryX = content.x + content.width - 12 - metrics.batteryWidth + BATTERY_OFFSET_X;
  GUI.drawBatteryRight(renderer,
                       Rect{batteryX, content.y + 5 + BATTERY_OFFSET_Y, metrics.batteryWidth, metrics.batteryHeight},
                       showBatteryPercentage);

  if (state == State::Loading) {
    renderer.drawCenteredText(UI_10_FONT_ID, content.y + content.height / 2, tr(STR_DICT_LOOKING_UP));
  } else if (state == State::Error) {
    renderer.drawCenteredText(UI_10_FONT_ID, content.y + content.height / 2,
                              errorMessage ? errorMessage : tr(STR_ERROR_MSG));
  } else {
    const int fontId = SETTINGS.getReaderFontId();
    const int lineHeight = renderer.getLineHeight(fontId);
    const int startY = content.y + metrics.headerHeight + metrics.verticalSpacing;
    const int first = currentPage * linesPerPage;
    const int last = std::min(first + linesPerPage, static_cast<int>(lines.size()));
    char buffer[MAX_DRAW_BYTES + 1];
    for (int i = first; i < last; ++i) {
      if (lines[i].length == 0) continue;
      const size_t length = std::min(static_cast<size_t>(lines[i].length), MAX_DRAW_BYTES);
      memcpy(buffer, definition.data() + lines[i].start, length);
      buffer[length] = '\0';
      renderer.drawText(fontId, content.x + SIDE_PADDING, startY + (i - first) * lineHeight, buffer);
    }
  }

  char page[16] = {};
  if (state == State::Ready && totalPages > 1) snprintf(page, sizeof(page), "%d/%d", currentPage + 1, totalPages);
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), page, currentPage > 0 ? "<" : "", currentPage + 1 < totalPages ? ">" : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
