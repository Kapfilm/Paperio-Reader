#include "PegSolitaireActivity.h"

#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "UiFonts.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr const char* kPrefs = "/.crosspoint/peg-prefs.bin";
const char* savePath(PegSolitaireModel::Board b) {
  return b == PegSolitaireModel::Board::English ? "/.crosspoint/peg-english.bin" : "/.crosspoint/peg-triangle.bin";
}


bool readBytes(const std::string& path, std::vector<uint8_t>& bytes) {
  HalFile file;
  if (!Storage.openFileForRead("PEG", path.c_str(), file) || file.size() > 128 || file.size() == 0) return false;
  bytes.resize(file.size());
  return file.read(bytes.data(), bytes.size()) == static_cast<int>(bytes.size());
}

// Verify the completed temporary file before replacing the old save. The backup
// also makes an interrupted rename recoverable on the next entry into the game.
bool writeSafely(const std::string& path, const std::vector<uint8_t>& bytes) {
  const auto temp = path + ".tmp";
  const auto backup = path + ".bak";
  HalFile file;
  if (!Storage.openFileForWrite("PEG", temp.c_str(), file) ||
      file.write(bytes.data(), bytes.size()) != bytes.size()) return false;
  file.flush();
  if (!file.close()) return false;
  std::vector<uint8_t> check;
  if (!readBytes(temp, check) || check != bytes) return false;
  if (Storage.exists(path.c_str())) {
    if (Storage.exists(backup.c_str()) && !Storage.remove(backup.c_str())) return false;
    if (!Storage.rename(path.c_str(), backup.c_str())) return false;
  }
  if (!Storage.rename(temp.c_str(), path.c_str())) {
    if (!Storage.exists(path.c_str()) && Storage.exists(backup.c_str())) Storage.rename(backup.c_str(), path.c_str());
    return false;
  }
  if (Storage.exists(backup.c_str())) Storage.remove(backup.c_str());
  return true;
}
}

void PegSolitaireActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  auto type = PegSolitaireModel::Board::English;
  for (const auto& path : {std::string(kPrefs), std::string(kPrefs) + ".bak", std::string(kPrefs) + ".tmp"}) {
    std::vector<uint8_t> prefs;
    if (!readBytes(path, prefs) || prefs.size() != 7 || prefs[0] != 'P' || prefs[1] != 'E' ||
        prefs[2] != 'G' || prefs[3] != 1 || prefs[4] > 1) continue;
    type = static_cast<PegSolitaireModel::Board>(prefs[4]);
    if (prefs[5] >= 1 && prefs[5] <= 33) best[0] = prefs[5];
    if (prefs[6] >= 1 && prefs[6] <= 15) best[1] = prefs[6];
    break;
  }
  load(type);
  screen = Screen::Menu;
  requestUpdate();
}

void PegSolitaireActivity::onExit() {
  save();
  Activity::onExit();
}

const char* PegSolitaireActivity::boardName() const {
  return board.board() == PegSolitaireModel::Board::English ? tr(STR_PEG_ENGLISH) : tr(STR_PEG_TRIANGLE);
}

bool PegSolitaireActivity::load(PegSolitaireModel::Board type) {
  board.reset(type);
  cursor = board.initialHole();
  picked = -1;
  const std::string path(savePath(type));
  for (const auto& candidate : {path, path + ".bak", path + ".tmp"}) {
    std::vector<uint8_t> data;
    if (!readBytes(candidate, data) || data.size() < 6 || data[0] != 'P' || data[1] != 'E' ||
        data[2] != 'G' || data[3] != 1 || data[4] != static_cast<uint8_t>(type) || data[5] > 31 ||
        data.size() != 6u + 2u * data[5]) continue;
    std::vector<PegSolitaireModel::Jump> jumps;
    for (int i = 0; i < data[5]; ++i) jumps.push_back({data[6 + 2*i], data[7 + 2*i]});
    if (board.restore(type, jumps)) return true;
  }
  return false;
}

void PegSolitaireActivity::savePreferences() {
  if (!Storage.exists("/.crosspoint") && !Storage.mkdir("/.crosspoint")) { saveFailed = true; return; }
  const std::vector<uint8_t> prefs = {'P', 'E', 'G', 1, static_cast<uint8_t>(board.board()), best[0], best[1]};
  if (!writeSafely(kPrefs, prefs)) saveFailed = true;
}

bool PegSolitaireActivity::save() {
  if (!Storage.exists("/.crosspoint") && !Storage.mkdir("/.crosspoint")) { saveFailed = true; return false; }
  if (!board.anyMove()) {
    auto& record = best[static_cast<int>(board.board())];
    record = std::min(record, static_cast<uint8_t>(board.count()));
  }
  saveFailed = false;
  std::vector<uint8_t> data = {'P', 'E', 'G', 1, static_cast<uint8_t>(board.board()), static_cast<uint8_t>(board.moves())};
  for (auto j : board.history()) { data.push_back(j.from); data.push_back(j.to); }
  if (!writeSafely(savePath(board.board()), data)) { saveFailed = true; return false; }
  savePreferences();
  return !saveFailed;
}

void PegSolitaireActivity::move(int dr, int dc) {
  int next = board.navigate(cursor, dr, dc);
  if (next != cursor) { cursor = next; requestUpdate(); }
}

void PegSolitaireActivity::undo() {
  if (board.undo()) { picked = -1; save(); requestUpdate(); }
}

void PegSolitaireActivity::choose() {
  if (board.occupied(cursor)) {
    picked = picked == cursor ? -1 : (board.canJump(cursor) ? cursor : -1);
  } else if (picked >= 0 && board.jump(picked, cursor)) {
    picked = -1;
    save();
  }
  requestUpdate();
}

void PegSolitaireActivity::loop() {
  if (screen == Screen::Menu) {
    navigator.onNext([this] { menuIndex = ButtonNavigator::nextIndex(menuIndex, 6); requestUpdate(); });
    navigator.onPrevious([this] { menuIndex = ButtonNavigator::previousIndex(menuIndex, 6); requestUpdate(); });
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      if (save()) activityManager.goToGames();
      else { screen = Screen::Playing; requestUpdate(); }
      return;
    }
    if (!mappedInput.wasReleased(MappedInputManager::Button::Confirm)) return;
    switch (menuIndex) {
      case 0: screen = Screen::Playing; break;
      case 1: undo(); screen = Screen::Playing; break;
      case 2: screen = Screen::ConfirmRestart; break;
      case 3: {
        if (!save()) { screen = Screen::Playing; break; }
        auto target = board.board() == PegSolitaireModel::Board::English ? PegSolitaireModel::Board::Triangle
                                                                       : PegSolitaireModel::Board::English;
        load(target);
        savePreferences();
        break;
      }
      case 4: helpLine = 0; screen = Screen::Help; break;
      default:
        if (save()) activityManager.goToGames();
        else { screen = Screen::Playing; requestUpdate(); }
        return;
    }
    requestUpdate();
    return;
  }
  if (screen == Screen::ConfirmRestart) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      board.reset(board.board()); picked = -1; cursor = board.initialHole(); save(); screen = Screen::Playing;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) { screen = Screen::Menu; requestUpdate(); }
    return;
  }
  if (screen == Screen::Help) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID) + 3;
    const int top = metrics.topPadding + metrics.headerHeight + 24;
    const int pageLines = std::max(1, (renderer.getScreenHeight() - metrics.buttonHintsHeight - top - 12) / lineHeight);
    const auto lines = renderer.wrappedText(UI_10_FONT_ID, tr(STR_PEG_HELP), renderer.getScreenWidth() - 40, 100);
    const int maxOffset = std::max(0, static_cast<int>(lines.size()) - pageLines);
    navigator.onNext([this, maxOffset] { helpLine = std::min(helpLine + 1, maxOffset); requestUpdate(); });
    navigator.onPrevious([this] { helpLine = std::max(0, helpLine - 1); requestUpdate(); });
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) { screen = Screen::Menu; requestUpdate(); }
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) move(-1, 0);
  else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) move(1, 0);
  else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) move(0, -1);
  else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) move(0, 1);
  else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) choose();
  else if (mappedInput.wasReleased(MappedInputManager::Button::Power)) undo();
  else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (picked >= 0) picked = -1;
    else { screen = Screen::Menu; menuIndex = 0; save(); }
    requestUpdate();
  }
}

void PegSolitaireActivity::drawPlaying() {
  const auto& m = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  char status[96];
  snprintf(status, sizeof(status), "%s · %s %d · %s %d", boardName(), tr(STR_PEG_PEGS), board.count(),
           tr(STR_PEG_JUMPS), board.moves());
  const int subY = m.topPadding + m.headerHeight;
  // Keep both counters visible even with the largest interface text.
  renderer.drawText(UI_HINT_FONT_ID, m.contentSidePadding, subY + 7, status);
  const Rect content = UITheme::getContentRect(renderer, true, false);
  const int top = subY + m.tabBarHeight + 18;
  // Two message lines plus the best-result caption must clear the footer at every UI size.
  const int messageHeight = std::max(90, 2 * renderer.getLineHeight(UI_10_FONT_ID) +
                                           renderer.getLineHeight(UI_HINT_FONT_ID) + 12);
  const int bottom = content.y + content.height - messageHeight;
  const int rows = board.board() == PegSolitaireModel::Board::English ? 7 : 5;
  const int pitch = std::min({(width - 48) / rows, std::max(1, bottom - top) / rows, 66});
  const int y0 = top + std::max(0, (bottom - top - rows * pitch) / 2) + pitch / 2;
  const int x0 = (width - rows * pitch) / 2 + pitch / 2;
  const int radius = std::max(4, pitch / 3);
  for (int i = 0; i < board.size(); ++i) {
    const auto& cell = board.cell(i);
    const int x = board.board() == PegSolitaireModel::Board::English ? x0 + cell.col * pitch
                         : width / 2 + (2 * cell.col - cell.row) * pitch / 2;
    const int y = y0 + cell.row * pitch;
    const int r = board.occupied(i) ? radius : std::max(3, radius / 3);
    renderer.drawRoundedRect(x-r, y-r, 2*r+1, 2*r+1, board.occupied(i) ? 3 : 1, r, true);
    if (picked >= 0 && board.legal(picked, i))
      renderer.drawRoundedRect(x-radius, y-radius, 2*radius+1, 2*radius+1, 2, radius, true);
    if (i == picked) renderer.drawRect(x-radius-5, y-radius-5, 2*radius+11, 2*radius+11, 2, true);
    if (i == cursor) {
      const int edge = pitch / 2 - 2;
      renderer.drawRoundedRect(x-edge, y-edge, 2*edge+1, 2*edge+1, 2, 5, true);
    }
  }
  const char* message = picked >= 0 ? tr(STR_PEG_PICK_TARGET) : tr(STR_PEG_PICK_PEG);
  if (!board.anyMove()) message = board.perfect() ? tr(STR_PEG_PERFECT) : board.count() == 1 ? tr(STR_PEG_WIN)
                                                                                       : tr(STR_PEG_NO_MOVES);
  if (saveFailed) message = tr(STR_PEG_SAVE_ERROR);
  int y = bottom + 8;
  for (const auto& line : renderer.wrappedText(UI_10_FONT_ID, message, width - 40, 2)) {
    renderer.drawText(UI_10_FONT_ID, 20, y, line.c_str()); y += renderer.getLineHeight(UI_10_FONT_ID);
  }
  if (!board.anyMove() && !saveFailed) {
    char bestLine[64];
    snprintf(bestLine, sizeof(bestLine), "%s %u", tr(STR_PEG_BEST), best[static_cast<int>(board.board())]);
    renderer.drawText(UI_HINT_FONT_ID, 20, y + 2, bestLine);
  }
}

void PegSolitaireActivity::render(RenderLock&&) {
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.clearScreen();
  const auto& m = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, m.topPadding, width, m.headerHeight}, tr(STR_PEG_TITLE));
  if (screen == Screen::Playing) {
    drawPlaying();
  } else if (screen == Screen::Menu) {
    constexpr StrId titles[] = {StrId::STR_GAME_CONTINUE, StrId::STR_PEG_UNDO, StrId::STR_GAME_NEW_GAME,
                               StrId::STR_PEG_BOARD, StrId::STR_PEG_RULES, StrId::STR_GAME_EXIT};
    const int top = m.topPadding + m.headerHeight + m.verticalSpacing;
    GUI.drawList(renderer, Rect{0, top, width, height - top - m.buttonHintsHeight - m.verticalSpacing}, 6, menuIndex,
                 [&](int i) { return std::string(I18N.get(titles[i])); }, nullptr, nullptr,
                 [this](int i) { return i == 3 ? std::string(boardName()) : std::string(); });
  } else {
    const char* message = screen == Screen::Help ? tr(STR_PEG_HELP) : tr(STR_PEG_RESTART_CONFIRM);
    const int top = m.topPadding + m.headerHeight + 24;
    int y = top;
    const auto lines = renderer.wrappedText(UI_10_FONT_ID, message, width - 40, 100);
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID) + 3;
    const int pageLines = std::max(1, (height - m.buttonHintsHeight - top - 12) / lineHeight);
    const int start = screen == Screen::Help ? std::min(helpLine, std::max(0, static_cast<int>(lines.size()) - pageLines)) : 0;
    for (int i = start; i < static_cast<int>(lines.size()) && i < start + pageLines; ++i) {
      renderer.drawText(UI_10_FONT_ID, 20, y, lines[i].c_str()); y += lineHeight;
    }
  }
  const auto labels = mappedInput.mapLabels(screen == Screen::Playing ? tr(STR_PEG_MENU) : tr(STR_BACK),
      screen == Screen::ConfirmRestart ? tr(STR_YES) : tr(STR_SELECT),
      screen == Screen::Help ? tr(STR_DIR_UP) : tr(STR_DIR_LEFT),
      screen == Screen::Help ? tr(STR_DIR_DOWN) : tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
