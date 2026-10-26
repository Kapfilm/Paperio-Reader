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
constexpr uint8_t MAX_ATTEMPTS = 3;

// Register reads and ordinary command writes are idempotent. Retry brief I2C
// faults locally; the total extra blocking per transaction is at most 4 ms.
bool readRetry(Bq27220Capacity::Bus& bus, uint8_t reg, uint16_t& word) {
  for (uint8_t i = 0; i < MAX_ATTEMPTS; ++i) {
    if (bus.read(reg, word)) return true;
    if (i + 1 < MAX_ATTEMPTS) bus.pause(2);
  }
  return false;
}
bool writeRetry(Bq27220Capacity::Bus& bus, uint8_t reg, uint16_t word) {
  for (uint8_t i = 0; i < MAX_ATTEMPTS; ++i) {
    if (bus.write(reg, word)) return true;
    if (i + 1 < MAX_ATTEMPTS) bus.pause(2);
  }
  return false;
}

uint8_t security(const uint16_t status) { return (status >> 1) & 0b11; }
uint16_t swapBytes(const uint16_t word) { return static_cast<uint16_t>((word << 8) | (word >> 8)); }
bool due(const uint32_t nowMs, const uint32_t atMs) { return static_cast<int32_t>(nowMs - atMs) >= 0; }
}  // namespace

void Bq27220Capacity::tick(Bus& bus, const uint32_t nowMs) {
  if (outcome != Result::Pending || !due(nowMs, nextAt)) return;

  uint16_t status = 0;
  switch (step) {
    case Step::Check:
      if (target == 0) {
        outcome = Result::NotNeeded;
        return;
      }
      if (!readRetry(bus, DESIGN_CAPACITY, dc) || !readRetry(bus, OPERATION_STATUS, status)) {
        if (++retries < MAX_ATTEMPTS)
          nextAt = nowMs + FLAG_POLL_MS;
        else
          outcome = Result::Failed;
        return;
      }
      retries = 0;
      inConfig = status & CFGUPDATE;
      cleanupNeeded = inConfig;
      if (dc == target) {
        wrote = true;
        if (inConfig || security(status) != SEC_SEALED) return finish(bus, nowMs, true);
        outcome = Result::NotNeeded;
      } else if (dc != TI_DEFAULT_MAH) {
        customCapacity = true;
        // Preserve factory/custom capacity, but never leave a configuration
        // session inherited from a reset running indefinitely.
        if (inConfig || security(status) != SEC_SEALED) return finish(bus, nowMs, true);
        outcome = Result::NotNeeded;
      } else {
        key = security(status) == SEC_SEALED ? 0 : security(status) == SEC_FULL_ACCESS ? 4 : 2;
        step = Step::Keys;
      }
      return;

    case Step::Keys:
      cleanupNeeded = true;
      if (key < 4) {
        // Security keys are an ordered pair, not an idempotent command. A
        // failed write may already have reached the gauge: do not resend one
        // half of the pair and silently change its sequence.
        if (!bus.write(CONTROL, KEYS[key++])) return finish(bus, nowMs, false);
        nextAt = nowMs + KEY_GAP_MS;
        return;
      }
      inConfig = true;
      if (!writeRetry(bus, CONTROL, ENTER_CFG_UPDATE)) return finish(bus, nowMs, false);
      return wait(Step::WaitEnter, nowMs);

    case Step::WaitEnter:
    case Step::WaitExit: {
      const bool entering = step == Step::WaitEnter;
      const bool read = readRetry(bus, OPERATION_STATUS, status);
      const bool reached = read && static_cast<bool>(status & CFGUPDATE) == entering;
      if (!reached && !due(nowMs, sentAt + FLAG_GIVE_UP_MS)) {
        nextAt = nowMs + FLAG_POLL_MS;
        return;
      }
      if (!entering) {
        if (!reached) {
          // Never seal while CONFIG UPDATE may still be active. Retain
          // inConfig so abandon() can make a fresh bounded cleanup attempt.
          failed = true;
          outcome = Result::Failed;
          return;
        }
        inConfig = false;
        return seal(bus, nowMs);
      }
      return finish(bus, nowMs,
                    reached && writeParam(bus, DM_LEARNED_FULL_CHARGE_CAPACITY) && writeParam(bus, DM_DESIGN_CAPACITY));
    }
    case Step::Exit:
      if (writeRetry(bus, CONTROL, wrote ? EXIT_CFG_UPDATE_REINIT : EXIT_CFG_UPDATE)) {
        retries = 0;
        return wait(Step::WaitExit, nowMs);
      }
      failed = true;
      if (++retries < MAX_ATTEMPTS)
        nextAt = nowMs + FLAG_POLL_MS;
      else
        outcome = Result::Failed;
      return;
    case Step::Seal:
      return seal(bus, nowMs);
    case Step::VerifySeal:
      if (readRetry(bus, OPERATION_STATUS, status) && !(status & CFGUPDATE) && security(status) == SEC_SEALED) {
        cleanupNeeded = false;
        if (readRetry(bus, DESIGN_CAPACITY, dc)) {
          // Retry an interrupted security/entry sequence only after verified
          // cleanup. Never resend a lone key or restart after partial DM writes.
          if (failed && !wrote && dc == TI_DEFAULT_MAH && ++restarts < MAX_ATTEMPTS) {
            failed = false;
            step = Step::Check;
            retries = 0;
            nextAt = nowMs + FLAG_POLL_MS;
            return;
          }
          outcome = failed || (!customCapacity && dc != target) ? Result::Failed
                    : customCapacity                            ? Result::NotNeeded
                                                                : Result::Loaded;
          return;
        }
      }
      if (!due(nowMs, sentAt + FLAG_GIVE_UP_MS))
        nextAt = nowMs + FLAG_POLL_MS;
      else
        outcome = Result::Failed;
      return;
  }
}

bool Bq27220Capacity::writeParam(Bus& bus, const uint16_t address) {
  uint16_t old = 0;
  uint16_t sumAndLength = 0;
  if (!writeRetry(bus, MAC_CONTROL, address)) return false;
  bus.pause(SELECT_SETTLE_MS);
  if (!readRetry(bus, MAC_DATA, old) || !readRetry(bus, MAC_DATA_SUM, sumAndLength)) return false;
  if (swapBytes(old) != TI_DEFAULT_MAH) return true;

  wrote = true;
  const uint8_t sum = static_cast<uint8_t>(sumAndLength + (old & 0xFF) + (old >> 8) - (target & 0xFF) - (target >> 8));
  if (!writeRetry(bus, MAC_CONTROL, address)) return false;
  bus.pause(SELECT_SETTLE_MS);
  return writeRetry(bus, MAC_DATA, swapBytes(target)) && writeRetry(bus, MAC_DATA_SUM, (sumAndLength & 0xFF00) | sum);
}

void Bq27220Capacity::wait(const Step next, const uint32_t nowMs) {
  step = next;
  sentAt = nowMs;
  nextAt = nowMs + FLAG_FIRST_READ_MS;
}

void Bq27220Capacity::finish(Bus& bus, const uint32_t nowMs, const bool ok) {
  failed |= !ok;
  if (inConfig) {
    step = Step::Exit;
    retries = 0;
    nextAt = nowMs;
    return;
  }
  seal(bus, nowMs);
}

void Bq27220Capacity::seal(Bus& bus, const uint32_t nowMs) {
  cleanupNeeded = true;
  if (writeRetry(bus, CONTROL, SEALED)) {
    retries = 0;
    return wait(Step::VerifySeal, nowMs);
  }
  failed = true;
  step = Step::Seal;
  if (++retries < MAX_ATTEMPTS)
    nextAt = nowMs + FLAG_POLL_MS;
  else
    outcome = Result::Failed;
}

bool Bq27220Capacity::abandon(Bus& bus) {
  // A gauge that was never modified, or whose cleanup was already verified,
  // must not block normal sleep just because it is currently unreachable.
  if (!cleanupNeeded) return true;
  // Do not trust local state after a failed command: read the actual gauge.
  // TI SLUUBD4A section 6.1 requires confirming CFGUPDATE clear before sealing.
  // A single deadline bounds the entire cleanup, including transient faults.
  bool exitSent = false;
  bool sealSent = false;
  for (uint32_t elapsed = 0; elapsed <= FLAG_GIVE_UP_MS; elapsed += 100) {
    uint16_t status = 0;
    if (readRetry(bus, OPERATION_STATUS, status)) {
      if (status & CFGUPDATE) {
        sealSent = false;
        if (!exitSent) exitSent = writeRetry(bus, CONTROL, wrote ? EXIT_CFG_UPDATE_REINIT : EXIT_CFG_UPDATE);
      } else if (security(status) == SEC_SEALED) {
        inConfig = false;
        cleanupNeeded = false;
        outcome = Result::Failed;  // cancelled, not a completed capacity load
        return true;
      } else if (!sealSent) {
        sealSent = writeRetry(bus, CONTROL, SEALED);
      }
    }
    if (elapsed != FLAG_GIVE_UP_MS) bus.pause(100);
  }
  outcome = Result::Failed;
  return false;
}
