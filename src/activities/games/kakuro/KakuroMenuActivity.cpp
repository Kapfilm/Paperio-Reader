#include "KakuroMenuActivity.h"

#include <I18n.h>

#include <cstdio>
#include <memory>

#include "KakuroGameActivity.h"
#include "components/UITheme.h"

KakuroMenuActivity::KakuroMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("KakuroMenu", renderer, mappedInput) {}

void KakuroMenuActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  selected = 0;
  showingStats = false;
  stats = KakuroStore::loadStats();
  buildItems();
  requestUpdate();
}

void KakuroMenuActivity::buildItems() {
  items.clear();
  items.reserve(5);
  if (KakuroStore::hasInProgress()) {
    KakuroSaveSlot slot;
    if (KakuroStore::load(slot)) {
      resumeDifficulty = slot.difficulty;
      resumeElapsedSec = slot.elapsedSec;
      const uint8_t total = slot.board.countPlayable();
      resumeProgressPercent = total == 0 ? 0 : static_cast<uint8_t>((slot.board.countFilled() * 100u) / total);
      items.push_back({ItemKind::Continue, slot.difficulty});
    }
  }
  items.push_back({ItemKind::NewGame, KakuroBoard::Difficulty::Easy});
  items.push_back({ItemKind::NewGame, KakuroBoard::Difficulty::Medium});
  items.push_back({ItemKind::NewGame, KakuroBoard::Difficulty::Hard});
  items.push_back({ItemKind::Stats, KakuroBoard::Difficulty::Easy});
}

void KakuroMenuActivity::loop() {
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
    selectItem();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToGames();
  }
}

void KakuroMenuActivity::selectItem() {
  if (selected < 0 || selected >= static_cast<int>(items.size())) return;
  const Item item = items[selected];
  if (item.kind == ItemKind::Continue) {
    activityManager.replaceActivity(std::make_unique<KakuroGameActivity>(renderer, mappedInput, item.difficulty, true));
  } else if (item.kind == ItemKind::NewGame) {
    KakuroStore::clear();
    activityManager.replaceActivity(
        std::make_unique<KakuroGameActivity>(renderer, mappedInput, item.difficulty, false));
  } else {
    showingStats = true;
    requestUpdate();
  }
}

void KakuroMenuActivity::render(RenderLock&&) {
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.clearScreen();
  if (showingStats) {
    renderStats();
  } else {
    renderList();
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void KakuroMenuActivity::renderList() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_KAKURO_TITLE));

  const int listY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listHeight = height - listY - metrics.buttonHintsHeight - metrics.verticalSpacing;
  auto title = [this](int index) -> std::string {
    const Item item = items[index];
    if (item.kind == ItemKind::Continue) return tr(STR_GAME_CONTINUE);
    if (item.kind == ItemKind::Stats) return tr(STR_GAME_STATS);
    if (item.difficulty == KakuroBoard::Difficulty::Medium) return tr(STR_KAKURO_NEW_MEDIUM);
    if (item.difficulty == KakuroBoard::Difficulty::Hard) return tr(STR_KAKURO_NEW_HARD);
    return tr(STR_KAKURO_NEW_EASY);
  };
  auto subtitle = [this](int index) -> std::string {
    const Item item = items[index];
    char buffer[88];
    if (item.kind == ItemKind::Continue) {
      snprintf(buffer, sizeof(buffer), "%s · %02u:%02u · %u%%", KakuroGameActivity::difficultyName(resumeDifficulty),
               static_cast<unsigned>(resumeElapsedSec / 60), static_cast<unsigned>(resumeElapsedSec % 60),
               static_cast<unsigned>(resumeProgressPercent));
      return buffer;
    }
    if (item.kind == ItemKind::Stats) {
      if (stats.bestTimeSec[0] == 0) return tr(STR_GAME_NO_RECORD);
      snprintf(buffer, sizeof(buffer), "%s %02u:%02u · %u %s", tr(STR_SUDOKU_DIFFICULTY_EASY),
               static_cast<unsigned>(stats.bestTimeSec[0] / 60), static_cast<unsigned>(stats.bestTimeSec[0] % 60),
               static_cast<unsigned>(stats.completedCount[0]), tr(STR_KAKURO_COMPLETED));
      return buffer;
    }
    if (item.difficulty == KakuroBoard::Difficulty::Medium) return tr(STR_KAKURO_MEDIUM_DESC);
    if (item.difficulty == KakuroBoard::Difficulty::Hard) return tr(STR_KAKURO_HARD_DESC);
    return tr(STR_KAKURO_EASY_DESC);
  };
  GUI.drawList(renderer, Rect{0, listY, width, listHeight}, static_cast<int>(items.size()), selected, title, subtitle);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void KakuroMenuActivity::renderStats() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_GAME_STATS));
  const int listY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listHeight = height - listY - metrics.buttonHintsHeight - metrics.verticalSpacing;
  auto title = [](int index) -> std::string {
    if (index == 1) return tr(STR_SUDOKU_DIFFICULTY_MEDIUM);
    if (index == 2) return tr(STR_SUDOKU_DIFFICULTY_HARD);
    return tr(STR_SUDOKU_DIFFICULTY_EASY);
  };
  auto subtitle = [this](int index) -> std::string {
    if (stats.bestTimeSec[index] == 0) return tr(STR_GAME_NO_RECORD);
    const unsigned rate = stats.startedCount[index] == 0
                              ? 0
                              : static_cast<unsigned>((stats.completedCount[index] * 100u) / stats.startedCount[index]);
    char buffer[96];
    snprintf(buffer, sizeof(buffer), "%s %02u:%02u · %u %s · %u%%", tr(STR_GAME_BEST_TIME),
             static_cast<unsigned>(stats.bestTimeSec[index] / 60), static_cast<unsigned>(stats.bestTimeSec[index] % 60),
             static_cast<unsigned>(stats.completedCount[index]), tr(STR_KAKURO_COMPLETED), rate);
    return buffer;
  };
  GUI.drawList(renderer, Rect{0, listY, width, listHeight}, 3, -1, title, subtitle);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
