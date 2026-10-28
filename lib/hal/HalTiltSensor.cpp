#include "HalTiltSensor.h"

#include <Logging.h>

HalTiltSensor halTiltSensor;

bool HalTiltSensor::writeReg(uint8_t reg, uint8_t val) const {
  Wire.beginTransmission(_i2cAddr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool HalTiltSensor::readReg(uint8_t reg, uint8_t* val) const {
  Wire.beginTransmission(_i2cAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  Wire.requestFrom(_i2cAddr, (uint8_t)1);
  if (Wire.available() < 1) {
    return false;
  }
  *val = Wire.read();
  return true;
}

bool HalTiltSensor::readGyro(float& gx, float& gy, float& gz) const {
  Wire.beginTransmission(_i2cAddr);
  Wire.write(REG_GX_L);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  Wire.requestFrom(_i2cAddr, (uint8_t)6);
  if (Wire.available() < 6) {
    return false;
  }

  auto readInt16 = [&]() -> int16_t {
    const uint8_t lo = Wire.read();
    const uint8_t hi = Wire.read();
    return static_cast<int16_t>((hi << 8) | lo);
  };

  constexpr float SCALE = 1.0f / 64.0f;  // ±512 dps full scale: 32768 / 512 = 64 LSB/dps
  gx = readInt16() * SCALE;
  gy = readInt16() * SCALE;
  gz = readInt16() * SCALE;
  return true;
}

void HalTiltSensor::begin() {
  if (!gpio.deviceIsX3()) {
    _available = false;
    return;
  }

  uint8_t whoami = 0;
  _i2cAddr = I2C_ADDR_QMI8658;
  if (!readReg(QMI8658_WHO_AM_I_REG, &whoami) || whoami != QMI8658_WHO_AM_I_VALUE) {
    _i2cAddr = I2C_ADDR_QMI8658_ALT;
    if (!readReg(QMI8658_WHO_AM_I_REG, &whoami) || whoami != QMI8658_WHO_AM_I_VALUE) {
      LOG_ERR("GYR", "QMI8658 IMU not found");
      _available = false;
      return;
    }
  }

  LOG_INF("GYR", "QMI8658 IMU found at 0x%02X", _i2cAddr);

  if (!writeReg(REG_CTRL7, CTRL7_DISABLE_ALL) || !writeReg(REG_CTRL3, CTRL3_FS_512DPS | CTRL3_ODR_28HZ) ||
      !writeReg(REG_CTRL1, CTRL1_BASE | CTRL1_SENSOR_DISABLE)) {
    LOG_ERR("GYR", "QMI8658 register configuration failed");
    _available = false;
    return;
  }

  _available = true;
  _initMs = millis();
  _sleepMs = millis();
  _lastPollMs = millis();
  LOG_INF("GYR", "QMI8658 gyro initialized and put to sleep");
}

bool HalTiltSensor::wake() {
  if (!_available) {
    return false;
  }

  if ((millis() - _sleepMs) < SLEEP_STABILIZE_MS) {
    return false;
  }

  if (writeReg(REG_CTRL1, CTRL1_BASE) && writeReg(REG_CTRL7, CTRL7_GYRO_ENABLE)) {
    _lastPollMs = millis();
    _lastTiltMs = millis();
    _wakeMs = millis();
    _gesture.reset(_wakeMs);
    LOG_INF("GYR", "QMI8658 woke up");
    return true;
  } else {
    LOG_ERR("GYR", "Failed to wake QMI8658");
    return false;
  }
}

bool HalTiltSensor::deepSleep() {
  if (!_available) {
    return false;
  }

  if ((millis() - _wakeMs) < SLEEP_STABILIZE_MS) {
    return false;
  }

  if (writeReg(REG_CTRL7, CTRL7_DISABLE_ALL) && writeReg(REG_CTRL1, CTRL1_BASE | CTRL1_SENSOR_DISABLE)) {
    clearPendingEvents();
    _gesture.reset(millis());
    _sleepMs = millis();
    LOG_INF("GYR", "QMI8658 entered sleep mode");
    return true;
  } else {
    LOG_ERR("GYR", "Failed to put QMI8658 to sleep");
    return false;
  }
}

void HalTiltSensor::update(CrossPointTiltPageTurn::Value mode, CrossPointOrientation::Value orientation, bool inReader,
                           uint8_t direction) {
  if (!_available) {
    return;
  }

  if ((mode != CrossPointTiltPageTurn::TILT_OFF && inReader) && !_isAwake) {
    _isAwake = wake();
    return;
  } else if ((mode == CrossPointTiltPageTurn::TILT_OFF || !inReader) && _isAwake) {
    _isAwake = !deepSleep();
    return;
  }

  if ((mode == CrossPointTiltPageTurn::TILT_OFF) || !inReader) {
    return;
  }

  const unsigned long now = millis();
  if ((now - _wakeMs) < WAKE_STABILIZE_MS) {
    return;
  }

  if ((now - _lastPollMs) < POLL_INTERVAL_MS) {
    return;
  }
  _lastPollMs = now;

  float gx, gy, gz;
  if (!readGyro(gx, gy, gz)) {
    return;
  }

  if (_lastDirection != direction || _lastOrientation != static_cast<uint8_t>(orientation)) {
    clearPendingEvents();
    _gesture.reset(now);
    _lastDirection = direction;
    _lastOrientation = static_cast<uint8_t>(orientation);
    return;
  }
  const float rate = tilt_gesture::axis(gx, gy, static_cast<uint8_t>(orientation), direction);
  const int event = _gesture.update(rate, now);
  if (event != 0) {
    _tiltForwardEvent = event > 0;
    _tiltBackEvent = event < 0;
    _hadActivity = true;
    LOG_INF("GYR", "Tilt direction=%u event=%d rate=%.1f dps", direction, event, rate);
  }
}

bool HalTiltSensor::wasTiltedForward() {
  const bool val = _tiltForwardEvent;
  _tiltForwardEvent = false;
  return val;
}

bool HalTiltSensor::wasTiltedBack() {
  const bool val = _tiltBackEvent;
  _tiltBackEvent = false;
  return val;
}

bool HalTiltSensor::hadActivity() {
  const bool val = _hadActivity;
  _hadActivity = false;
  return val;
}

void HalTiltSensor::clearPendingEvents() {
  _tiltForwardEvent = false;
  _tiltBackEvent = false;
  _hadActivity = false;
}
