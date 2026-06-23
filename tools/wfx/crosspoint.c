// CrossPoint USB serial file-system plugin for Total Commander / Double
// Commander (WFX). Browses and transfers files on the e-reader's SD card over
// the USB cable using the serial protocol (see cp_serial.c).
//
// Open the device's "File Transfer -> USB Transfer" screen first, then open the
// "CrossPoint USB" file system in the commander.
//
// Protocol/idea: CidVonHighwind/MicroReader (https://github.com/CidVonHighwind/microreader).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cp_serial.h"
#include "wfxplugin.h"

static int g_plugin_nr = 0;
static tProgressProc g_progress = NULL;
static tLogProc g_log = NULL;
static tRequestProc g_request = NULL;
static CpSerial* g_conn = NULL;

static void logmsg(int type, const char* s) {
  if (g_log) g_log(g_plugin_nr, type, (char*)s);
}

// Lazily (re)open the connection. Returns NULL on failure (already logged).
static CpSerial* conn(void) {
  if (g_conn) return g_conn;
  logmsg(MSGTYPE_CONNECT, "CrossPoint USB: connecting...");
  g_conn = cp_open(NULL);
  if (!g_conn) {
    logmsg(MSGTYPE_IMPORTANTERROR, "CrossPoint USB: no device. Open 'USB Transfer' on the reader, then retry.");
  }
  return g_conn;
}

// Drop the connection so the next operation reconnects (after an I/O error).
static void drop_conn(void) {
  if (g_conn) {
    cp_close(g_conn);
    g_conn = NULL;
  }
}

// Convert a commander path ("\books\foo") to a device path ("/books/foo").
static void dev_path(const char* tc, char* out, size_t cap) {
  size_t j = 0;
  if (!tc || !tc[0]) {
    snprintf(out, cap, "/");
    return;
  }
  for (size_t i = 0; tc[i] && j + 1 < cap; i++) out[j++] = (tc[i] == '\\') ? '/' : tc[i];
  out[j] = '\0';
  if (out[0] == '\0') snprintf(out, cap, "/");
}

static void unix_to_filetime(uint32_t t, FILETIME* ft) {
  // FILETIME = 100ns intervals since 1601-01-01; unix epoch is 1601 + 11644473600s.
  uint64_t ll = ((uint64_t)t + 11644473600ULL) * 10000000ULL;
  ft->dwLowDateTime = (DWORD)(ll & 0xFFFFFFFFULL);
  ft->dwHighDateTime = (DWORD)(ll >> 32);
}

static void fill_find_data(const CpEntry* e, WIN32_FIND_DATAA* fd) {
  memset(fd, 0, sizeof(*fd));
  fd->dwFileAttributes = e->is_dir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
  fd->nFileSizeLow = (DWORD)(e->size & 0xFFFFFFFFULL);
  fd->nFileSizeHigh = (DWORD)(e->size >> 32);
  unix_to_filetime(e->mtime, &fd->ftLastWriteTime);
  fd->ftCreationTime = fd->ftLastAccessTime = fd->ftLastWriteTime;
  snprintf(fd->cFileName, sizeof(fd->cFileName), "%s", e->name);
}

// --- directory listing ------------------------------------------------------
typedef struct {
  CpEntry* items;
  int count;
  int cap;
  int idx;
} FindState;

static int collect_cb(const CpEntry* e, void* user) {
  FindState* st = (FindState*)user;
  if (st->count >= st->cap) {
    int newcap = st->cap ? st->cap * 2 : 32;
    CpEntry* p = (CpEntry*)realloc(st->items, (size_t)newcap * sizeof(CpEntry));
    if (!p) return 1;  // stop on OOM
    st->items = p;
    st->cap = newcap;
  }
  st->items[st->count++] = *e;
  return 0;
}

WFX_EXPORT HANDLE FsFindFirst(char* Path, WIN32_FIND_DATAA* FindData) {
  CpSerial* c = conn();
  if (!c) return INVALID_HANDLE_VALUE;
  char path[600];
  dev_path(Path, path, sizeof(path));

  FindState* st = (FindState*)calloc(1, sizeof(FindState));
  if (!st) return INVALID_HANDLE_VALUE;
  if (cp_list_dir(c, path, collect_cb, st) != 0) {
    logmsg(MSGTYPE_IMPORTANTERROR, cp_last_error(c));
    drop_conn();
    free(st->items);
    free(st);
    return INVALID_HANDLE_VALUE;
  }
  if (st->count == 0) {
    free(st->items);
    free(st);
    return INVALID_HANDLE_VALUE;  // empty directory
  }
  fill_find_data(&st->items[0], FindData);
  st->idx = 1;
  return (HANDLE)st;
}

WFX_EXPORT BOOL FsFindNext(HANDLE Hdl, WIN32_FIND_DATAA* FindData) {
  FindState* st = (FindState*)Hdl;
  if (!st || st->idx >= st->count) return FALSE;
  fill_find_data(&st->items[st->idx++], FindData);
  return TRUE;
}

WFX_EXPORT int FsFindClose(HANDLE Hdl) {
  FindState* st = (FindState*)Hdl;
  if (st) {
    free(st->items);
    free(st);
  }
  return 0;
}

// --- transfers --------------------------------------------------------------
typedef struct {
  char* src;
  char* dst;
} ProgCtx;

static int progress_cb(uint64_t done, uint64_t total, void* user) {
  if (!g_progress) return 0;
  ProgCtx* p = (ProgCtx*)user;
  int pct = total ? (int)((done * 100) / total) : 0;
  return g_progress(g_plugin_nr, p->src, p->dst, pct);  // non-zero => abort
}

WFX_EXPORT int FsGetFile(char* RemoteName, char* LocalName, int CopyFlags, void* RemoteInfo) {
  (void)RemoteInfo;
  CpSerial* c = conn();
  if (!c) return FS_FILE_READERROR;
  if (!(CopyFlags & FS_COPYFLAGS_OVERWRITE)) {
    FILE* f = fopen(LocalName, "rb");
    if (f) {
      fclose(f);
      return FS_FILE_EXISTS;
    }
  }
  char remote[600];
  dev_path(RemoteName, remote, sizeof(remote));
  ProgCtx ctx = {RemoteName, LocalName};
  if (cp_download(c, remote, LocalName, progress_cb, &ctx) != 0) {
    const char* err = cp_last_error(c);
    logmsg(MSGTYPE_IMPORTANTERROR, err);
    if (strstr(err, "aborted")) return FS_FILE_USERABORT;
    drop_conn();
    return FS_FILE_READERROR;
  }
  return FS_FILE_OK;
}

WFX_EXPORT int FsPutFile(char* LocalName, char* RemoteName, int CopyFlags) {
  CpSerial* c = conn();
  if (!c) return FS_FILE_WRITEERROR;
  (void)CopyFlags;  // device overwrites by path; commander handles confirm dialog
  char remote[600];
  dev_path(RemoteName, remote, sizeof(remote));
  ProgCtx ctx = {LocalName, RemoteName};
  if (cp_upload(c, LocalName, remote, progress_cb, &ctx) != 0) {
    const char* err = cp_last_error(c);
    logmsg(MSGTYPE_IMPORTANTERROR, err);
    if (strstr(err, "aborted")) return FS_FILE_USERABORT;
    drop_conn();
    return FS_FILE_WRITEERROR;
  }
  return FS_FILE_OK;
}

// --- file ops ---------------------------------------------------------------
WFX_EXPORT BOOL FsDeleteFile(char* RemoteName) {
  CpSerial* c = conn();
  if (!c) return FALSE;
  char remote[600];
  dev_path(RemoteName, remote, sizeof(remote));
  if (cp_remove(c, remote) != 0) {
    logmsg(MSGTYPE_IMPORTANTERROR, cp_last_error(c));
    return FALSE;
  }
  return TRUE;
}

WFX_EXPORT BOOL FsRemoveDir(char* RemoteName) {
  return FsDeleteFile(RemoteName);  // device 'R' removes an (empty) dir too
}

WFX_EXPORT BOOL FsMkDir(char* Path) {
  CpSerial* c = conn();
  if (!c) return FALSE;
  char path[600];
  dev_path(Path, path, sizeof(path));
  if (cp_mkdir(c, path) != 0) {
    logmsg(MSGTYPE_IMPORTANTERROR, cp_last_error(c));
    return FALSE;
  }
  return TRUE;
}

WFX_EXPORT int FsRenMovFile(char* OldName, char* NewName, BOOL Move, BOOL OverWrite, void* RemoteInfo) {
  (void)Move;
  (void)OverWrite;
  (void)RemoteInfo;
  CpSerial* c = conn();
  if (!c) return FS_FILE_WRITEERROR;
  char a[600], b[600];
  dev_path(OldName, a, sizeof(a));
  dev_path(NewName, b, sizeof(b));
  if (cp_rename(c, a, b) != 0) {
    logmsg(MSGTYPE_IMPORTANTERROR, cp_last_error(c));
    drop_conn();
    return FS_FILE_WRITEERROR;
  }
  return FS_FILE_OK;
}

// --- plugin lifecycle -------------------------------------------------------
WFX_EXPORT int FsInit(int PluginNr, tProgressProc pProgress, tLogProc pLog, tRequestProc pRequest) {
  g_plugin_nr = PluginNr;
  g_progress = pProgress;
  g_log = pLog;
  g_request = pRequest;
  return 0;
}

WFX_EXPORT void FsGetDefRootName(char* DefRootName, int maxlen) {
  snprintf(DefRootName, (size_t)maxlen, "CrossPoint USB");
}

WFX_EXPORT void FsSetDefaultParams(void* dps) { (void)dps; }

WFX_EXPORT int FsExecuteFile(HANDLE MainWin, char* RemoteName, char* Verb) {
  (void)MainWin;
  (void)RemoteName;
  (void)Verb;
  return 0;  // FS_EXEC_OK / no special handling
}
