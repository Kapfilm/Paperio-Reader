#include "SerialTransferProtocol.h"

#include <cstring>

namespace serialtransfer {

namespace {
constexpr uint8_t kAck = 0x06;

// The two 4-byte magics we recognize. "CMND" ≠ the firmware's existing "CMD:"
// line prefix, so the two protocols don't collide.
constexpr char kMagicCmd[4] = {'C', 'M', 'N', 'D'};
constexpr char kMagicEpub[4] = {'E', 'P', 'U', 'B'};
}  // namespace

void SerialTransferHost::writeLine(const char* s) {
  writeBytes(reinterpret_cast<const uint8_t*>(s), std::strlen(s));
  const uint8_t nl = '\n';
  writeBytes(&nl, 1);
}

// --- CRC32 (zlib/IEEE) ------------------------------------------------------
// Computed on the fly (no 1 KB table) to keep the footprint tiny; the upload is
// line/SD-bound, not CRC-bound, so the per-bit loop is not a bottleneck.
uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len) {
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) {
      crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));
    }
  }
  return ~crc;
}

// --- low-level reads --------------------------------------------------------
bool SerialTransferProtocol::readExact(uint8_t* buf, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    const int b = host_.readByte();
    if (b < 0) return false;  // timeout: abort this message
    buf[i] = static_cast<uint8_t>(b);
  }
  return true;
}

bool SerialTransferProtocol::readU16LE(uint16_t& out) {
  uint8_t b[2];
  if (!readExact(b, 2)) return false;
  out = static_cast<uint16_t>(b[0]) | (static_cast<uint16_t>(b[1]) << 8);
  return true;
}

bool SerialTransferProtocol::readU32LE(uint32_t& out) {
  uint8_t b[4];
  if (!readExact(b, 4)) return false;
  out = static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) | (static_cast<uint32_t>(b[2]) << 16) |
        (static_cast<uint32_t>(b[3]) << 24);
  return true;
}

// --- dispatch ---------------------------------------------------------------
bool SerialTransferProtocol::poll() {
  // Need a full 4-byte magic before we commit to either protocol.
  if (host_.available() < 4) return false;

  // Probe the magic WITHOUT consuming it, so a non-match leaves the bytes intact
  // for the caller's line-based "CMD:" protocol.
  uint8_t magic[4];
  for (size_t i = 0; i < 4; ++i) {
    const int b = host_.peek(i);
    if (b < 0) return false;
    magic[i] = static_cast<uint8_t>(b);
  }

  const bool isCmd = std::memcmp(magic, kMagicCmd, 4) == 0;
  const bool isEpub = std::memcmp(magic, kMagicEpub, 4) == 0;
  if (!isCmd && !isEpub) return false;  // not ours; nothing consumed

  // Match: now consume the 4 magic bytes and dispatch.
  uint8_t discard[4];
  if (!readExact(discard, 4)) return false;
  return isCmd ? handleCommand() : handleUpload();
}

bool SerialTransferProtocol::handleCommand() {
  const int op = host_.readByte();
  if (op < 0) return false;

  switch (op) {
    case 'L': {  // list books
      host_.writeLine("BOOKS:");
      for (const auto& e : host_.listBooks()) {
        std::string line = e.path + "|" + e.title + "|" + e.author;
        host_.writeLine(line.c_str());
      }
      host_.writeLine("END");
      return true;
    }
    case 'R': {  // remove file
      uint16_t len = 0;
      if (!readU16LE(len)) return true;
      std::string path(len, '\0');
      if (len && !readExact(reinterpret_cast<uint8_t*>(&path[0]), len)) return true;
      host_.writeLine(host_.removeFile(path) ? "OK" : "ERR:remove");
      return true;
    }
    case 'S': {  // status (heap etc.)
      std::string s = "STATUS:" + host_.statusLine();
      host_.writeLine(s.c_str());
      return true;
    }
    default:
      // Recognized magic but an opcode we don't implement (bench/clear/etc.).
      // Report rather than silently stall the host.
      host_.writeLine("ERR:unsupported");
      return true;
  }
}

bool SerialTransferProtocol::handleUpload() {
  uint16_t nameLen = 0;
  if (!readU16LE(nameLen)) return true;
  std::string name(nameLen, '\0');
  if (nameLen && !readExact(reinterpret_cast<uint8_t*>(&name[0]), nameLen)) return true;

  uint32_t dataLen = 0;
  if (!readU32LE(dataLen)) return true;

  const std::string dest = host_.uploadDestination(name);
  if (!host_.fileBegin(dest)) {
    host_.writeLine("ERR:create");
    return true;
  }

  host_.writeLine("READY");

  uint8_t buf[kChunkSize];
  uint32_t crc = 0;
  uint32_t remaining = dataLen;
  bool ok = true;

  while (remaining > 0) {
    const size_t want = remaining < kChunkSize ? remaining : kChunkSize;
    if (!readExact(buf, want)) {  // timeout mid-stream
      ok = false;
      break;
    }
    crc = crc32Update(crc, buf, want);
    if (!host_.fileWrite(buf, want)) {
      ok = false;
      break;
    }
    // ACK immediately; the firmware's fileWrite buffers/overlaps the SD write so
    // line time hides SD latency. One ACK per chunk, as the protocol requires.
    const uint8_t ack = kAck;
    host_.writeBytes(&ack, 1);
    remaining -= static_cast<uint32_t>(want);
  }

  if (!ok) {
    host_.fileEnd(/*keep=*/false);
    host_.writeLine("ERR:io");
    return true;
  }

  uint32_t wantCrc = 0;
  if (!readU32LE(wantCrc)) {
    host_.fileEnd(/*keep=*/false);
    host_.writeLine("ERR:crc");
    return true;
  }

  if (wantCrc != crc) {
    host_.fileEnd(/*keep=*/false);
    host_.writeLine("ERR:crc");
    return true;
  }

  host_.fileEnd(/*keep=*/true);
  host_.writeLine("OK");
  return true;
}

}  // namespace serialtransfer
