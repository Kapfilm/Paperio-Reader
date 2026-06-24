// Diagnostic tool for the CrossPoint/MicroReader serial protocol.
// Exercises connect, list, and download through the real cp_serial code paths.
//
// Usage:
//   diagnose.exe                  auto-detect port
//   diagnose.exe COM8             explicit port
//   diagnose.exe COM8 /fonts_families.bin   download a specific file
//
// Build (MSYS2 MinGW64 shell from tools/wfx/):
//   gcc -o diagnose.exe diagnose.c cp_serial.c -lsetupapi -Wall -Wno-format
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "cp_serial.h"

static int on_progress(uint64_t done, uint64_t total, void* user) {
  (void)user;
  printf("  progress: %llu / %llu bytes (%.1f%%)\n",
         (unsigned long long)done, (unsigned long long)total,
         total ? (done * 100.0 / total) : 0.0);
  fflush(stdout);
  return 0;
}

static int on_entry(const CpEntry* e, void* user) {
  (void)user;
  if (e->is_dir)
    printf("  DIR  %s\n", e->name);
  else
    printf("  FILE %-40s  %llu bytes\n", e->name, (unsigned long long)e->size);
  fflush(stdout);
  return 0;
}

// ---------------------------------------------------------------------------
// Raw ACK-paced download probe: manually walks the protocol step by step,
// printing timing and byte counts at each stage so we can see exactly where
// it stalls or fails.
// ---------------------------------------------------------------------------
static void raw_ack_probe(const char* port, const char* dl_path) {
  printf("[5] Raw ACK-paced probe for '%s'...\n", dl_path); fflush(stdout);

  char devname[32];
  snprintf(devname, sizeof(devname), "\\\\.\\%s", port ? port : "COM8");
  HANDLE h = CreateFileA(devname, GENERIC_READ|GENERIC_WRITE, 0, NULL,
                         OPEN_EXISTING, 0, NULL);
  if (h == INVALID_HANDLE_VALUE) { printf("  reopen failed (err %lu)\n", GetLastError()); return; }

  DCB dcb = {0}; dcb.DCBlength = sizeof(dcb);
  GetCommState(h, &dcb);
  dcb.BaudRate = CBR_115200; dcb.ByteSize = 8;
  dcb.Parity = NOPARITY; dcb.StopBits = ONESTOPBIT;
  dcb.fBinary = TRUE;
  dcb.fDtrControl = DTR_CONTROL_DISABLE;
  dcb.fRtsControl = RTS_CONTROL_DISABLE;
  SetCommState(h, &dcb);

  // Helper: read exactly n bytes, return actual count received within timeout_ms.
  #define RAW_READ(buf, n, ms) raw_read_exact(h, buf, n, ms)

  // Warm up: send STATUS, drain response.
  Sleep(200); PurgeComm(h, PURGE_RXCLEAR|PURGE_TXCLEAR);
  { unsigned char sc[5] = {'C','M','N','D','S'};
    DWORD wr = 0; WriteFile(h, sc, 5, &wr, NULL); }
  Sleep(300); PurgeComm(h, PURGE_RXCLEAR|PURGE_TXCLEAR);
  printf("  Warmed up (STATUS sent+drained).\n"); fflush(stdout);

  // Build CMND T frame.
  const char* pfx = "/sdcard";
  int need_pfx = (strncmp(dl_path, pfx, strlen(pfx)) != 0);
  char full[512];
  snprintf(full, sizeof(full), "%s%s", need_pfx ? pfx : "", dl_path);
  uint16_t plen = (uint16_t)strlen(full);
  unsigned char tcmd[7 + 512];
  tcmd[0]='C'; tcmd[1]='M'; tcmd[2]='N'; tcmd[3]='D'; tcmd[4]='T';
  tcmd[5] = plen & 0xFF; tcmd[6] = (plen >> 8) & 0xFF;
  memcpy(tcmd + 7, full, plen);

  DWORD t0 = GetTickCount();
  DWORD wr = 0;
  WriteFile(h, tcmd, 7 + plen, &wr, NULL);
  printf("  Sent CMND T for '%s' (%lu bytes written).\n", full, (unsigned long)wr);
  fflush(stdout);

  // Step 1: read until "READY\n" — collect bytes line by line, print each.
  printf("  Waiting for READY line...\n"); fflush(stdout);
  {
    unsigned char linebuf[512]; int li = 0;
    int found_ready = 0;
    DWORD deadline = GetTickCount() + 10000;
    while (GetTickCount() < deadline && !found_ready) {
      COMMTIMEOUTS to = {0};
      to.ReadIntervalTimeout = MAXDWORD;
      to.ReadTotalTimeoutConstant = (DWORD)(deadline - GetTickCount());
      SetCommTimeouts(h, &to);
      unsigned char c; DWORD rd = 0;
      ReadFile(h, &c, 1, &rd, NULL);
      if (!rd) { printf("  (timeout waiting for data)\n"); break; }
      if (c == '\r') continue;
      if (c == '\n') {
        linebuf[li] = '\0';
        printf("  +%lums  line: [%s]\n", (unsigned long)(GetTickCount()-t0), linebuf);
        fflush(stdout);
        if (strcmp((char*)linebuf, "READY") == 0) { found_ready = 1; break; }
        li = 0;
      } else {
        if (li < (int)sizeof(linebuf)-1) linebuf[li++] = (char)c;
      }
    }
    if (!found_ready) { printf("  READY not received — aborting.\n"); CloseHandle(h); return; }
  }
  printf("  +%lums  READY received.\n", (unsigned long)(GetTickCount()-t0)); fflush(stdout);

  // Step 2: read 4-byte size.
  {
    COMMTIMEOUTS to = {0};
    to.ReadIntervalTimeout = MAXDWORD;
    to.ReadTotalTimeoutConstant = 5000;
    SetCommTimeouts(h, &to);
    unsigned char sb[4]; DWORD rd = 0, total_rd = 0;
    // Read up to 4 bytes with a loop.
    DWORD dl2 = GetTickCount() + 5000;
    while (total_rd < 4 && GetTickCount() < dl2) {
      to.ReadTotalTimeoutConstant = (DWORD)(dl2 - GetTickCount());
      SetCommTimeouts(h, &to);
      ReadFile(h, sb + total_rd, 4 - total_rd, &rd, NULL);
      total_rd += rd;
      if (!rd) break;
    }
    if (total_rd != 4) {
      printf("  Size read failed: got %lu of 4 bytes.\n", (unsigned long)total_rd);
      CloseHandle(h); return;
    }
    uint32_t fsize = sb[0] | ((uint32_t)sb[1]<<8) | ((uint32_t)sb[2]<<16) | ((uint32_t)sb[3]<<24);
    printf("  +%lums  Size: %lu bytes (0x%08lX raw: %02X %02X %02X %02X)\n",
           (unsigned long)(GetTickCount()-t0), (unsigned long)fsize,
           (unsigned long)fsize, sb[0], sb[1], sb[2], sb[3]);
    fflush(stdout);

    // Step 3: read chunks, send ACK after each, print timing per chunk.
    uint32_t remaining = fsize;
    uint32_t chunk_num = 0;
    unsigned char chunk[2048];
    while (remaining > 0) {
      uint32_t want = remaining < 2048 ? remaining : 2048;
      uint32_t got = 0;
      DWORD cdl = GetTickCount() + 30000;
      DWORD chunk_t0 = GetTickCount();
      while (got < want && GetTickCount() < cdl) {
        COMMTIMEOUTS cto = {0};
        cto.ReadIntervalTimeout = MAXDWORD;
        cto.ReadTotalTimeoutConstant = (DWORD)(cdl - GetTickCount());
        SetCommTimeouts(h, &cto);
        DWORD crd = 0;
        ReadFile(h, chunk + got, want - got, &crd, NULL);
        if (!crd) break;
        got += crd;
      }
      DWORD chunk_elapsed = GetTickCount() - chunk_t0;
      printf("  +%lums  chunk %lu: want=%lu got=%lu in %lums",
             (unsigned long)(GetTickCount()-t0), (unsigned long)chunk_num,
             (unsigned long)want, (unsigned long)got, (unsigned long)chunk_elapsed);
      if (got < want) {
        printf("  *** SHORT READ — sending ACK anyway ***");
      }
      fflush(stdout);
      // Send ACK.
      DWORD ack_t0 = GetTickCount();
      unsigned char ack = 0x06;
      DWORD awr = 0;
      WriteFile(h, &ack, 1, &awr, NULL);
      printf("  ACK sent in %lums\n", (unsigned long)(GetTickCount()-ack_t0));
      fflush(stdout);
      remaining -= got < want ? got : want;
      chunk_num++;
      if (got < want) { printf("  *** Aborting due to short read ***\n"); break; }
    }

    // Step 4: read 4-byte CRC.
    if (remaining == 0) {
      unsigned char cb[4]; DWORD crd = 0, ctot = 0;
      DWORD cdl2 = GetTickCount() + 5000;
      while (ctot < 4 && GetTickCount() < cdl2) {
        COMMTIMEOUTS cto = {0};
        cto.ReadIntervalTimeout = MAXDWORD;
        cto.ReadTotalTimeoutConstant = (DWORD)(cdl2 - GetTickCount());
        SetCommTimeouts(h, &cto);
        ReadFile(h, cb + ctot, 4 - ctot, &crd, NULL);
        ctot += crd;
        if (!crd) break;
      }
      if (ctot == 4) {
        uint32_t dev_crc = cb[0] | ((uint32_t)cb[1]<<8) | ((uint32_t)cb[2]<<16) | ((uint32_t)cb[3]<<24);
        printf("  +%lums  CRC received: 0x%08lX\n",
               (unsigned long)(GetTickCount()-t0), (unsigned long)dev_crc);
      } else {
        printf("  +%lums  CRC read failed: got %lu of 4 bytes\n",
               (unsigned long)(GetTickCount()-t0), (unsigned long)ctot);
      }
      fflush(stdout);
    }
  }

  printf("  Probe done. Total time: %lums\n", (unsigned long)(GetTickCount()-t0));
  CloseHandle(h);
}

int main(int argc, char** argv) {
  const char* port    = argc > 1 ? argv[1] : NULL;
  const char* dl_path = argc > 2 ? argv[2] : NULL;

  printf("=== CrossPoint/MicroReader diagnostic ===\n");
  printf("Port: %s\n", port ? port : "(auto-detect)");
  fflush(stdout);

  printf("\n[1] Connecting...\n"); fflush(stdout);
  CpSerial* s = cp_open(port);
  if (!s) { printf("FAILED to connect.\n"); return 1; }
  printf("Connected OK.\n\n"); fflush(stdout);

  printf("[2] Status...\n"); fflush(stdout);
  char status[256] = {0};
  if (cp_status(s, status, sizeof(status)) == 0)
    printf("STATUS: %s\n\n", status);
  else
    printf("STATUS failed: %s\n\n", cp_last_error(s));
  fflush(stdout);

  printf("[3] Listing '/'...\n"); fflush(stdout);
  if (cp_list_dir(s, "/", on_entry, NULL) != 0)
    printf("List failed: %s\n", cp_last_error(s));
  printf("\n"); fflush(stdout);

  if (dl_path) {
    printf("[4] cp_download '%s'...\n", dl_path); fflush(stdout);
    const char* local_out = "diagnose_download.tmp";
    if (cp_download(s, dl_path, local_out, on_progress, NULL) == 0) {
      printf("Download OK.\n\n");
    } else {
      printf("Download FAILED: %s\n\n", cp_last_error(s));
    }
    fflush(stdout);

    // Close the cp_serial connection, then run the raw step-by-step probe.
    cp_close(s); s = NULL;
    raw_ack_probe(port, dl_path);
  } else {
    printf("[4] Skipping download (pass a device path as 2nd arg).\n\n");
  }

  cp_close(s);
  printf("\nDone.\n");
  return 0;
}
