#pragma once

#include <cstdint>

// Loads the real pack capacity into a BQ27220 that is still using TI's
// 3000 mAh default. Work is split into short tick() calls so boot and the main
// loop are never blocked for the several seconds required by CONFIG UPDATE.
class Bq27220Capacity {
 public:
  struct Bus {
    virtual bool write(uint8_t reg, uint16_t word) = 0;
    virtual bool read(uint8_t reg, uint16_t& word) = 0;
    virtual void pause(uint32_t ms) = 0;

   protected:
    ~Bus() = default;
  };

  enum class Result : uint8_t { Pending, NotNeeded, Loaded, Failed };

  explicit Bq27220Capacity(uint16_t targetMah = 0) : target(targetMah) {}

  void tick(Bus& bus, uint32_t nowMs);
  void abandon(Bus& bus);

  Result result() const { return outcome; }
  uint16_t designCapacity() const { return dc; }

 private:
  enum class Step : uint8_t { Check, Keys, WaitEnter, WaitExit };

  bool writeParam(Bus& bus, uint16_t address);
  void wait(Step next, uint32_t nowMs);
  void finish(Bus& bus, uint32_t nowMs, bool ok);
  void seal(Bus& bus);

  uint16_t target;
  Step step = Step::Check;
  Result outcome = Result::Pending;
  uint8_t key = 0;
  bool failed = false;
  bool inConfig = false;
  bool wrote = false;
  uint16_t dc = 0;
  uint32_t sentAt = 0;
  uint32_t nextAt = 0;
};
