#include "HalPowerManager.h"

#include <Logging.h>
#include <PowerManager.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include <algorithm>
#include <cassert>
#include <cstdlib>

#include "HalGPIO.h"

HalPowerManager powerManager;  // Singleton instance

namespace {
constexpr char BATTERY_NVS_NAMESPACE[] = "cpbattery";
constexpr char X4_PERCENT_NVS_KEY[] = "x4_pct";
constexpr uint8_t X4_PERCENT_UNKNOWN = 0xFF;
// X4 has no coulomb counter, only the cell-voltage ADC. Its 650 mAh cell and
// TP4056-family charger use a constant-current/constant-voltage profile: the
// middle of the charge is relatively quick and the last few percent taper.
// Keep tenths internally so the displayed value follows that profile instead
// of either jumping to 100% from charge-raised voltage or lagging far behind it.
constexpr uint32_t X4_CHARGE_FAST_TENTH_MS = 4000;  // 1.5 displayed %/min below 85%
// Hardware tests bracketed the useful upper-charge rate: v2 reached 100%
// before the charger LED turned green, while v3 was still at 92% when it did.
// The midpoint calibration then reached 100% 3-4 minutes before green in three
// repeat tests. Keep 85-95 unchanged and add about 3.3 minutes to the final 5%.
constexpr uint32_t X4_CHARGE_MID_TENTH_MS = 4000;    // 1.5 displayed %/min from 85-95%
constexpr uint32_t X4_CHARGE_TAPER_TENTH_MS = 12000;  // 0.5 displayed %/min above 95%

// Once USB is removed, terminal voltage is usable again, but only after the
// immediate charging surface voltage has relaxed. Reconcile a stale persisted
// estimate for a short bounded window; afterwards use a much slower rate so
// normal load/temperature voltage movement cannot make the percentage jump.
constexpr uint32_t X4_UNPLUG_SETTLE_MS = 2UL * 60UL * 1000UL;
constexpr uint32_t X4_UNPLUG_RECONCILE_MS = 12UL * 60UL * 1000UL;
constexpr uint32_t X4_RECONCILE_TENTH_MS = 2000;     // at most 3 displayed %/min
constexpr uint32_t X4_DISCHARGE_TENTH_MS = 30000;    // at most 1 displayed %/5 min
// A confirmed full X4 rests near the old curve's 89% point after charge
// voltage disappears. Hold the just-completed 100% briefly, then spend that
// upper reserve at the normal discharge limiter instead of the fast unplug
// reconciliation rate. This changes only the calibrated 100-89% band.
constexpr uint32_t X4_FULL_UNPLUG_HOLD_MS = 60UL * 60UL * 1000UL;

uint32_t x4StepDurationMs(const int cachedTenths, const bool usbConnected,
                          const uint32_t unpluggedForMs) {
  if (usbConnected) {
    if (cachedTenths < 850) return X4_CHARGE_FAST_TENTH_MS;
    if (cachedTenths < 950) return X4_CHARGE_MID_TENTH_MS;
    return X4_CHARGE_TAPER_TENTH_MS;
  }
  if (unpluggedForMs >= X4_UNPLUG_SETTLE_MS &&
      unpluggedForMs < (X4_UNPLUG_SETTLE_MS + X4_UNPLUG_RECONCILE_MS)) {
    return X4_RECONCILE_TENTH_MS;
  }
  return X4_DISCHARGE_TENTH_MS;
}

int readPersistedX4Percent() {
  Preferences prefs;
  if (!prefs.begin(BATTERY_NVS_NAMESPACE, true)) return -1;
  const uint8_t value = prefs.getUChar(X4_PERCENT_NVS_KEY, X4_PERCENT_UNKNOWN);
  prefs.end();
  return value <= 100 ? value : -1;
}

void writePersistedX4Percent(const uint8_t percent) {
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
    LOG_INF("PWR", "X3 gauge capacity loaded, DesignCapacity %u mAh",
            static_cast<unsigned>(load.designCapacity()));
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
  } else {
    pinMode(BAT_GPIO0, INPUT);
    _x4LastPersistedPercent = readPersistedX4Percent();
    if (_x4LastPersistedPercent >= 0) {
      _batteryCachedPercent = _x4LastPersistedPercent * 10;
      _batterySeeded = true;
      _x4FullCycle = _x4LastPersistedPercent == 100;
    }
  }
  gaugeCapacity = Bq27220Capacity(_batteryUseI2C ? X3_BATTERY_MAH : 0);
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

void HalPowerManager::abandonGaugeCapacityLoad() {
  if (!_batteryUseI2C || !isGaugeCapacityLoadPending()) return;
  X3GaugeBus bus;
  gaugeCapacity.abandon(bus);
  logGaugeCapacity(gaugeCapacity);
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
  // The sleep screen is rendered with the current cached X4 estimate. Persist
  // that exact whole-percent value before power is cut so wake cannot restore
  // an older periodic NVS checkpoint (for example 87% after sleep showed 95%).
  if (!_batteryUseI2C && _batterySeeded) {
    const int percent = std::clamp(_batteryCachedPercent / 10, 0, 100);
    if (percent != _x4LastPersistedPercent) {
      writePersistedX4Percent(static_cast<uint8_t>(percent));
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

void HalPowerManager::pollBatteryPercentage() const {
  // Guard against an X3 board mistakenly taking the ADC path: BAT_GPIO0 is
  // reused as X3_I2C_SCL on X3, so reading it as ADC would collide with the
  // fuel-gauge bus. _batteryUseI2C must match the detected device type.
  assert(_batteryUseI2C == gpio.deviceIsX3());
  const unsigned long now = millis();
  if (_batteryLastPollMs != 0 && (now - _batteryLastPollMs) < BATTERY_POLL_MS) return;

  if (_batteryUseI2C) {
    // Read SOC from the I2C fuel gauge via the shared helper so the transaction
    // shape stays consistent with other BQ27220/DS3231/QMI8658 reads.
    // On I2C error, keep last known value to avoid UI jitter/slowdowns.
    uint16_t soc = 0;
    if (X3GPIO::readI2CReg16LE(I2C_ADDR_BQ27220, BQ27220_SOC_REG, &soc)) {
      _batteryCachedPercent = soc > 100 ? 100 : soc;
    }
    _batteryLastPollMs = now;
    return;
  }
  static const BatteryMonitor battery = BatteryMonitor(BAT_GPIO0);

  const uint16_t millivolts = battery.readMillivolts();
  const uint16_t sample = std::min<uint16_t>(100, BatteryMonitor::percentageFromMillivolts(millivolts));
  const bool usbConnected = gpio.isUsbConnected();

  // Seed from the first real battery sample when no persisted estimate exists.
  // Normally the NVS value loaded in begin() is used, which prevents reboot or
  // deep sleep from discarding the filter state.
  if (!_batterySeeded) {
    _batteryCachedPercent = 10 * sample;
    _batterySeeded = true;
    _x4LastFilterMs = now;
    _x4LastPersistedPercent = sample;
    writePersistedX4Percent(static_cast<uint8_t>(sample));
    _x4LastPersistMs = now;
  }

  if (!_x4UsbStateKnown) {
    _x4UsbStateKnown = true;
    _x4LastUsbConnected = usbConnected;
    _x4LastFilterMs = now;
    if (!usbConnected) {
      _x4UnpluggedAtMs = now;
      if (_x4FullCycle && _batteryCachedPercent >= 1000) _x4FullUnpluggedAtMs = now;
    }
  }

  const bool usbChanged = usbConnected != _x4LastUsbConnected;
  if (usbChanged) {
    _x4LastUsbConnected = usbConnected;
    _x4RateBudgetMs = 0;
    _x4LastFilterMs = now;
    _x4UnpluggedAtMs = usbConnected ? 0 : now;
    if (usbConnected) {
      // This is a new charge cycle. It becomes calibrated only if it actually
      // reaches 100%; unplugging at 99% or below keeps the ordinary ADC path.
      _x4FullCycle = false;
      _x4FullUnpluggedAtMs = 0;
    } else if (_x4FullCycle && _batteryCachedPercent >= 1000) {
      _x4FullUnpluggedAtMs = now;
    }
  }

  if (usbConnected && _batteryCachedPercent >= 1000) {
    _x4FullCycle = true;
  }

  // A plain X4 has no fuel gauge: while USB is connected its ADC measures the
  // charge-raised terminal voltage and can jump from e.g. 23% to 100% in a few
  // minutes. Follow it only at the physical CC/CV charge rate. After unplugging,
  // briefly hold the visible value while surface voltage settles, then allow a
  // bounded faster reconciliation in either direction. This repairs estimates
  // inherited from an older build without turning every transient ADC change
  // into a visible percentage jump.
  if (_x4LastFilterMs == 0) _x4LastFilterMs = now;
  const uint32_t elapsedMs = now - _x4LastFilterMs;
  _x4LastFilterMs = now;
  _x4RateBudgetMs = std::min<uint32_t>(_x4RateBudgetMs + elapsedMs, 30UL * 60UL * 1000UL);

  int target = static_cast<int>(sample) * 10;
  if (usbConnected && target < _batteryCachedPercent) target = _batteryCachedPercent;
  const uint32_t unpluggedForMs = !usbConnected && _x4UnpluggedAtMs != 0 ? now - _x4UnpluggedAtMs : 0;
  if (!usbConnected && unpluggedForMs < X4_UNPLUG_SETTLE_MS) target = _batteryCachedPercent;
  const bool fullHold = !usbConnected && _x4FullCycle && _x4FullUnpluggedAtMs != 0 &&
                        (now - _x4FullUnpluggedAtMs) < X4_FULL_UNPLUG_HOLD_MS;
  if (fullHold) target = _batteryCachedPercent;
  // The X4 exposes neither charge current nor the red/green charger LED state
  // to the MCU. In particular, 4080 mV can be reached during the red CC/CV
  // phase, so voltage must never force an immediate 100%. CrossPoint likewise
  // only smooths the ADC estimate. Keep our time-based limiter all the way to
  // full so the final 8% advances gradually instead of jumping 92% -> 100%.
  // After the hold, a completed full cycle may move toward the relaxed ADC
  // value, but only at the normal discharge rate. The generic post-unplug
  // reconciliation is intentionally bypassed for this one upper-band case.
  const uint32_t stepDurationMs = !usbConnected && _x4FullCycle
                                      ? X4_DISCHARGE_TENTH_MS
                                      : x4StepDurationMs(_batteryCachedPercent, usbConnected, unpluggedForMs);
  const uint32_t allowedTenths = _x4RateBudgetMs / stepDurationMs;
  if (allowedTenths > 0 && target != _batteryCachedPercent) {
    const int delta = target - _batteryCachedPercent;
    const int step = std::min<int>(std::abs(delta), static_cast<int>(allowedTenths));
    _batteryCachedPercent += delta > 0 ? step : -step;
    _x4RateBudgetMs -= static_cast<uint32_t>(step) * stepDurationMs;
  } else {
    // Don't accumulate an unlimited future jump while the raw value already
    // agrees with the estimate.
    if (target == _batteryCachedPercent) _x4RateBudgetMs = 0;
  }

  // The calibrated reserve has been consumed once the display reaches the X4
  // ADC's observed post-charge resting point. Below it, use the normal curve.
  if (!usbConnected && _x4FullCycle && _batteryCachedPercent <= 890) {
    _x4FullCycle = false;
    _x4FullUnpluggedAtMs = 0;
  }

  const int percent = std::clamp(_batteryCachedPercent / 10, 0, 100);
  constexpr uint32_t X4_PERSIST_INTERVAL_MS = 10UL * 60UL * 1000UL;
  const bool persistDue = _x4LastPersistMs == 0 || (now - _x4LastPersistMs) >= X4_PERSIST_INTERVAL_MS;
  if ((usbChanged || persistDue) && percent != _x4LastPersistedPercent) {
    writePersistedX4Percent(static_cast<uint8_t>(percent));
    _x4LastPersistedPercent = percent;
    _x4LastPersistMs = now;
  }
  _batteryLastPollMs = now;
}

uint16_t HalPowerManager::getBatteryPercentage() const {
  // Early-boot fallback; during normal operation loop() keeps the cache fresh
  // so rendering never needs to start an ADC/I2C transaction.
  if (_batteryLastPollMs == 0) pollBatteryPercentage();
  if (_batteryUseI2C) return static_cast<uint16_t>(std::clamp(_batteryCachedPercent, 0, 100));
  return static_cast<uint16_t>(std::clamp(_batteryCachedPercent / 10, 0, 100));
}

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
