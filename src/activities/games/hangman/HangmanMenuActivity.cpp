#include "HangmanMenuActivity.h"

#include <I18n.h>

#include <cstdio>
#include <memory>

#include "HangmanGameActivity.h"
#include "HangmanWordEntryActivity.h"
#include "components/UITheme.h"

void HangmanMenuActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  preferences = HangmanStore::loadPreferences();
  stats = HangmanStore::loadStats();
  showingStats = false;
  selected = 0;
  buildItems();
  requestUpdate();
}

void HangmanMenuActivity::buildItems() {
  items.clear();
  if (HangmanStore::hasInProgress()) items.push_back(ItemKind::Continue);
  items.push_back(ItemKind::NewGame);
  items.push_back(ItemKind::TwoPlayer);
  items.push_back(ItemKind::Difficulty);
  items.push_back(ItemKind::Language);
  items.push_back(ItemKind::Category);
  items.push_back(ItemKind::Stats);
}

void HangmanMenuActivity::loop() {
  if (showingStats) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      showingStats = false;
      requestUpdate();
    }
    return;
  }

  const int count = static_cast<int>(items.size());
  buttonNavigator.onNext([this, count] {
    selected = ButtonNavigator::nextIndex(selected, count);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, count] {
    selected = ButtonNavigator::previousIndex(selected, count);
    requestUpdate();
  });
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelected();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToGames();
  }
}

void HangmanMenuActivity::activateSelected() {
  if (selected < 0 || selected >= static_cast<int>(items.size())) return;
  switch (items[selected]) {
    case ItemKind::Continue:
      activityManager.replaceActivity(std::make_unique<HangmanGameActivity>(renderer, mappedInput, true));
      return;
    case ItemKind::NewGame:
      HangmanStore::clear();
      activityManager.replaceActivity(
          std::make_unique<HangmanGameActivity>(renderer, mappedInput, preferences.language, preferences.category,
                                                preferences.difficulty));
      return;
    case ItemKind::TwoPlayer:
      activityManager.replaceActivity(
          std::make_unique<HangmanWordEntryActivity>(renderer, mappedInput, preferences.language));
      return;
    case ItemKind::Difficulty:
      preferences.difficulty = preferences.difficulty == HangmanDifficulty::Easy ? HangmanDifficulty::Hard
                                                                                  : HangmanDifficulty::Easy;
      HangmanStore::savePreferences(preferences);
      requestUpdate();
      return;
    case ItemKind::Language:
      preferences.language =
          preferences.language == HangmanLanguage::English ? HangmanLanguage::Russian : HangmanLanguage::English;
      HangmanStore::savePreferences(preferences);
      requestUpdate();
      return;
    case ItemKind::Category:
      preferences.category = static_cast<HangmanCategory>((static_cast<uint8_t>(preferences.category) + 1) % 3);
      HangmanStore::savePreferences(preferences);
      requestUpdate();
      return;
    case ItemKind::Stats:
      stats = HangmanStore::loadStats();
      showingStats = true;
      requestUpdate();
      return;
  }
}

const char* HangmanMenuActivity::languageName() const {
  return preferences.language == HangmanLanguage::Russian ? tr(STR_HANGMAN_RUSSIAN) : tr(STR_HANGMAN_ENGLISH);
}

const char* HangmanMenuActivity::categoryName() const {
  switch (preferences.category) {
    case HangmanCategory::Nature:
      return tr(STR_HANGMAN_NATURE);
    case HangmanCategory::Objects:
      return tr(STR_HANGMAN_OBJECTS);
    case HangmanCategory::Animals:
    default:
      return tr(STR_HANGMAN_ANIMALS);
  }
}

const char* HangmanMenuActivity::difficultyName() const {
  return preferences.difficulty == HangmanDifficulty::Hard ? tr(STR_HANGMAN_HARD) : tr(STR_HANGMAN_EASY);
}

void HangmanMenuActivity::render(RenderLock&&) {
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.clearScreen();
  if (showingStats) {
    renderStats();
  } else {
    renderMenu();
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void HangmanMenuActivity::renderMenu() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_HANGMAN_TITLE));
  const int listY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listH = height - listY - metrics.buttonHintsHeight - metrics.verticalSpacing;

  GUI.drawList(
      renderer, Rect{0, listY, width, listH}, static_cast<int>(items.size()), selected,
      [this](const int index) {
        if (index < 0 || index >= static_cast<int>(items.size())) return std::string();
        switch (items[index]) {
          case ItemKind::Continue:
            return std::string(tr(STR_GAME_CONTINUE));
          case ItemKind::NewGame:
            return std::string(tr(STR_GAME_NEW_GAME));
          case ItemKind::TwoPlayer:
            return std::string(tr(STR_HANGMAN_TWO_PLAYER));
          case ItemKind::Difficulty:
            return std::string(tr(STR_HANGMAN_DIFFICULTY));
          case ItemKind::Language:
            return std::string(tr(STR_HANGMAN_LANGUAGE));
          case ItemKind::Category:
            return std::string(tr(STR_HANGMAN_CATEGORY));
          case ItemKind::Stats:
            return std::string(tr(STR_GAME_STATS));
        }
        return std::string();
      },
      [this](const int index) {
        if (index < 0 || index >= static_cast<int>(items.size())) return std::string();
        switch (items[index]) {
          case ItemKind::NewGame: {
            std::string subtitle(languageName());
            subtitle += " · ";
            subtitle += categoryName();
            subtitle += " · ";
            subtitle += difficultyName();
            return subtitle;
          }
          case ItemKind::Difficulty:
            return std::string(difficultyName());
          case ItemKind::TwoPlayer:
            return std::string(tr(STR_HANGMAN_TWO_PLAYER_DESC));
          case ItemKind::Language:
            return std::string(languageName());
          case ItemKind::Category:
            return std::string(categoryName());
          case ItemKind::Stats: {
            char value[64];
            snprintf(value, sizeof(value), "%s: %lu · %s: %lu", tr(STR_HANGMAN_WINS),
                     static_cast<unsigned long>(stats.wins), tr(STR_HANGMAN_LOSSES),
                     static_cast<unsigned long>(stats.losses));
            return std::string(value);
          }
          case ItemKind::Continue:
          default:
            return std::string();
        }
      });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void HangmanMenuActivity::renderStats() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_GAME_STATS));
  const int listY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listH = height - listY - metrics.buttonHintsHeight - metrics.verticalSpacing;

  GUI.drawList(
      renderer, Rect{0, listY, width, listH}, 3, -1,
      [](const int index) {
        if (index == 0) return std::string(tr(STR_HANGMAN_WINS));
        if (index == 1) return std::string(tr(STR_HANGMAN_LOSSES));
        return std::string(tr(STR_HANGMAN_WIN_RATE));
      },
      [this](const int index) {
        char value[32];
        if (index == 0) {
          snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(stats.wins));
        } else if (index == 1) {
          snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(stats.losses));
        } else {
          const uint64_t total = static_cast<uint64_t>(stats.wins) + stats.losses;
          const unsigned long rate =
              total == 0 ? 0 : static_cast<unsigned long>((static_cast<uint64_t>(stats.wins) * 100u) / total);
          snprintf(value, sizeof(value), "%lu%%", rate);
        }
        return std::string(value);
      });
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
