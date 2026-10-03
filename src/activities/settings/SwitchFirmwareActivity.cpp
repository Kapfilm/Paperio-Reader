#include "SwitchFirmwareActivity.h"

#include <Arduino.h>
#include <I18n.h>
#include <esp_ota_ops.h>
#include <cstring>

#include "components/UITheme.h"
#include "fontIds.h"
#include "network/FirmwareFlasher.h"
#include "network/OtaBootSwitch.h"

void SwitchFirmwareActivity::onEnter() {
  Activity::onEnter();
  requestUpdateAndWait();
  const auto* running = esp_ota_get_running_partition();
  target = running && esp_ota_get_app_partition_count() == 2 ? esp_ota_get_next_update_partition(running) : nullptr;
  std::string nextError, nextDetail;
  bool nextReady = false;
  size_t bytes = 0;
  if (!target || !running || target->address == running->address ||
      firmware_flash::validateImagePartition(target, &bytes) != firmware_flash::Result::OK) {
    nextError = tr(STR_FIRMWARE_SLOT_INVALID);
  } else {
    esp_app_desc_t desc{};
    if (esp_ota_get_partition_description(target, &desc) != ESP_OK) {
      nextError = tr(STR_FIRMWARE_SLOT_INVALID);
    } else {
      auto safe = [](const char* value, size_t size) {
        std::string text(value, strnlen(value, size));
        for (char& c : text) if (static_cast<unsigned char>(c) < 32) c = ' ';
        return text;
      };
      nextDetail = safe(target->label, sizeof(target->label)) + ": " +
               safe(desc.project_name, sizeof(desc.project_name)) + " " + safe(desc.version, sizeof(desc.version));
      nextReady = true;
    }
  }
  {
    RenderLock lock(*this);
    error = std::move(nextError);
    detail = std::move(nextDetail);
    ready = nextReady;
    busy = false;
  }
  requestUpdate();
}

void SwitchFirmwareActivity::loop() {
  if (!inputArmed) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Back) &&
        !mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
        !mappedInput.isPressed(MappedInputManager::Button::Left) &&
        !mappedInput.isPressed(MappedInputManager::Button::Right) &&
        !mappedInput.wasAnyPressed() && !mappedInput.wasAnyReleased()) inputArmed = true;
    return;
  }
  if (busy) return;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Left)) { finish(); return; }
  if (!ready || !mappedInput.wasReleased(MappedInputManager::Button::Right)) return;
  {
    RenderLock lock(*this);
    busy = true;
  }
  requestUpdateAndWait();
  // Revalidate immediately before selecting; no partition contents are written.
  if (firmware_flash::validateImagePartition(target, nullptr) != firmware_flash::Result::OK ||
      !ota_boot::switchToInstalled(target)) {
    {
      RenderLock lock(*this);
      error = tr(STR_FIRMWARE_SLOT_SWITCH_FAILED);
      ready = false;
      busy = false;
    }
    requestUpdate();
    return;
  }
  ESP.restart();
}

void SwitchFirmwareActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const Rect rect = UITheme::getContentRect(renderer, true, false);
  int y = rect.y + 24;
  const int line = renderer.getLineHeight(UI_10_FONT_ID) + 5;
  renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_SWITCH_FIRMWARE), true, EpdFontFamily::BOLD);
  y += 2 * line;
  const auto draw = [&](const std::string& text, int maxLines) {
    for (const auto& part : renderer.wrappedText(UI_10_FONT_ID, text.c_str(), rect.width - 32, maxLines)) {
      renderer.drawText(UI_10_FONT_ID, rect.x + 16, y, part.c_str());
      y += line;
    }
    y += line;
  };
  if (busy) draw(tr(STR_FIRMWARE_SLOT_CHECKING), 3);
  else if (!error.empty()) draw(error, 6);
  else { draw(detail, 3); draw(tr(STR_FIRMWARE_SLOT_CONFIRM), 5); }
  const auto labels = mappedInput.mapLabels("", "", tr(STR_BACK), ready && !busy ? tr(STR_CONFIRM) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
