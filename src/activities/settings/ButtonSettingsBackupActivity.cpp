#include "ButtonSettingsBackupActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <JsonSettingsIO.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr char BACKUP_PATH[] = "/button-settings.json";
}

ButtonSettingsBackupActivity::ButtonSettingsBackupActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : MenuListActivity("ButtonSettingsBackup", renderer, mappedInput) {
  menuItems.push_back(SettingInfo::Action(StrId::STR_BUTTON_SETTINGS_SAVE, SettingAction::None));
  menuItems.push_back(SettingInfo::Action(StrId::STR_BUTTON_SETTINGS_RESTORE, SettingAction::None));
}

void ButtonSettingsBackupActivity::onActionSelected(const int index) {
  if (menuItems[index].nameId == StrId::STR_BUTTON_SETTINGS_SAVE) {
    status = JsonSettingsIO::saveButtonSettings(SETTINGS, BACKUP_PATH) ? tr(STR_BUTTON_SETTINGS_SAVED)
                                                                      : tr(STR_BUTTON_SETTINGS_SAVE_FAILED);
    requestUpdate();
    return;
  }

  if (menuItems[index].nameId != StrId::STR_BUTTON_SETTINGS_RESTORE) return;
  if (!Storage.exists(BACKUP_PATH)) {
    status = tr(STR_BUTTON_SETTINGS_FILE_MISSING);
    requestUpdate();
    return;
  }

  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_BUTTON_SETTINGS_RESTORE_PROMPT),
                                             BACKUP_PATH),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) restoreFromFile();
        requestUpdate();
      });
}

void ButtonSettingsBackupActivity::restoreFromFile() {
  const String json = Storage.readFile(BACKUP_PATH);
  if (json.isEmpty() || !JsonSettingsIO::loadButtonSettings(SETTINGS, json.c_str()) || !SETTINGS.saveToFile()) {
    status = tr(STR_BUTTON_SETTINGS_RESTORE_FAILED);
    return;
  }
  status = tr(STR_BUTTON_SETTINGS_RESTORED);
}

void ButtonSettingsBackupActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect content = UITheme::getContentRect(renderer, true, false);
  GUI.drawHeader(renderer, Rect{content.x, metrics.topPadding, content.width, metrics.headerHeight},
                 tr(STR_BUTTON_SETTINGS_BACKUP));

  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int statusHeight = status.empty() ? 0 : renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing;
  drawMenuList(Rect{content.x, listTop, content.width, content.height - listTop - statusHeight});
  if (!status.empty()) {
    renderer.drawCenteredText(SMALL_FONT_ID, content.y + content.height - statusHeight, status.c_str());
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
