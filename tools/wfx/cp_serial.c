// CrossPoint serial transport — see cp_serial.h. Mirrors tools/serial_transfer.py.
#ifndef _WIN32
#define _DEFAULT_SOURCE  // clock_gettime, cfmakeraw, TIOCM_* on glibc
#endif
#include "cp_serial.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
// clang-format off
#include <windows.h>   // must come first — setupapi.h depends on its types
#include <setupapi.h>
// clang-format on
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

#define CHUNK 2048  // transfer chunk (ACK-paced both directions; matches protocol)
#define ACK 0x06

struct CpSerial {
#ifdef _WIN32
  HANDLE h;
#else
  int fd;
#endif
  char err[256];
  // Path prefix prepended to every device path. Empty for Witchhunt (uses "/")
  // as root directly. Set to "/sdcard" for MicroReader, which mounts the SD
  // card at that VFS path and rejects bare "/" as a root.
  char path_prefix[32];
  // Download mode for CMND 'T': 0 = unsupported, 1 = ACK-paced (both
  // Witchhunt and MicroReader use the same 0x06-per-chunk protocol).
  int download_supported;
};

static void set_err(CpSerial* s, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(s->err, sizeof(s->err), fmt, ap);
  va_end(ap);
}
const char* cp_last_error(CpSerial* s) { return s ? s->err : "null"; }
int cp_download_supported(CpSerial* s) { return s ? s->download_supported : 0; }

// --- CRC32 (zlib/IEEE) ------------------------------------------------------
static uint32_t crc32_update(uint32_t crc, const uint8_t* d, size_t n) {
  crc = ~crc;
  for (size_t i = 0; i < n; i++) {
    crc ^= d[i];
    for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));
  }
  return ~crc;
}

static uint64_t now_ms(void) {
#ifdef _WIN32
  return GetTickCount64();
#else
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
#endif
}

// --- platform raw byte I/O --------------------------------------------------
// Returns bytes read (0..n) within timeout_ms; -1 on hard error.
static int port_read(CpSerial* s, uint8_t* buf, size_t n, int timeout_ms) {
  size_t got = 0;
  uint64_t deadline = now_ms() + (uint64_t)timeout_ms;
  while (got < n) {
    int remaining = (int)(deadline - now_ms());
    if (remaining <= 0) break;
#ifdef _WIN32
    COMMTIMEOUTS to = {0};
    to.ReadIntervalTimeout = MAXDWORD;
    to.ReadTotalTimeoutConstant = (DWORD)remaining;
    SetCommTimeouts(s->h, &to);
    DWORD rd = 0;
    if (!ReadFile(s->h, buf + got, (DWORD)(n - got), &rd, NULL)) return -1;
    if (rd == 0) break;  // timed out this pass
    got += rd;
#else
    fd_set rf;
    FD_ZERO(&rf);
    FD_SET(s->fd, &rf);
    struct timeval tv = {remaining / 1000, (remaining % 1000) * 1000};
    int r = select(s->fd + 1, &rf, NULL, NULL, &tv);
    if (r < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    if (r == 0) break;
    ssize_t k = read(s->fd, buf + got, n - got);
    if (k < 0) {
      if (errno == EINTR || errno == EAGAIN) continue;
      return -1;
    }
    if (k == 0) break;
    got += (size_t)k;
#endif
  }
  return (int)got;
}

static int port_write(CpSerial* s, const void* buf, size_t n) {
  const uint8_t* p = (const uint8_t*)buf;
  size_t sent = 0;
  while (sent < n) {
#ifdef _WIN32
    DWORD wr = 0;
    if (!WriteFile(s->h, p + sent, (DWORD)(n - sent), &wr, NULL)) return -1;
    sent += wr;
#else
    ssize_t k = write(s->fd, p + sent, n - sent);
    if (k < 0) {
      if (errno == EINTR || errno == EAGAIN) continue;
      return -1;
    }
    sent += (size_t)k;
#endif
  }
  return 0;
}

static void port_flush_in(CpSerial* s) {
#ifdef _WIN32
  PurgeComm(s->h, PURGE_RXCLEAR);
#else
  tcflush(s->fd, TCIFLUSH);
#endif
}

// Open a local file given a UTF-8 path. On Windows fopen() uses the ANSI code
// page (lossy for Unicode), so convert to UTF-16 and use _wfopen; POSIX fopen
// already takes UTF-8.
static FILE* cp_fopen(const char* utf8_path, const char* mode) {
#ifdef _WIN32
  wchar_t wpath[1024], wmode[8];
  if (MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, wpath, 1024) == 0) return NULL;
  MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, 8);
  return _wfopen(wpath, wmode);
#else
  return fopen(utf8_path, mode);
#endif
}

// --- protocol helpers -------------------------------------------------------
static int read_exact(CpSerial* s, uint8_t* buf, size_t n, int timeout_ms) {
  int got = port_read(s, buf, n, timeout_ms);
  if (got < 0) {
    set_err(s, "read error");
    return -1;
  }
  if ((size_t)got != n) {
    set_err(s, "timeout: got %d of %zu bytes", got, n);
    return -1;
  }
  return 0;
}

// Protocol tokens that can appear mid-line when ESP log output runs directly
// into a protocol response without an intervening newline (MicroReader
// behaviour: logging is not suppressed on the wire).
//
// Two categories:
//   kProtoPrefix  — tokens that always have content after them (colon/pipe
//                   anchored), safe to match as substrings.
//   kProtoWhole   — tokens that are the *entire* line when genuine ("OK",
//                   "END", "READY"). Only strip to these if nothing follows
//                   them, to avoid matching "OK)" or "ENDpoint" in log text.
static const char* const kProtoPrefix[] = {"STATUS:", "DIR:", "BOOKS:", "READY:", "ERR:", "d|", "f|", NULL};
static const char* const kProtoWhole[] = {"OK", "END", "READY", NULL};

// Strip any leading log noise from a line. Modifies in place.
static void strip_log_noise(char* line) {
  size_t len = strlen(line);
  size_t best = len;  // index of earliest protocol token found

  // Prefix tokens: safe to match anywhere.
  for (int t = 0; kProtoPrefix[t]; t++) {
    char* hit = strstr(line, kProtoPrefix[t]);
    if (hit && (size_t)(hit - line) < best) best = (size_t)(hit - line);
  }

  // Whole-line tokens: only valid if the match runs to the end of the string.
  for (int t = 0; kProtoWhole[t]; t++) {
    size_t tlen = strlen(kProtoWhole[t]);
    char* hit = strstr(line, kProtoWhole[t]);
    if (hit && (hit - line) + tlen == len) {  // nothing after token
      size_t pos = (size_t)(hit - line);
      if (pos < best) best = pos;
    }
  }

  if (best > 0) memmove(line, line + best, len - best + 1);
}

// Read a '\n'-terminated line (without CR/LF) into out. Returns 0 on success.
static int read_line(CpSerial* s, char* out, size_t cap, int timeout_ms) {
  size_t i = 0;
  uint64_t deadline = now_ms() + (uint64_t)timeout_ms;
  for (;;) {
    int remaining = (int)(deadline - now_ms());
    if (remaining <= 0) {
      set_err(s, "line read timeout");
      return -1;
    }
    uint8_t c;
    int r = port_read(s, &c, 1, remaining);
    if (r < 0) return -1;
    if (r == 0) {
      set_err(s, "line read timeout");
      return -1;
    }
    if (c == '\n') break;
    if (c != '\r' && i + 1 < cap) out[i++] = (char)c;
  }
  out[i] = '\0';
  strip_log_noise(out);
  return 0;
}

// Read bytes until `prefix` appears in the stream, then copy from the prefix
// to the next '\n' into out. Works even when ESP log lines arrive on the same
// wire without a newline separator before the response (MicroReader behaviour).
static int read_until(CpSerial* s, const char* prefix, char* out, size_t cap, int timeout_ms) {
  uint64_t deadline = now_ms() + (uint64_t)timeout_ms;
  // Sliding window: accumulate bytes until we see the prefix.
  char window[600];
  size_t wlen = 0;
  for (;;) {
    int remaining = (int)(deadline - now_ms());
    if (remaining <= 0) {
      set_err(s, "no '%s' in stream", prefix);
      return -1;
    }
    uint8_t c;
    int r = port_read(s, &c, 1, remaining);
    if (r < 0) {
      set_err(s, "read error");
      return -1;
    }
    if (r == 0) {
      set_err(s, "no '%s' in stream", prefix);
      return -1;
    }
    if (c == '\r') continue;
    if (wlen < sizeof(window) - 1) window[wlen++] = (char)c;
    window[wlen] = '\0';
    // Check if the prefix appears anywhere in the accumulated window.
    char* hit = strstr(window, prefix);
    if (c == '\n') {
      // End of a line. If prefix was found on this line, extract and return it.
      if (hit) {
        // Copy from hit to the end of the window (excluding the trailing '\n').
        size_t li = wlen - (size_t)(hit - window);
        // Remove trailing '\n' that we just stored.
        if (li > 0 && window[wlen - 1] == '\n') li--;
        char line[600];
        if (li >= sizeof(line)) li = sizeof(line) - 1;
        memcpy(line, hit, li);
        line[li] = '\0';
        if (out) snprintf(out, cap, "%s", line);
        return 0;
      }
      // No prefix on this line; reset window for the next line.
      wlen = 0;
      continue;
    }
    if (!hit) continue;
    // Prefix found mid-line: read the rest of the line.
    size_t o = (size_t)(wlen - (size_t)(hit - window));
    char line[600];
    memcpy(line, hit, o);
    size_t li = o;
    while (li < sizeof(line) - 1) {
      remaining = (int)(deadline - now_ms());
      if (remaining <= 0) break;
      if (port_read(s, &c, 1, remaining) <= 0) break;
      if (c == '\n') break;
      if (c != '\r') line[li++] = (char)c;
    }
    line[li] = '\0';
    if (out) snprintf(out, cap, "%s", line);
    return 0;
  }
}

// Expect an "OK" line; treat "ERR:" (or anything else) as failure.
static int expect_ok(CpSerial* s, const char* what, int timeout_ms) {
  char line[600];
  uint64_t deadline = now_ms() + (uint64_t)timeout_ms;
  for (;;) {
    int remaining = (int)(deadline - now_ms());
    if (remaining <= 0) {
      set_err(s, "%s: no reply", what);
      return -1;
    }
    if (read_line(s, line, sizeof(line), remaining) != 0) return -1;
    if (line[0] == '\0') continue;
    if (strcmp(line, "OK") == 0) return 0;
    set_err(s, "%s: %s", what, line);
    return -1;
  }
}

static void put_u16(uint8_t* b, uint16_t v) {
  b[0] = v & 0xFF;
  b[1] = (v >> 8) & 0xFF;
}
static void put_u32(uint8_t* b, uint32_t v) {
  b[0] = v & 0xFF;
  b[1] = (v >> 8) & 0xFF;
  b[2] = (v >> 16) & 0xFF;
  b[3] = (v >> 24) & 0xFF;
}
static uint32_t get_u32(const uint8_t* b) {
  return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

// Send "CMND" + opcode + a length-prefixed path (path may be NULL for none).
// The struct's path_prefix is prepended to non-NULL paths so MicroReader
// (/sdcard root) and Witchhunt (/ root) both see the right VFS paths.
static int send_cmd_path(CpSerial* s, char op, const char* path) {
  uint8_t hdr[7];
  memcpy(hdr, "CMND", 4);
  hdr[4] = (uint8_t)op;
  size_t hlen = 5;
  if (path) {
    size_t prelen = strlen(s->path_prefix);
    size_t pathlen = strlen(path);
    // Avoid double-slash: don't prepend prefix if path already starts with it.
    if (prelen && strncmp(path, s->path_prefix, prelen) == 0) prelen = 0;
    put_u16(hdr + 5, (uint16_t)(prelen + pathlen));
    hlen = 7;
    if (port_write(s, hdr, hlen) != 0) return -1;
    if (prelen && port_write(s, s->path_prefix, prelen) != 0) return -1;
    if (port_write(s, path, pathlen) != 0) return -1;
    return 0;
  }
  if (port_write(s, hdr, hlen) != 0) return -1;
  return 0;
}

// --- open / autodetect ------------------------------------------------------

static void sleep_ms(int ms) {
#ifdef _WIN32
  Sleep((DWORD)ms);
#else
  struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
  nanosleep(&ts, NULL);
#endif
}

static int wait_ready(CpSerial* s, int timeout_ms) {
  // Give the USB link a moment to settle before the first probe. Our own
  // firmware resets on port-open and takes ~2 s to come back, so this short
  // pause is invisible. MicroReader (usb_serial_jtag, no reset) needs it
  // because the host-side driver may not yet have completed enumeration.
  sleep_ms(100);

  uint64_t deadline = now_ms() + (uint64_t)timeout_ms;
  while (now_ms() < deadline) {
    port_flush_in(s);
    if (send_cmd_path(s, 'S', NULL) == 0) {
      char line[600];
      if (read_until(s, "STATUS:", line, sizeof(line), 2000) == 0) {
        // Detect firmware from STATUS payload to set capabilities.
        // MicroReader:  STATUS:free=N,largest=N  -> SD at /sdcard, no download
        // Witchhunt:    STATUS:free=N min=N ...  -> SD is root /, download ok
        if (strstr(line, "largest=")) {
          // MicroReader: SD at /sdcard. Uses the same ACK-paced download
          // protocol as Witchhunt (both send data in CHUNK-byte blocks gated
          // by 0x06 ACKs from the host).
          snprintf(s->path_prefix, sizeof(s->path_prefix), "/sdcard");
          s->download_supported = 1;
        } else {
          // Witchhunt: SD is root /, ACK-paced download.
          s->download_supported = 1;
        }
        return 0;
      }
    }
    // Brief pause before retrying so a slow response that arrived just after
    // the flush is not flushed again on the very next iteration.
    sleep_ms(50);
  }
  set_err(s, "device not responding (is the USB Transfer screen open?)");
  return -1;
}

#ifndef _WIN32
// ESP32-C3 native USB Serial/JTAG identity, used to pick the right port among
// any other serial gadgets that may be plugged in.
#define CP_ESP_VID "303a"
#define CP_ESP_PID "1001"

static int read_first_line(const char* path, char* out, size_t cap) {
  FILE* f = fopen(path, "r");
  if (!f) return -1;
  char* r = fgets(out, (int)cap, f);
  fclose(f);
  if (!r) return -1;
  out[strcspn(out, "\r\n")] = '\0';
  return 0;
}

// Walk up from /sys/class/tty/<name>/device to the USB device dir (the one with
// idVendor) and check it matches the Espressif VID:PID. Returns 1 on match.
static int tty_is_espressif(const char* name) {
  char link[300], real[PATH_MAX];
  snprintf(link, sizeof(link), "/sys/class/tty/%s/device", name);
  if (!realpath(link, real)) return 0;
  for (int depth = 0; depth < 6; depth++) {
    char path[PATH_MAX + 16], v[16], p[16];
    snprintf(path, sizeof(path), "%s/idVendor", real);
    if (read_first_line(path, v, sizeof(v)) == 0) {
      snprintf(path, sizeof(path), "%s/idProduct", real);
      if (read_first_line(path, p, sizeof(p)) != 0) return 0;
      return strcasecmp(v, CP_ESP_VID) == 0 && strcasecmp(p, CP_ESP_PID) == 0;
    }
    char* slash = strrchr(real, '/');  // up one level
    if (!slash || slash == real) break;
    *slash = '\0';
  }
  return 0;
}

// Find the reader's tty by USB VID:PID. Returns 0 and fills out on success.
static int find_espressif_port(char* out, size_t cap) {
  DIR* d = opendir("/sys/class/tty");
  if (!d) return -1;
  struct dirent* e;
  int rc = -1;
  while ((e = readdir(d))) {
    if (strncmp(e->d_name, "ttyACM", 6) != 0 && strncmp(e->d_name, "ttyUSB", 6) != 0) continue;
    if (tty_is_espressif(e->d_name)) {
      snprintf(out, cap, "/dev/%s", e->d_name);
      rc = 0;
      break;
    }
  }
  closedir(d);
  return rc;
}

static int posix_open(CpSerial* s, const char* port) {
  s->fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (s->fd < 0) {
    set_err(s, "open %s: %s", port, strerror(errno));
    return -1;
  }
  struct termios t;
  if (tcgetattr(s->fd, &t) != 0) {
    set_err(s, "tcgetattr: %s", strerror(errno));
    return -1;
  }
  cfmakeraw(&t);
  cfsetispeed(&t, B115200);
  cfsetospeed(&t, B115200);
  t.c_cflag |= (CLOCAL | CREAD);
  t.c_cflag &= ~HUPCL;  // don't reset on close
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 0;
  tcsetattr(s->fd, TCSANOW, &t);
  int bits;
  if (ioctl(s->fd, TIOCMGET, &bits) == 0) {
    bits &= ~(TIOCM_DTR | TIOCM_RTS);  // deassert (matches the python tool)
    ioctl(s->fd, TIOCMSET, &bits);
  }
  return 0;
}
#endif

CpSerial* cp_open(const char* port) {
  CpSerial* s = (CpSerial*)calloc(1, sizeof(CpSerial));
  if (!s) return NULL;
  char auto_port[280] = {0};

#ifdef _WIN32
  s->h = INVALID_HANDLE_VALUE;
  char name[32];
  const char* env = getenv("CROSSPOINT_PORT");  // explicit override wins
  if (env && env[0]) port = env;
  if (!port || !port[0]) {
    // Use SetupAPI to find the ESP32-C3 (VID 303A, PID 1001) COM port.
    // Walk all COM-port devices, check their hardware-ID against the known
    // VID:PID, then read PortName from the device's registry key.
    GUID guid_comport = {0x86E0D1E0L, 0x8089, 0x11D0, {0x9C, 0xE4, 0x08, 0x00, 0x3E, 0x30, 0x1F, 0x73}};
    HDEVINFO devs = SetupDiGetClassDevsA(&guid_comport, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devs != INVALID_HANDLE_VALUE) {
      SP_DEVINFO_DATA di;
      di.cbSize = sizeof(di);
      for (DWORD idx = 0; SetupDiEnumDeviceInfo(devs, idx, &di); idx++) {
        char hwid[512] = {0};
        // Hardware ID looks like: USB\VID_303A&PID_1001&MI_00\...
        if (!SetupDiGetDeviceRegistryPropertyA(devs, &di, SPDRP_HARDWAREID, NULL, (BYTE*)hwid, sizeof(hwid) - 1, NULL))
          continue;
        // Case-insensitive substring match on VID_303A and PID_1001.
        char lo[512];
        for (int k = 0; hwid[k]; k++) lo[k] = (hwid[k] >= 'A' && hwid[k] <= 'Z') ? hwid[k] + 32 : hwid[k];
        lo[strlen(hwid)] = '\0';
        if (!strstr(lo, "vid_303a") || !strstr(lo, "pid_1001")) continue;
        // Matched: read the COM port name from the device's Parameters key.
        HKEY hk = SetupDiOpenDevRegKey(devs, &di, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (hk == INVALID_HANDLE_VALUE) continue;
        char portname[32] = {0};
        DWORD sz = sizeof(portname) - 1, type;
        if (RegQueryValueExA(hk, "PortName", NULL, &type, (BYTE*)portname, &sz) == ERROR_SUCCESS && portname[0])
          snprintf(auto_port, sizeof(auto_port), "%s", portname);
        RegCloseKey(hk);
        if (auto_port[0]) break;
      }
      SetupDiDestroyDeviceInfoList(devs);
    }
    // Fallback: scan COM1-COM32 if SetupAPI found nothing.
    if (!auto_port[0]) {
      for (int i = 1; i <= 32 && !auto_port[0]; i++) {
        snprintf(name, sizeof(name), "\\\\.\\COM%d", i);
        HANDLE h = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
          CloseHandle(h);
          snprintf(auto_port, sizeof(auto_port), "COM%d", i);
        }
      }
    }
    port = auto_port[0] ? auto_port : "COM3";
  }
  snprintf(name, sizeof(name), "\\\\.\\%s", port);
  s->h = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
  if (s->h == INVALID_HANDLE_VALUE) {
    set_err(s, "open %s failed", port);
    return s;  // caller checks cp_last_error via a NULL-ish path below
  }
  DCB dcb = {0};
  dcb.DCBlength = sizeof(dcb);
  GetCommState(s->h, &dcb);
  dcb.BaudRate = CBR_115200;
  dcb.ByteSize = 8;
  dcb.Parity = NOPARITY;
  dcb.StopBits = ONESTOPBIT;
  dcb.fBinary = TRUE;
  dcb.fDtrControl = DTR_CONTROL_DISABLE;
  dcb.fRtsControl = RTS_CONTROL_DISABLE;
  SetCommState(s->h, &dcb);
#else
  if (!port || !port[0]) {
    const char* env = getenv("CROSSPOINT_PORT");  // explicit override wins
    if (env && env[0]) {
      port = env;
    } else if (find_espressif_port(auto_port, sizeof(auto_port)) == 0) {
      port = auto_port;  // matched the reader by USB VID:PID
    } else {
      // Fallback: first ttyACM*/ttyUSB* in /dev.
      const char* bases[] = {"ttyACM", "ttyUSB"};
      DIR* d = opendir("/dev");
      struct dirent* e;
      if (d) {
        while ((e = readdir(d)) && !auto_port[0]) {
          for (int g = 0; g < 2; g++)
            if (strncmp(e->d_name, bases[g], strlen(bases[g])) == 0) {
              snprintf(auto_port, sizeof(auto_port), "/dev/%s", e->d_name);
              break;
            }
        }
        closedir(d);
      }
      port = auto_port[0] ? auto_port : "/dev/ttyACM0";
    }
  }
  if (posix_open(s, port) != 0) return s;  // err already set
#endif

  if (wait_ready(s, 25000) != 0) {
    cp_close(s);
    return NULL;
  }
  return s;
}

void cp_close(CpSerial* s) {
  if (!s) return;
#ifdef _WIN32
  if (s->h != INVALID_HANDLE_VALUE) CloseHandle(s->h);
#else
  if (s->fd >= 0) close(s->fd);
#endif
  free(s);
}

// --- operations -------------------------------------------------------------
int cp_status(CpSerial* s, char* out, size_t out_len) {
  port_flush_in(s);
  if (send_cmd_path(s, 'S', NULL) != 0) return -1;
  char line[600];
  if (read_until(s, "STATUS:", line, sizeof(line), 5000) != 0) return -1;
  snprintf(out, out_len, "%s", line + strlen("STATUS:"));
  return 0;
}

int cp_list_dir(CpSerial* s, const char* path, CpEntryCb cb, void* user) {
  port_flush_in(s);
  if (send_cmd_path(s, 'A', path) != 0) return -1;
  char line[600];
  if (read_until(s, "DIR:", line, sizeof(line), 5000) != 0) return -1;  // ERR:* surfaces as timeout
  for (;;) {
    if (read_line(s, line, sizeof(line), 5000) != 0) return -1;
    if (strcmp(line, "END") == 0) break;
    if (line[0] == '\0') continue;
    CpEntry e;
    memset(&e, 0, sizeof(e));
    if (line[0] == 'd' && line[1] == '|') {
      e.is_dir = 1;
      snprintf(e.name, sizeof(e.name), "%s", line + 2);
    } else if (line[0] == 'f' && line[1] == '|') {
      // f|name|size|mtime  (name may not contain '|')
      char* p = line + 2;
      char* bar1 = strchr(p, '|');
      if (!bar1) continue;
      *bar1 = '\0';
      snprintf(e.name, sizeof(e.name), "%s", p);
      char* sz = bar1 + 1;
      char* bar2 = strchr(sz, '|');
      if (bar2) {
        *bar2 = '\0';
        e.mtime = (uint32_t)strtoul(bar2 + 1, NULL, 10);
      }
      e.size = strtoull(sz, NULL, 10);
    } else {
      continue;
    }
    if (cb && cb(&e, user)) break;
  }
  return 0;
}

// Shared: send the T command and read READY + 4-byte size, open local file.
// Returns file handle on success, NULL on error (err already set).
static FILE* download_begin(CpSerial* s, const char* remote, const char* local, uint32_t* out_size) {
  port_flush_in(s);
  if (send_cmd_path(s, 'T', remote) != 0) return NULL;
  char line[600];
  if (read_until(s, "READY", line, sizeof(line), 10000) != 0) return NULL;
  uint8_t b4[4];
  if (read_exact(s, b4, 4, 5000) != 0) return NULL;
  *out_size = get_u32(b4);
  FILE* f = cp_fopen(local, "wb");
  if (!f) {
    set_err(s, "cannot create %s", local);
    return NULL;
  }
  return f;
}

static int progress_report(CpProgress cb, uint32_t done, uint32_t total, int* last_pct, void* user) {
  if (!cb) return 0;
  int pct = total ? (int)(((uint64_t)done * 100) / total) : 100;
  if (pct == *last_pct) return 0;
  *last_pct = pct;
  return cb(done, total, user);
}

// ACK-paced download: host sends 0x06 after each CHUNK-byte block. Both
// Witchhunt and MicroReader use this protocol (MicroReader firmware v? and
// later; the pacing prevents USB-CDC TX buffer overruns and data corruption).
static int cp_download_once(CpSerial* s, const char* remote, const char* local, CpProgress cb, void* user) {
  uint32_t size = 0;
  FILE* f = download_begin(s, remote, local, &size);
  if (!f) return -1;
  uint32_t crc = 0, remaining = size;
  uint8_t buf[CHUNK];
  int last_pct = -1;
  while (remaining > 0) {
    size_t want = remaining < CHUNK ? remaining : CHUNK;
    if (read_exact(s, buf, want, 30000) != 0) {
      fclose(f);
      return -1;
    }
    fwrite(buf, 1, want, f);
    crc = crc32_update(crc, buf, want);
    uint8_t ack = ACK;
    if (port_write(s, &ack, 1) != 0) {
      fclose(f);
      return -1;
    }
    remaining -= (uint32_t)want;
    if (progress_report(cb, size - remaining, size, &last_pct, user)) {
      fclose(f);
      set_err(s, "aborted");
      return -1;
    }
  }
  fclose(f);
  uint8_t b4[4];
  if (read_exact(s, b4, 4, 5000) != 0) return -1;
  if (get_u32(b4) != crc) {
    set_err(s, "CRC mismatch on %s", remote);
    return -1;
  }
  return 0;
}

int cp_download(CpSerial* s, const char* remote, const char* local, CpProgress cb, void* user) {
  // Retry: a download can occasionally stall on the device (a transient USB-CDC
  // link hiccup) and abort; the device recovers immediately, so a re-request
  // succeeds. Drain and retry a few times before giving up.
  for (int attempt = 0; attempt < 3; attempt++) {
    if (cp_download_once(s, remote, local, cb, user) == 0) return 0;
    if (s->err[0] && strstr(s->err, "aborted")) return -1;  // user cancelled: don't retry
    port_flush_in(s);
  }
  return -1;
}

int cp_upload(CpSerial* s, const char* local, const char* remote, CpProgress cb, void* user) {
  FILE* f = cp_fopen(local, "rb");
  if (!f) {
    set_err(s, "cannot open %s", local);
    return -1;
  }
  fseek(f, 0, SEEK_END);
  long total = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (total < 0) {
    fclose(f);
    set_err(s, "stat %s failed", local);
    return -1;
  }

  port_flush_in(s);
  if (send_cmd_path(s, 'W', remote) != 0) {
    fclose(f);
    return -1;
  }
  uint8_t szb[4];
  put_u32(szb, (uint32_t)total);
  if (port_write(s, szb, 4) != 0) {
    fclose(f);
    return -1;
  }
  char line[600];
  if (read_until(s, "READY", line, sizeof(line), 10000) != 0) {
    fclose(f);
    return -1;
  }

  uint32_t crc = 0;
  uint64_t sent = 0;
  uint8_t buf[CHUNK];
  size_t n;
  while ((n = fread(buf, 1, CHUNK, f)) > 0) {
    if (port_write(s, buf, n) != 0) {
      fclose(f);
      return -1;
    }
    crc = crc32_update(crc, buf, n);
    uint8_t ack;
    if (read_exact(s, &ack, 1, 30000) != 0) {
      fclose(f);
      return -1;
    }
    if (ack != ACK) {
      fclose(f);
      // Device sent an error response instead of a 0x06 ACK. Read the rest of
      // the line so the message ("ERR:disk full", etc.) reaches the user.
      char tail[200] = {0};
      size_t ti = 0;
      tail[ti++] = (char)ack;
      uint8_t c;
      while (ti < sizeof(tail) - 1) {
        if (port_read(s, &c, 1, 2000) <= 0) break;
        if (c == '\n') break;
        if (c != '\r') tail[ti++] = (char)c;
      }
      tail[ti] = '\0';
      if (tail[0] == 'E' && tail[1] == 'R' && tail[2] == 'R' && tail[3] == ':')
        set_err(s, "upload error: %s", tail + 4);
      else
        set_err(s, "bad ACK 0x%02x (%s)", ack, tail);
      return -1;
    }
    sent += n;
    if (cb && cb(sent, (uint64_t)total, user)) {
      fclose(f);
      set_err(s, "aborted");
      return -1;
    }
  }
  fclose(f);
  uint8_t crcb[4];
  put_u32(crcb, crc);
  if (port_write(s, crcb, 4) != 0) return -1;
  return expect_ok(s, "upload", 30000);
}

int cp_remove(CpSerial* s, const char* remote) {
  port_flush_in(s);
  if (send_cmd_path(s, 'R', remote) != 0) return -1;
  return expect_ok(s, "remove", 10000);
}

int cp_rename(CpSerial* s, const char* src, const char* dst) {
  port_flush_in(s);
  size_t prelen = strlen(s->path_prefix);
  // Avoid double prefix if path already starts with it.
  size_t src_pre = (prelen && strncmp(src, s->path_prefix, prelen) != 0) ? prelen : 0;
  size_t dst_pre = (prelen && strncmp(dst, s->path_prefix, prelen) != 0) ? prelen : 0;
  uint8_t hdr[5];
  memcpy(hdr, "CMND", 4);
  hdr[4] = 'N';
  uint8_t lb[2];
  if (port_write(s, hdr, 5) != 0) return -1;
  put_u16(lb, (uint16_t)(src_pre + strlen(src)));
  if (port_write(s, lb, 2) != 0) return -1;
  if (src_pre && port_write(s, s->path_prefix, src_pre) != 0) return -1;
  if (port_write(s, src, strlen(src)) != 0) return -1;
  put_u16(lb, (uint16_t)(dst_pre + strlen(dst)));
  if (port_write(s, lb, 2) != 0) return -1;
  if (dst_pre && port_write(s, s->path_prefix, dst_pre) != 0) return -1;
  if (port_write(s, dst, strlen(dst)) != 0) return -1;
  return expect_ok(s, "rename", 10000);
}

int cp_mkdir(CpSerial* s, const char* path) {
  port_flush_in(s);
  if (send_cmd_path(s, 'K', path) != 0) return -1;
  return expect_ok(s, "mkdir", 10000);
}

int cp_local_exists(const char* utf8_path) {
  FILE* f = cp_fopen(utf8_path, "rb");
  if (f) {
    fclose(f);
    return 1;
  }
  return 0;
}
