#include <cstdlib>
#include <iostream>
#include <vector>

#include "Bq27220Capacity.h"
#include "X3ChargeStatus.h"

#define CHECK(value)                             \
  do {                                           \
    if (!(value)) {                              \
      std::cerr << __LINE__ << ": " #value "\n"; \
      std::exit(1);                              \
    }                                            \
  } while (false)

namespace {
constexpr uint16_t CFG = 1 << 10;
constexpr uint16_t SEALED = 6;
uint16_t swap(uint16_t value) { return static_cast<uint16_t>((value << 8) | (value >> 8)); }

struct Gauge : Bq27220Capacity::Bus {
  uint16_t dc = 3000;
  uint16_t learned = 3000;
  uint16_t status = SEALED;
  uint16_t selected = 0;
  uint16_t pendingValue = 0;
  int readFailures = 0;
  int keyFailures = 0;
  bool unavailable = false;
  bool stuckConfig = false;
  bool badSeal = false;
  int configReadFailures = 0;
  int calls = 0;
  int pauses = 0;
  int unsealKeys = 0;
  int fullKeys = 0;
  int seals = 0;
  bool sealedDuringConfig = false;
  std::vector<uint16_t> commands;

  bool write(uint8_t reg, uint16_t word) override {
    ++calls;
    if (unavailable) return false;
    if (reg == 0) {
      commands.push_back(word);
      if (word == 0x0414 || word == 0x3672 || word == 0xffff) {
        if (keyFailures > 0) {
          --keyFailures;
          return false;
        }
        if (word == 0x0414) unsealKeys = 1;
        if (word == 0x3672 && unsealKeys == 1) status = 4;
        if (word == 0xffff && ++fullKeys == 2) status = 2;
      } else if (word == 0x90) {
        status |= CFG;
      } else if (word == 0x91 || word == 0x92) {
        if (!stuckConfig) status &= ~CFG;
      } else if (word == 0x30) {
        ++seals;
        sealedDuringConfig |= (status & CFG) != 0;
        if (!badSeal) status = SEALED;
        unsealKeys = fullKeys = 0;
      }
    } else if (reg == 0x3e)
      selected = word;
    else if (reg == 0x40)
      pendingValue = swap(word);
    else if (reg == 0x60) {
      if (selected == 0x929f) dc = pendingValue;
      if (selected == 0x929d) learned = pendingValue;
    }
    return true;
  }
  bool read(uint8_t reg, uint16_t& word) override {
    ++calls;
    if (unavailable) return false;
    if (readFailures > 0) {
      --readFailures;
      return false;
    }
    if (reg == 0x3a && (status & CFG) && configReadFailures > 0) {
      --configReadFailures;
      return false;
    }
    if (reg == 0x3c)
      word = dc;
    else if (reg == 0x3a)
      word = status;
    else if (reg == 0x40)
      word = swap(selected == 0x929f ? dc : learned);
    else if (reg == 0x60)
      word = 0x2488;
    else
      return false;
    return true;
  }
  void pause(uint32_t ms) override { pauses += static_cast<int>(ms); }
};

void run(Bq27220Capacity& load, Gauge& gauge, uint32_t limit = 40000) {
  for (uint32_t now = 0; now <= limit && load.result() == Bq27220Capacity::Result::Pending; now += 100) {
    load.tick(gauge, now);
  }
  CHECK(load.result() != Bq27220Capacity::Result::Pending);
}

void testUnmodifiedSleep() {
  Gauge gauge;
  Bq27220Capacity load(650);
  gauge.unavailable = true;
  CHECK(load.abandon(gauge));
  CHECK(gauge.calls == 0);
  run(load, gauge);
  CHECK(load.result() == Bq27220Capacity::Result::Failed);
  int before = gauge.calls;
  CHECK(load.abandon(gauge));
  CHECK(gauge.calls == before);
}

void testNormalAndTransientReads() {
  for (int failures : {0, 1, 3, 6}) {
    Gauge gauge;
    gauge.readFailures = failures;
    Bq27220Capacity load(650);
    run(load, gauge);
    CHECK(load.result() == Bq27220Capacity::Result::Loaded);
    CHECK(gauge.dc == 650 && gauge.learned == 650);
    CHECK(gauge.status == SEALED);
    CHECK(!gauge.sealedDuringConfig);
    gauge.unavailable = true;
    int before = gauge.calls;
    CHECK(load.abandon(gauge));
    CHECK(gauge.calls == before);
  }
}

void testCustomAndDisabled() {
  Gauge gauge;
  gauge.dc = 800;
  Bq27220Capacity load(650);
  run(load, gauge);
  CHECK(load.result() == Bq27220Capacity::Result::NotNeeded);
  CHECK(gauge.commands.empty());
  Bq27220Capacity disabled;
  run(disabled, gauge);
  CHECK(disabled.result() == Bq27220Capacity::Result::NotNeeded);

  Gauge interrupted;
  interrupted.dc = 800;
  interrupted.status = CFG | 2;
  Bq27220Capacity custom(650);
  run(custom, interrupted);
  CHECK(custom.result() == Bq27220Capacity::Result::NotNeeded);
  CHECK(interrupted.dc == 800 && interrupted.learned == 3000);
  CHECK(interrupted.status == SEALED);
  CHECK(!interrupted.sealedDuringConfig);
}

void testKeySequenceRetry() {
  Gauge gauge;
  gauge.keyFailures = 1;
  Bq27220Capacity load(650);
  run(load, gauge);
  CHECK(load.result() == Bq27220Capacity::Result::Loaded);
  CHECK(gauge.seals == 2);  // failed sequence cleaned before retry
  CHECK(!gauge.sealedDuringConfig);
}

void testPollingTransientFault() {
  Gauge gauge;
  gauge.configReadFailures = 3;
  Bq27220Capacity load(650);
  run(load, gauge);
  CHECK(load.result() == Bq27220Capacity::Result::Loaded);
}

void testAbandonDuringConfig() {
  Gauge gauge;
  Bq27220Capacity load(650);
  for (uint32_t now = 0; !(gauge.status & CFG) && now < 10000; now += 100) load.tick(gauge, now);
  CHECK(gauge.status & CFG);
  CHECK(load.abandon(gauge));
  CHECK(gauge.status == SEALED);
  CHECK(!gauge.sealedDuringConfig);
  CHECK(load.result() == Bq27220Capacity::Result::Failed);
}

void testStuckConfigBlocksSleep() {
  Gauge gauge;
  gauge.stuckConfig = true;
  Bq27220Capacity load(650);
  run(load, gauge);
  CHECK(load.result() == Bq27220Capacity::Result::Failed);
  CHECK(gauge.seals == 0);
  CHECK(!load.abandon(gauge));
  CHECK(gauge.seals == 0);
  CHECK(gauge.pauses < 6000);
  gauge.stuckConfig = false;
  CHECK(load.abandon(gauge));
  CHECK(gauge.status == SEALED);
}

void testSealMustBeVerified() {
  Gauge gauge;
  gauge.badSeal = true;
  Bq27220Capacity load(650);
  run(load, gauge);
  CHECK(load.result() == Bq27220Capacity::Result::Failed);
  CHECK(!load.abandon(gauge));
  gauge.badSeal = false;
  CHECK(load.abandon(gauge));
}

void testChargeCurrentCache() {
  X3ChargeStatus status;
  CHECK(status.pollDue(0));
  CHECK(!status.chargingKnown(0));
  status.record(true, 6, 0);
  CHECK(status.charging(0));
  CHECK(!status.pollDue(999));
  CHECK(status.pollDue(1000));
  status.record(false, 0, 1000);
  CHECK(status.charging(1000));  // a brief I2C fault does not synthesize unplug
  CHECK(!status.chargingKnown(3000));
  CHECK(!status.charging(3000));
  for (int16_t current : {int16_t(0), int16_t(5), int16_t(-20)}) {
    status.record(true, current, 4000);
    CHECK(status.chargingKnown(4000));
    CHECK(!status.charging(4000));
  }
  status.record(true, 100, UINT32_MAX - 500);
  CHECK(!status.pollDue(100));
  CHECK(status.charging(100));
  CHECK(status.pollDue(600));
}
}  // namespace

int main() {
  testUnmodifiedSleep();
  testNormalAndTransientReads();
  testCustomAndDisabled();
  testKeySequenceRetry();
  testPollingTransientFault();
  testAbandonDuringConfig();
  testStuckConfigBlocksSleep();
  testSealMustBeVerified();
  testChargeCurrentCache();
  std::cout << "X3 battery: 9 scenarios passed\n";
}
