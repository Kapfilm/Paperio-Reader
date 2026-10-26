#include "HalPowerManager.h"

#include <Logging.h>
#include <PowerManager.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include <algorithm>
#include <cassert>

#include "HalGPIO.h"
#include "X4BatteryDisplayScale.h"

HalPowerManager powerManager;  // Singleton instance

namespace {
constexpr char BATTERY_NVS_NAMESPACE[] = "cpbattery";
constexpr char X4_PERCENT_NVS_KEY[] = "x4_pct";

int readPersistedX4Percent() {
  Preferences prefs;
  if (!prefs.begin(BATTERY_NVS_NAMESPACE, true)) return -1;
  const uint8_t value = prefs.getUChar(X4_PERCENT_NVS_KEY, 0xFF);
  prefs.end();
  return value <= 100 ? value : -1;
}

void writePersistedX4Percent(uint8_t percent) {
  Preferences prefs;
  if (!prefs.begin(BATTERY_NVS_NAMESPACE, false)) return;
  prefs.putUChar(X4_PERCENT_NVS_KEY, percent);
  prefs.end();
}

class X3GaugeBus final : public Bq27220Capacity::Bus {
 public:
  bool write(const uint8_t reg, const uint16_t word) override {
    const uint8_t bytes[] = {reg, static_cast<uint8_t>(word & 0xFF), static_cast<uint8_t>(word >> 8)};
    Wire.beginTransmission(I2C_ADDR_BQ27220);
    Wire.write(bytes, sizeof(bytes));
    const bool ok = Wire.endTransmission() == 0;
    delayMicroseconds(66);
    return ok;
  }

  bool read(const uint8_t reg, uint16_t& word) override {
    Wire.beginTransmission(I2C_ADDR_BQ27220);
    Wire.write(reg);
    const bool ok = Wire.endTransmission(false) == 0 &&
                    Wire.requestFrom(I2C_ADDR_BQ27220, static_cast<uint8_t>(2), static_cast<uint8_t>(true)) == 2;
    if (ok) {
      word = static_cast<uint16_t>(Wire.read() & 0xFF);
      word |= static_cast<uint16_t>((Wire.read() & 0xFF) << 8);
    }
    delayMicroseconds(66);
    return ok;
  }

  void pause(const uint32_t ms) override { delay(ms); }
};

void logGaugeCapacity(const Bq27220Capacity& load) {
  if (load.result() == Bq27220Capacity::Result::Loaded) {
    LOG_INF("PWR", "X3 gauge capacity loaded, DesignCapacity %u mAh", static_cast<unsigned>(load.designCapacity()));
  } else if (load.result() == Bq27220Capacity::Result::Failed) {
    LOG_ERR("PWR", "X3 gauge capacity load failed, DesignCapacity %u mAh",
            static_cast<unsigned>(load.designCapacity()));
  }
}
}  // namespace

void HalPowerManager::begin() {
  if (gpio.deviceIsX3()) {
    // X3 uses an I2C fuel gauge for battery monitoring.
    // I2C init must come AFTER gpio.begin() so early hardware detection/probes are finished.
    Wire.begin(X3_I2C_SDA, X3_I2C_SCL, X3_I2C_FREQ);
    Wire.setTimeOut(4);
    _batteryUseI2C = true;
    gpio.pollPowerStatus();
  } else {
    pinMode(BAT_GPIO0, INPUT);
    _x4LastPersistedPercent = readPersistedX4Percent();
    x4Battery.restore(_x4LastPersistedPercent);
  }
  gaugeCapacity = Bq27220Capacity(_batteryUseI2C ? 650 : 0);
  pollBatteryPercentage();
  normalFreq = getCpuFrequencyMhz();
  modeMutex = xSemaphoreCreateMutex();
  assert(modeMutex != nullptr);
}

void HalPowerManager::loadGaugeCapacity() {
  if (!_batteryUseI2C) return;
  X3GaugeBus bus;
  gaugeCapacity.tick(bus, millis());
  logGaugeCapacity(gaugeCapacity);
}

bool HalPowerManager::abandonGaugeCapacityLoad() {
  if (!_batteryUseI2C) return true;
  X3GaugeBus bus;
  return gaugeCapacity.abandon(bus);
}

void HalPowerManager::setPowerSaving(bool enabled) {
  if (normalFreq <= 0) {
    return;  // invalid state
  }

  auto wifiMode = WiFi.getMode();
  if (wifiMode != WIFI_MODE_NULL) {
    // Wifi is active, force disabling power saving
    enabled = false;
  }

  // Relaxed atomic read: a slightly stale value is acceptable (the lock holder
  // that just won the race will re-call setPowerSaving anyway), but we want
  // defined semantics rather than relying on compiler behavior for a plain int.
  const LockMode mode = currentLockMode.load(std::memory_order_relaxed);

  if (mode == None && enabled && !isLowPower) {
    LOG_DBG("PWR", "Going to low-power mode");
    if (!setCpuFrequencyMhz(LOW_POWER_FREQ)) {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", LOW_POWER_FREQ);
      return;
    }
    isLowPower = true;

  } else if ((!enabled || mode != None) && isLowPower) {
    LOG_DBG("PWR", "Restoring normal CPU frequency");
    if (!setCpuFrequencyMhz(normalFreq)) {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", normalFreq);
      return;
    }
    isLowPower = false;
  }

  // Otherwise, no change needed
}

void HalPowerManager::ensureFullSpeedForRadio() {
  if (normalFreq <= 0) {
    return;  // begin() not called yet — nothing to restore to
  }
  // Clear BOTH downclock owners before touching the frequency. The idle governor
  // (isLowPower) and the waveform hook (waveformLowPower_) track their drops separately, and
  // whichever one is live would otherwise restore or re-drop the clock behind the radio's back.
  xSemaphoreTake(modeMutex, portMAX_DELAY);
  const bool wasIdleLow = isLowPower;
  const bool wasWaveformLow = waveformLowPower_;
  isLowPower = false;
  waveformLowPower_ = false;
  xSemaphoreGive(modeMutex);

  const int current = getCpuFrequencyMhz();
  if (current >= normalFreq) {
    return;  // already at full speed; nothing to do beyond the flag reconciliation above
  }
  if (setCpuFrequencyMhz(normalFreq)) {
    LOG_INF("PWR", "CPU %d -> %d MHz for radio bring-up (idleLow=%d waveformLow=%d)", current, normalFreq,
            wasIdleLow ? 1 : 0, wasWaveformLow ? 1 : 0);
  } else {
    // Worth an error rather than a debug line: WiFi association from a 10 MHz clock does not
    // fail cleanly, it hangs, and this is the only place that would have seen it coming.
    LOG_ERR("PWR", "Failed to restore %d MHz before radio bring-up (still %d MHz) — WiFi may hang", normalFreq,
            current);
  }
}

void HalPowerManager::enterWaveformWait() {
  if (normalFreq <= 0) {
    return;  // begin() not called yet — nothing to restore to
  }
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    return;  // WiFi requires the 80 MHz APB clock
  }
  xSemaphoreTake(modeMutex, portMAX_DELAY);
  // A NormalSpeed lock held by ANOTHER task means full-speed code is running
  // concurrently — don't downclock. Our own lock (the render task's per-render
  // Lock) is fine: this task is about to sleep and only resumes after
  // exitWaveformWait() restores the clock.
  const bool foreignLock =
      currentLockMode.load(std::memory_order_relaxed) != None && lockOwnerTask_ != xTaskGetCurrentTaskHandle();
  // Already at the low clock from idle power saving — leave ownership of the
  // restore with setPowerSaving(); don't double-track it here.
  if (!foreignLock && !isLowPower && setCpuFrequencyMhz(LOW_POWER_FREQ)) {
    waveformLowPower_ = true;
    LOG_DBG("PWR", "Waveform wait: CPU down to %d MHz", LOW_POWER_FREQ);
  }
  xSemaphoreGive(modeMutex);
}

void HalPowerManager::exitWaveformWait() {
  xSemaphoreTake(modeMutex, portMAX_DELAY);
  if (waveformLowPower_) {
    waveformLowPower_ = false;
    // If idle power saving engaged meanwhile (loop task), the low clock is now
    // intentional — keep it and let setPowerSaving(false) restore it later.
    if (!isLowPower) {
      setCpuFrequencyMhz(normalFreq);
      LOG_DBG("PWR", "Waveform wait: CPU restored to %d MHz", normalFreq);
    }
  }
  xSemaphoreGive(modeMutex);
}

void HalPowerManager::startDeepSleep(HalGPIO& gpio, bool keepClockAlive) const {
  if (!_batteryUseI2C && x4Battery.known()) {
    // Checkpoints are base estimates, never the adjusted display percentage.
    const uint16_t percent = x4Battery.percentage();
    if (percent != _x4LastPersistedPercent) {
      writePersistedX4Percent(percent);
      _x4LastPersistedPercent = percent;
    }
  }
  LOG_DBG("PWR", "startDeepSleep: isPressed=%d, rawPin=%d, keepClock=%d", gpio.isPressed(HalGPIO::BTN_POWER),
          digitalRead(InputManager::POWER_BUTTON_PIN) == LOW, keepClockAlive);

#ifdef ENABLE_SERIAL_LOG
  // Tear down HWCDC so the host sees a clean disconnect and the peripheral
  // doesn't hold power domains that interfere with USB-powered GPIO wake.
  // logSerial is the raw HWCDC reference; Serial is the MySerialImpl proxy
  // (which doesn't expose end()).
  logSerial.end();
#endif
  // Perform all hardware preparation immediately (while the button may still be held)
  // so the user gets instant visual feedback (display already off). Only block for
  // button release at the very end, right before entering sleep.
  // GPIO13 on X4 is the battery latch MOSFET.
  // When keepClockAlive is false (default): GPIO13 goes LOW, the MCU is
  // completely powered off during sleep (including the LP timer / RTC memory).
  // When keepClockAlive is true: GPIO13 stays HIGH, the MCU remains powered
  // at ~3-4 mA so the LP timer keeps running and RTC memory is preserved.
  // This allows HalClock to accurately compute elapsed sleep time on wake.
  //
  // X3 does NOT share this meaning: there GPIO13 is the SD rail's power enable
  // (active-high), declared as sd.powerEnable in the XTEINK_X3 board profiles.
  // X3 keeps time on a battery-backed DS3231 (I2C 0x68), so it never needs the
  // LP timer held alive across sleep — main.cpp forces keepLpAlive=false on X3
  // for exactly that reason. So on X3 we hand the pin to the SDK's rail teardown
  // (which cuts the card's power and latches it off) instead of treating it as a
  // latch; SDCardManager::begin() releases that hold and re-powers on the next
  // boot. Doing both would make us a second, independent writer of the pin.
  if (gpio.deviceIsX3()) {
    freeink::PowerManager::powerDownRailsForSleep();
    esp_sleep_config_gpio_isolate();
    gpio_deep_sleep_hold_en();
  } else {
    constexpr gpio_num_t GPIO_SPIWP = GPIO_NUM_13;
    // Release any GPIO hold from a previous sleep cycle (keepClockAlive=true leaves GPIO13 held after wake).
    // Without this, gpio_set_level() below silently fails and GPIO13 is stuck in its prior state,
    // causing the device to enter a sleep/wake loop that requires a hardware reset to escape.
    gpio_hold_dis(GPIO_SPIWP);
    gpio_deep_sleep_hold_dis();
    gpio_set_direction(GPIO_SPIWP, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_SPIWP, keepClockAlive ? 1 : 0);
    esp_sleep_config_gpio_isolate();
    gpio_deep_sleep_hold_en();
    gpio_hold_en(GPIO_SPIWP);
  }
  pinMode(InputManager::POWER_BUTTON_PIN, INPUT_PULLUP);

  // Now wait for the power button to be fully released before arming the wakeup
  // trigger and entering sleep — prevents immediate re-wake from a held button.
  gpio.waitForStablePowerRelease();

  // Arm the wakeup trigger *after* the button is released
  // Note: when keepClockAlive is false, this is only useful for waking up on USB power. On battery, the MCU will be
  // completely powered off, so the power button is hard-wired to briefly provide power to the MCU, waking it up
  // regardless of the wakeup source configuration.
  // When keepClockAlive is true, this is the actual wakeup mechanism since the MCU stays powered.
  esp_deep_sleep_enable_gpio_wakeup(1ULL << InputManager::POWER_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
  // Enter Deep Sleep
  esp_deep_sleep_start();
}

void HalPowerManager::pollBatteryPercentage() {
  assert(_batteryUseI2C == gpio.deviceIsX3());
  const uint32_t now = millis();
  if (_batteryPolled && now - _batteryLastPollMs < BATTERY_POLL_MS) return;
  _batteryPolled = true;
  _batteryLastPollMs = now;
  if (_batteryUseI2C) {
    uint16_t soc = 0;
    if (X3GPIO::readI2CReg16LE(I2C_ADDR_BQ27220, BQ27220_SOC_REG, &soc) && soc <= 100) {
      _batteryCachedPercent.store(soc, std::memory_order_relaxed);
    }
    return;
  }

  static const BatteryMonitor battery(BAT_GPIO0);
  // Median rejects isolated ADC glitches without sharing mutable filter state
  // with the rendering task. Sampling and NVS writes only happen on loop().
  uint16_t samples[5];
  for (auto& sample : samples) sample = battery.readMillivolts();
  std::sort(std::begin(samples), std::end(samples));
  const uint16_t mv = samples[2];
  const uint16_t rawPercent = BatteryMonitor::percentageFromMillivolts(mv);
  const bool usb = gpio.isUsbConnected();
  // No proven charge-complete GPIO on the stock X4. The rate-limited endpoint
  // may reach an estimated 100%, independently of the hardware charge LED.
  const bool valid = x4Battery.update(now, mv, rawPercent, usb);
  const uint16_t percent =
      valid ? X4BatteryDisplayScale::percent(x4Battery.percentageTenths()) : getBatteryPercentage();
  batteryCalibrationLog.record(now, mv, rawPercent, percent, usb);
  if (!valid) return;
  const uint16_t basePercent = x4Battery.percentage();
  _batteryCachedPercent.store(percent, std::memory_order_relaxed);
  const bool usbChanged = usb != _batteryLastUsb;
  _batteryLastUsb = usb;
  if ((usbChanged || now - _x4LastPersistMs >= 600000) && basePercent != _x4LastPersistedPercent) {
    writePersistedX4Percent(basePercent);
    _x4LastPersistedPercent = basePercent;
    _x4LastPersistMs = now;
  }
  if (usbChanged || now - _batteryLastLogMs >= 60000) {
    LOG_INF("BAT", "X4 mv=%u raw=%u base=%u estimate=%u usb=%d fullSignal=unavailable", mv, rawPercent, basePercent,
            percent, usb ? 1 : 0);
    _batteryLastLogMs = now;
  }
}

uint16_t HalPowerManager::getBatteryPercentage() const { return _batteryCachedPercent.load(std::memory_order_relaxed); }

HalPowerManager::Lock::Lock() {
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  // Current limitation: only one lock at a time
  if (powerManager.currentLockMode.load(std::memory_order_relaxed) != None) {
    LOG_ERR("PWR", "Lock already held, ignore");
    valid = false;
  } else {
    powerManager.currentLockMode.store(NormalSpeed, std::memory_order_relaxed);
    powerManager.lockOwnerTask_ = xTaskGetCurrentTaskHandle();
    valid = true;
  }
  xSemaphoreGive(powerManager.modeMutex);
  if (valid) {
    // Immediately restore normal CPU frequency if currently in low-power mode
    powerManager.setPowerSaving(false);
  }
}

HalPowerManager::Lock::~Lock() {
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  if (valid) {
    powerManager.currentLockMode.store(None, std::memory_order_relaxed);
    powerManager.lockOwnerTask_ = nullptr;
  }
  xSemaphoreGive(powerManager.modeMutex);
}
