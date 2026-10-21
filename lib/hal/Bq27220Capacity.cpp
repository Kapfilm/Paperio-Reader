#include "Bq27220Capacity.h"

// BQ27220 Technical Reference Manual (SLUUBD4A), section 6.1. This is the
// sequence used by inkMOD, kept intentionally conservative: only gauges still
// at TI's known 3000 mAh default are changed. A value configured by the factory
// or another firmware is left untouched.
namespace {
constexpr uint8_t CONTROL = 0x00;
constexpr uint8_t OPERATION_STATUS = 0x3A;
constexpr uint8_t DESIGN_CAPACITY = 0x3C;
constexpr uint8_t MAC_CONTROL = 0x3E;
constexpr uint8_t MAC_DATA = 0x40;
constexpr uint8_t MAC_DATA_SUM = 0x60;
constexpr uint16_t CFGUPDATE = 1 << 10;
constexpr uint8_t SEC_SEALED = 0b11;
constexpr uint8_t SEC_FULL_ACCESS = 0b01;

constexpr uint16_t TI_DEFAULT_MAH = 3000;
constexpr uint16_t KEYS[] = {0x0414, 0x3672, 0xFFFF, 0xFFFF};
constexpr uint16_t SEALED = 0x0030;
constexpr uint16_t ENTER_CFG_UPDATE = 0x0090;
constexpr uint16_t EXIT_CFG_UPDATE_REINIT = 0x0091;
constexpr uint16_t EXIT_CFG_UPDATE = 0x0092;
constexpr uint16_t DM_LEARNED_FULL_CHARGE_CAPACITY = 0x929D;
constexpr uint16_t DM_DESIGN_CAPACITY = 0x929F;

constexpr uint32_t KEY_GAP_MS = 1500;
constexpr uint32_t SELECT_SETTLE_MS = 10;
constexpr uint32_t FLAG_FIRST_READ_MS = 2000;
constexpr uint32_t FLAG_POLL_MS = 500;
constexpr uint32_t FLAG_GIVE_UP_MS = 5000;

uint8_t security(const uint16_t status) { return (status >> 1) & 0b11; }
uint16_t swapBytes(const uint16_t word) { return static_cast<uint16_t>((word << 8) | (word >> 8)); }
bool due(const uint32_t nowMs, const uint32_t atMs) { return static_cast<int32_t>(nowMs - atMs) >= 0; }
}  // namespace

void Bq27220Capacity::tick(Bus& bus, const uint32_t nowMs) {
  if (outcome != Result::Pending || !due(nowMs, nextAt)) return;

  uint16_t status = 0;
  switch (step) {
    case Step::Check:
      if (target == 0 || !bus.read(DESIGN_CAPACITY, dc) || !bus.read(OPERATION_STATUS, status)) {
        outcome = target == 0 ? Result::NotNeeded : Result::Failed;
        return;
      }
      inConfig = status & CFGUPDATE;
      if (dc == target) {
        wrote = true;
        if (inConfig || security(status) != SEC_SEALED) return finish(bus, nowMs, true);
        outcome = Result::NotNeeded;
      } else if (dc != TI_DEFAULT_MAH) {
        outcome = Result::NotNeeded;
      } else {
        key = security(status) == SEC_SEALED ? 0 : security(status) == SEC_FULL_ACCESS ? 4 : 2;
        step = Step::Keys;
      }
      return;

    case Step::Keys:
      if (key < 4) {
        if (!bus.write(CONTROL, KEYS[key++])) return finish(bus, nowMs, false);
        nextAt = nowMs + KEY_GAP_MS;
        return;
      }
      inConfig = true;
      if (!bus.write(CONTROL, ENTER_CFG_UPDATE)) return finish(bus, nowMs, false);
      return wait(Step::WaitEnter, nowMs);

    case Step::WaitEnter:
    case Step::WaitExit: {
      const bool entering = step == Step::WaitEnter;
      const bool read = bus.read(OPERATION_STATUS, status);
      const bool reached = read && static_cast<bool>(status & CFGUPDATE) == entering;
      if (read && !reached && !due(nowMs, sentAt + FLAG_GIVE_UP_MS)) {
        nextAt = nowMs + FLAG_POLL_MS;
        return;
      }
      if (!entering) {
        failed |= !reached;
        return seal(bus);
      }
      return finish(bus, nowMs,
                    reached && writeParam(bus, DM_LEARNED_FULL_CHARGE_CAPACITY) &&
                        writeParam(bus, DM_DESIGN_CAPACITY));
    }
  }
}

bool Bq27220Capacity::writeParam(Bus& bus, const uint16_t address) {
  uint16_t old = 0;
  uint16_t sumAndLength = 0;
  if (!bus.write(MAC_CONTROL, address)) return false;
  bus.pause(SELECT_SETTLE_MS);
  if (!bus.read(MAC_DATA, old) || !bus.read(MAC_DATA_SUM, sumAndLength)) return false;
  if (swapBytes(old) != TI_DEFAULT_MAH) return true;

  wrote = true;
  const uint8_t sum =
      static_cast<uint8_t>(sumAndLength + (old & 0xFF) + (old >> 8) - (target & 0xFF) - (target >> 8));
  if (!bus.write(MAC_CONTROL, address)) return false;
  bus.pause(SELECT_SETTLE_MS);
  return bus.write(MAC_DATA, swapBytes(target)) && bus.write(MAC_DATA_SUM, (sumAndLength & 0xFF00) | sum);
}

void Bq27220Capacity::wait(const Step next, const uint32_t nowMs) {
  step = next;
  sentAt = nowMs;
  nextAt = nowMs + FLAG_FIRST_READ_MS;
}

void Bq27220Capacity::finish(Bus& bus, const uint32_t nowMs, const bool ok) {
  failed |= !ok;
  if (inConfig) {
    inConfig = false;
    if (bus.write(CONTROL, wrote ? EXIT_CFG_UPDATE_REINIT : EXIT_CFG_UPDATE)) {
      return wait(Step::WaitExit, nowMs);
    }
    failed = true;
  }
  seal(bus);
}

void Bq27220Capacity::seal(Bus& bus) {
  failed |= !bus.write(CONTROL, SEALED);
  if (!bus.read(DESIGN_CAPACITY, dc)) dc = 0;
  outcome = failed || dc != target ? Result::Failed : Result::Loaded;
}

void Bq27220Capacity::abandon(Bus& bus) {
  if (outcome != Result::Pending || step == Step::Check) return;
  failed = true;
  if (inConfig) bus.write(CONTROL, wrote ? EXIT_CFG_UPDATE_REINIT : EXIT_CFG_UPDATE);
  seal(bus);
}
