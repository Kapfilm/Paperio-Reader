// Minimal Total Commander / Double Commander WFX (file-system plugin) API.
// Trimmed from the TC plugin SDK fsplugin.h to what this plugin uses, with
// cross-platform type shims so the same source builds on Windows (.wfx/.wfx64)
// and Linux (.wfx for Double Commander).
#ifndef WFXPLUGIN_H
#define WFXPLUGIN_H

#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#define WFX_EXPORT __declspec(dllexport)
#else
#define WFX_EXPORT __attribute__((visibility("default")))

// --- Windows type shims (layouts must match what Double Commander expects) ---
typedef int BOOL;
typedef uint32_t DWORD;
typedef uint16_t WORD;
typedef void* HANDLE;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#define INVALID_HANDLE_VALUE ((HANDLE)(-1))
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_NORMAL 0x80

typedef struct {
  DWORD dwLowDateTime;
  DWORD dwHighDateTime;
} FILETIME;

typedef struct {
  DWORD dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD nFileSizeHigh;
  DWORD nFileSizeLow;
  DWORD dwReserved0;
  DWORD dwReserved1;
  char cFileName[260];
  char cAlternateFileName[14];
} WIN32_FIND_DATAA;

// Wide (UTF-16) variant. WCHAR is 2 bytes to match Free Pascal's WideChar, which
// is what Double Commander uses for Unicode plugins on every platform.
typedef uint16_t WCHAR;
typedef struct {
  DWORD dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD nFileSizeHigh;
  DWORD nFileSizeLow;
  DWORD dwReserved0;
  DWORD dwReserved1;
  WCHAR cFileName[260];
  WCHAR cAlternateFileName[14];
} WIN32_FIND_DATAW;
#endif  // _WIN32

// --- WFX callbacks (host -> plugin) ---
typedef int (*tProgressProc)(int PluginNr, char* SourceName, char* TargetName, int PercentDone);
typedef void (*tLogProc)(int PluginNr, int MsgType, char* LogString);
typedef BOOL (*tRequestProc)(int PluginNr, int RequestType, char* CustomTitle, char* CustomText, char* ReturnedText,
                             int maxlen);
// Wide (Unicode) callback variants, passed to FsInitW.
typedef int (*tProgressProcW)(int PluginNr, WCHAR* SourceName, WCHAR* TargetName, int PercentDone);
typedef void (*tLogProcW)(int PluginNr, int MsgType, WCHAR* LogString);
typedef BOOL (*tRequestProcW)(int PluginNr, int RequestType, WCHAR* CustomTitle, WCHAR* CustomText, WCHAR* ReturnedText,
                              int maxlen);

// Passed to FsSetDefaultParams; DefaultIniName is where the plugin reads/writes
// its settings (used here for the optional serial-port override).
typedef struct {
  int size;
  DWORD PluginInterfaceVersionLow;
  DWORD PluginInterfaceVersionHi;
  char DefaultIniName[260];
} FsDefaultParamStruct;

// --- FsGetFile / FsPutFile / FsRenMovFile return codes ---
#define FS_FILE_OK 0
#define FS_FILE_EXISTS 1
#define FS_FILE_NOTFOUND 2
#define FS_FILE_READERROR 3
#define FS_FILE_WRITEERROR 4
#define FS_FILE_USERABORT 5
#define FS_FILE_NOTSUPPORTED 6

// --- CopyFlags bits ---
#define FS_COPYFLAGS_OVERWRITE 1
#define FS_COPYFLAGS_RESUME 2
#define FS_COPYFLAGS_MOVE 4

// --- FsDisconnect reason codes ---
#define FS_DISCONNECT_CLOSE_CONNECTION 1

// --- Log message types ---
#define MSGTYPE_CONNECT 1
#define MSGTYPE_DISCONNECT 2
#define MSGTYPE_DETAILS 3
#define MSGTYPE_TRANSFERCOMPLETE 4
#define MSGTYPE_OPERATIONCOMPLETE 8
#define MSGTYPE_IMPORTANTERROR 16

#endif  // WFXPLUGIN_H
