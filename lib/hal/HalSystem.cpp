#include "HalSystem.h"

#include <string>

#include "Arduino.h"
#include "HalStorage.h"
#include "Logging.h"
#include "PanicStackSnapshot.h"
#include "esp_debug_helpers.h"
#include "esp_memory_utils.h"
#include "esp_private/esp_cpu_internal.h"
#include "esp_private/esp_system_attr.h"
#include "esp_private/panic_internal.h"

#define MAX_PANIC_STACK_DEPTH 32

RTC_NOINIT_ATTR char panicMessage[256];
RTC_NOINIT_ATTR HalSystem::StackFrame panicStack[MAX_PANIC_STACK_DEPTH];
RTC_NOINIT_ATTR uint32_t panicRegisters[5];  // MEPC, RA, SP, MCAUSE, MTVAL
RTC_NOINIT_ATTR uint32_t panicRegistersMagic;
constexpr uint32_t PANIC_REGISTERS_MAGIC = 0x50465231;


extern "C" {

void __real_panic_abort(const char* message);
void __real_panic_print_backtrace(const void* frame, int core);

static DRAM_ATTR const char PANIC_REASON_UNKNOWN[] = "(unknown panic reason)";
void IRAM_ATTR __wrap_panic_abort(const char* message) {
  if (!message) message = PANIC_REASON_UNKNOWN;
  // IRAM-safe bounded copy (strncpy is not IRAM-safe in panic context)
  int i = 0;
  for (; i < (int)sizeof(panicMessage) - 1 && message[i]; i++) {
    panicMessage[i] = message[i];
  }
  panicMessage[i] = '\0';

  __real_panic_abort(message);
}

void IRAM_ATTR __wrap_panic_print_backtrace(const void* frame, int core) {
  if (!frame) {
    __real_panic_print_backtrace(frame, core);
    return;
  }
  const auto* registers = static_cast<const RvExcFrame*>(frame);
  panicRegistersMagic = 0;
  panicRegisters[0] = static_cast<uint32_t>(registers->mepc);
  panicRegisters[1] = static_cast<uint32_t>(registers->ra);
  panicRegisters[2] = static_cast<uint32_t>(registers->sp);
  panicRegisters[3] = static_cast<uint32_t>(registers->mcause);
  panicRegisters[4] = static_cast<uint32_t>(registers->mtval);
  panicRegistersMagic = PANIC_REGISTERS_MAGIC;

  // Stack-pointer alignment is checked only for the original SP. A line's
  // end address need only be readable RAM; it is not a stack pointer itself.
  panic_snapshot::capture(
      panicRegisters[2], panicStack, MAX_PANIC_STACK_DEPTH, [](const uint32_t sp) { return esp_stack_ptr_is_sane(sp); },
      [](const uint32_t address) {
        const auto* byte = reinterpret_cast<const void*>(static_cast<uintptr_t>(address));
        return esp_ptr_in_dram(byte)
#if CONFIG_ESP_SYSTEM_ALLOW_RTC_FAST_MEM_AS_HEAP
               || esp_ptr_in_rtc_dram_fast(byte)
#endif
            ;
      },
      [](const uint32_t address) {
        return *reinterpret_cast<const volatile uint32_t*>(static_cast<uintptr_t>(address));
      });

  __real_panic_print_backtrace(frame, core);
}
}

namespace HalSystem {

void begin() {
  // This is mostly for the first boot, we need to initialize the panic info and logs to empty state
  // If we reboot from a panic state, we want to keep the panic info until we successfully dump it to the SD card, use
  // `clearPanic()` to clear it after dumping
  if (!isRebootFromPanic()) {
    clearPanic();
  } else {
    // Panic reboot: preserve logs and panic info, but clamp logHead in case the
    // panic occurred before begin() ever ran (e.g. in a static constructor).
    // If logHead was out of range, logMessages is also garbage — clear it so
    // getLastLogs() does not dump corrupt data into the crash report.
    if (sanitizeLogHead()) {
      clearLastLogs();
    }
  }
}

void checkPanic() {
  if (isRebootFromPanic()) {
    auto panicInfo = getPanicInfo(true);
    auto file = Storage.open("/crash_report.txt", O_WRITE | O_CREAT | O_TRUNC);
    if (file) {
      file.write(panicInfo.c_str(), panicInfo.size());
      file.close();
      LOG_INF("SYS", "Dumped panic info to SD card");
    } else {
      LOG_ERR("SYS", "Failed to open crash_report.txt for writing");
    }
  }
}

void clearPanic() {
  panicMessage[0] = '\0';
  panicRegistersMagic = 0;
  for (size_t i = 0; i < MAX_PANIC_STACK_DEPTH; i++) {
    panicStack[i].sp = 0;
  }
  clearLastLogs();
}

std::string getPanicInfo(bool full) {
  if (!full) {
    return panicMessage;
  } else {
    std::string info;

    info += "CrossPoint version: " CROSSPOINT_VERSION;
    info += "\nReset reason (esp_reset_reason): " + std::to_string(static_cast<int>(esp_reset_reason()));
    panicMessage[sizeof(panicMessage) - 1] = '\0';
    info += "\n\nPanic reason: " + std::string(panicMessage[0] ? panicMessage : "not captured; see reset reason/registers");
    if (panicRegistersMagic == PANIC_REGISTERS_MAGIC) {
      char registers[160];
      snprintf(registers, sizeof(registers),
               "\nMEPC=0x%08lX RA=0x%08lX SP=0x%08lX MCAUSE=0x%08lX MTVAL=0x%08lX\n",
               static_cast<unsigned long>(panicRegisters[0]), static_cast<unsigned long>(panicRegisters[1]),
               static_cast<unsigned long>(panicRegisters[2]), static_cast<unsigned long>(panicRegisters[3]),
               static_cast<unsigned long>(panicRegisters[4]));
      info += registers;
    }
    info += "\n\nLast logs:\n" + getLastLogs();
    info += "\n\nRaw stack memory (not a validated backtrace):\n";

    auto toHex = [](uint32_t value) {
      char buffer[9];
      snprintf(buffer, sizeof(buffer), "%08X", value);
      return std::string(buffer);
    };
    for (size_t i = 0; i < MAX_PANIC_STACK_DEPTH; i++) {
      if (panicStack[i].sp == 0) {
        break;
      }
      info += "0x" + toHex(panicStack[i].sp) + ": ";
      for (size_t j = 0; j < 8; j++) {
        info += "0x" + toHex(panicStack[i].spp[j]) + " ";
      }
      info += "\n";
    }

    return info;
  }
}

bool isRebootFromPanic() {
  const auto resetReason = esp_reset_reason();
  // Watchdog resets count as panics here: both watchdogs panic on timeout in this
  // build (CONFIG_ESP_TASK_WDT_PANIC, CONFIG_ESP_INT_WDT), so they preserve the same
  // panic info and deserve the same crash report. Without these a wedged task just
  // rebooted silently to Home with no diagnostic trail.
  return resetReason == ESP_RST_PANIC || resetReason == ESP_RST_CPU_LOCKUP || resetReason == ESP_RST_INT_WDT ||
         resetReason == ESP_RST_TASK_WDT || resetReason == ESP_RST_WDT;
}

}  // namespace HalSystem
