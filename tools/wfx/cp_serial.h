// CrossPoint serial file-transfer transport — C client for the firmware's USB
// "USB Transfer" protocol (wire-compatible with CidVonHighwind/MicroReader).
// Shared by the WFX file-system plugin; cross-platform (POSIX termios + Win32).
#ifndef CP_SERIAL_H
#define CP_SERIAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CpSerial CpSerial;

typedef struct {
  int is_dir;
  char name[256];
  uint64_t size;
  uint32_t mtime;  // unix seconds, 0 if unknown
} CpEntry;

// Progress callback: return non-zero to abort the transfer. May be NULL.
typedef int (*CpProgress)(uint64_t done, uint64_t total, void* user);
// Directory entry callback: return non-zero to stop iterating. May be NULL.
typedef int (*CpEntryCb)(const CpEntry* e, void* user);

// Open the port (auto-detect if port == NULL/empty), reset-tolerant: waits for
// the device to reboot back into the USB Transfer screen. Returns NULL on error.
CpSerial* cp_open(const char* port);
void cp_close(CpSerial* s);

// All return 0 on success, negative on error. cp_last_error() has details.
int cp_status(CpSerial* s, char* out, size_t out_len);
int cp_list_dir(CpSerial* s, const char* path, CpEntryCb cb, void* user);
int cp_download(CpSerial* s, const char* remote, const char* local, CpProgress cb, void* user);
int cp_upload(CpSerial* s, const char* local, const char* remote, CpProgress cb, void* user);
int cp_remove(CpSerial* s, const char* remote);
int cp_rename(CpSerial* s, const char* src, const char* dst);
int cp_mkdir(CpSerial* s, const char* path);

const char* cp_last_error(CpSerial* s);
// Returns non-zero if the connected firmware supports file download (CMND 'T').
int cp_download_supported(CpSerial* s);

// Returns 1 if a local file at the given UTF-8 path exists (Unicode-safe on
// Windows). Used by the plugin's overwrite check.
int cp_local_exists(const char* utf8_path);

#ifdef __cplusplus
}
#endif
#endif  // CP_SERIAL_H
