#pragma once

#include <cstdint>

// ESP.restart() with an RTC_NOINIT flag that survives the reboot, so setup()
// skips the boot splash and routes straight to a destination. Used to clear
// heap fragmentation accumulated during a wifi session — WiFi/LWIP/netif
// teardown scatters long-lived allocations across the heap, leaving ~50KB of
// contiguous space unrecoverable without a reboot.

// Silent-reboot destinations. The value is persisted in RTC_NOINIT across the
// restart and consumed by setup()'s routeSilentBootTarget(). Append new targets at
// the end — values 0..3 are historical and must not be renumbered. setup() also
// clamps the persisted value to the last enumerator, so any new target MUST be
// added here AND will then route automatically (see routeSilentBootTarget in main.cpp).
enum class SilentBootTarget : uint32_t {
  Home = 0,
  Reader = 1,          // currently-open EPUB (APP_STATE.openEpubPath)
  SerialTransfer = 2,  // USB serial file-transfer activity
  ClockSettings = 3,   // clock / time settings screen
  Settings = 4,
  FileBrowser = 5,
  RecentBooks = 6,
  GlobalBookmarks = 7,
  Browser = 8,  // OPDS browser
  FileTransfer = 9,
  Weather = 10,
  Last = Weather,  // keep in sync with the highest target above (boot-time clamp bound)
};

// Generic silent reboot into any target (does not return). Shared body behind the
// named wrappers below.
void silentRestartTo(SilentBootTarget target);

void silentRestart();                 // home screen
void silentRestartToReader();         // currently-open EPUB (APP_STATE.openEpubPath)
void silentRestartToClockSettings();  // clock / time settings screen

// Guarded silent reboot for HomeActivity's framebuffer-handover safety net: reboots
// straight into `target` so the next activity comes up on a clean heap with an intact
// secondary framebuffer. Single-shot per silent-boot chain (own RTC latch, cleared on a
// non-silent boot) so a persistent allocation failure can't reboot-loop. Returns false
// without rebooting if the latch is set or a deep sleep is in progress (caller then
// proceeds in degraded mode); otherwise does not return.
bool trySilentRestartForHomeHandover(SilentBootTarget target);

// One-shot guarded variant for heap-fragmentation recovery: allows only one
// restart attempt across consecutive silent boots until a non-silent boot
// clears the latch.
bool trySilentRestartToReaderForHeapRecovery();

// Arm/disarm a boot target that routes setup() straight back into the USB serial
// file-transfer activity. Unlike silentRestart*(), arm does NOT reboot — it only
// sets the RTC flag so the involuntary C3 USB-Serial/JTAG reset (triggered when a
// host opens the serial port) lands back in the activity instead of Home. The
// activity arms on entry and disarms on clean exit.
void armSerialTransferReboot();
void disarmSerialTransferReboot();
