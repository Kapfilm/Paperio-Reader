#pragma once

#include <string>

#include "SerialTransferDevice.h"
#include "activities/Activity.h"

// USB serial file-transfer mode. Wire-compatible with CidVonHighwind/MicroReader
// (https://github.com/CidVonHighwind/microreader): a host (its Calibre plugin or
// tools/serial_cmd.py) uploads/lists/removes books over the USB-CDC link.
//
// Modeled on CrossPointWebServerActivity but for USB instead of WiFi:
//  - While active it OWNS the serial input (ownsSerialInput()), so main.cpp's
//    line-based `CMD:` reader stands down and can't steal protocol bytes.
//  - It mutes LOG_* on the wire for the whole session (setSerialWireMuted) so a
//    concurrent task's log line can't interleave with the binary protocol's
//    0x06 ACKs / payload. Logs still go to the RTC ring buffer.
//  - The protocol is driven from loop() (no dedicated RTOS task): when the
//    activity is up the reader isn't rendering, so a single sequential task
//    handles serial + SD with no display/SD SPI contention.
//  - SerialTransferDevice reports per-operation status strings which are painted
//    on screen (the only user-visible / debug channel while logs are muted).
class SerialTransferActivity final : public Activity {
 public:
  explicit SerialTransferActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SerialTransfer", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  bool ownsSerialInput() const override { return true; }
  bool preventAutoSleep() override { return true; }
  bool skipLoopDelay() override { return true; }
  // Static screen; suppress the minute-tick refresh (it would hold the render
  // mutex and stall the serial loop, and the status text only changes on events).
  bool shouldSkipPeriodicUpdate() const override { return true; }

 private:
  static void statusThunk(void* ctx, const char* msg);
  void onStatus(const char* msg);

  SerialTransferDevice device_;
  std::string statusMessage_;
  bool statusDirty_ = false;  // a new status message is waiting to be painted (in loop())
};
