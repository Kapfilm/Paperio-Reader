#pragma once

#include <Epub/converters/ImageToFramebufferDecoder.h>
#include <I18nKeys.h>

#include <functional>
#include <string>

#include "../Activity.h"
#include "MappedInputManager.h"

class BmpViewerActivity final : public Activity {
 public:
  BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath);

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  std::string filePath;
  uint8_t imageDitherMode;
  bool renderCurrentImage(bool showControls = true);
  bool renderBmpImage(bool showControls = true);
  bool renderDecodedImage(bool showControls = true);
  void cycleDitherMode();
  StrId getCurrentDitherModeLabel() const;
  void renderError(const char* message);
  void setAsSleepScreen();
};
