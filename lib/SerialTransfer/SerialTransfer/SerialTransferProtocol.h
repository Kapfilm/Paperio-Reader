#pragma once

// Clean-room, wire-compatible implementation of CidVonHighwind/MicroReader's
// serial file-transfer protocol (independent implementation, protocol only —
// no firmware code copied). Goal: interoperate with MicroReader's host tools
// (serial_cmd.py, Calibre plugin) while being leaner/faster.
//
// The protocol logic here is deliberately hardware-free: it talks to the world
// through the SerialTransferHost interface (byte I/O + a file sink + a book
// lister), so it can be unit-tested on the host without Serial/HalStorage. The
// firmware wires those callbacks to logSerial and HalStorage; tests wire fakes.
//
// Wire format (source of truth: MicroReader tools/serial_cmd.py):
//   Command   = MAGIC("CMND") + opcode[1] [+ payload]
//   Upload     = MAGIC("EPUB") + <u16 nameLen> + name + <u32 dataLen> + data
//   <u16>/<u32> are little-endian. Text replies are '\n'-terminated ASCII.
//   Upload: device replies "READY", streams data in 2048-byte chunks, ACKs each
//   chunk with byte 0x06, then reads a trailing <u32 crc32> and replies
//   "OK"/"ERR:...". CRC32 is zlib/IEEE (poly 0xEDB88320).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace serialtransfer {

// One book entry as reported by the `L` (list) command: BOOKS: path|title|author
struct BookEntry {
  std::string path;
  std::string title;
  std::string author;
};

// Abstracts everything the protocol needs from the outside world so the state
// machine stays testable. The firmware implements this over logSerial +
// HalStorage; the host test implements it over in-memory buffers.
class SerialTransferHost {
 public:
  virtual ~SerialTransferHost() = default;

  // --- Inbound byte stream (from the serial line) ---
  // Number of bytes currently available to read without blocking.
  virtual size_t available() = 0;
  // Read one byte. Returns -1 if none became available within the timeout.
  virtual int readByte() = 0;
  // Non-destructively look at the byte `i` positions ahead (0 = next byte to be
  // read). Returns -1 if fewer than i+1 bytes are available. Used to probe the
  // 4-byte magic without consuming it, so a non-matching probe leaves the bytes
  // for the caller's own protocol.
  virtual int peek(size_t i) = 0;

  // --- Outbound (to the serial line) ---
  virtual void writeBytes(const uint8_t* data, size_t len) = 0;
  // Convenience: write an ASCII line followed by '\n'.
  void writeLine(const char* s);

  // --- File sink for uploads ---
  // Begin receiving a file at `path`. Returns false if it can't be created.
  virtual bool fileBegin(const std::string& path) = 0;
  // Append a chunk to the in-progress file. Returns false on write failure.
  virtual bool fileWrite(const uint8_t* data, size_t len) = 0;
  // Finish the file. If `keep` is false the (partial) file must be removed.
  virtual void fileEnd(bool keep) = 0;

  // --- Misc device queries used by simple opcodes ---
  virtual std::vector<BookEntry> listBooks() = 0;
  virtual bool removeFile(const std::string& path) = 0;
  virtual std::string statusLine() = 0;  // payload after "STATUS:"
  // Maps an upload name to its destination path on storage (e.g. books root).
  virtual std::string uploadDestination(const std::string& name) = 0;
};

// Drives one poll cycle. Detects the 4-byte magic, dispatches the opcode, and
// for uploads runs the chunked receive + CRC verification. Stateless between
// top-level messages: each poll() handles at most one complete message.
class SerialTransferProtocol {
 public:
  explicit SerialTransferProtocol(SerialTransferHost& host) : host_(host) {}

  // Returns true if a message was recognized and handled this call. Returns
  // false (without consuming bytes beyond the magic probe) when the leading
  // bytes are not one of our magics, so the caller can fall through to its own
  // line-based protocol.
  bool poll();

  // Chunk size mandated by the protocol (one 0x06 ACK per chunk).
  static constexpr size_t kChunkSize = 2048;

 private:
  bool handleCommand();  // after "CMND" consumed
  bool handleUpload();   // after "EPUB" consumed
  bool readExact(uint8_t* buf, size_t len);
  bool readU16LE(uint16_t& out);
  bool readU32LE(uint32_t& out);

  SerialTransferHost& host_;
};

// Incremental CRC32 (zlib/IEEE, poly 0xEDB88320), matching zlib.crc32 on the
// host. Seed with 0; feed chunks; the running value is the final CRC.
uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len);

}  // namespace serialtransfer
