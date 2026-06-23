#include <gtest/gtest.h>

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "SerialTransfer/SerialTransferProtocol.h"

using serialtransfer::BookEntry;
using serialtransfer::crc32Update;
using serialtransfer::SerialTransferHost;
using serialtransfer::SerialTransferProtocol;

namespace {

// In-memory host: `in` feeds the protocol, `out` captures replies, and the file
// sink records the last uploaded file. Mirrors what the firmware wires to
// logSerial + HalStorage, but with no hardware.
class FakeHost : public SerialTransferHost {
 public:
  std::deque<uint8_t> in;        // bytes the device "receives"
  std::vector<uint8_t> out;      // bytes the device "sends"
  std::vector<BookEntry> books;  // what listBooks() returns

  // Captured upload state.
  std::string lastPath;
  std::vector<uint8_t> lastData;
  bool fileKept = false;
  bool failCreate = false;

  std::vector<std::string> removed;
  bool removeResult = true;

  // -- inbound --
  size_t available() override { return in.size(); }
  int readByte() override {
    if (in.empty()) return -1;
    int b = in.front();
    in.pop_front();
    return b;
  }
  int peek(size_t i) override {
    if (i >= in.size()) return -1;
    return in[i];
  }

  // -- outbound --
  void writeBytes(const uint8_t* data, size_t len) override { out.insert(out.end(), data, data + len); }

  // -- file sink --
  bool fileBegin(const std::string& path) override {
    if (failCreate) return false;
    lastPath = path;
    lastData.clear();
    fileKept = false;
    return true;
  }
  bool fileWrite(const uint8_t* data, size_t len) override {
    lastData.insert(lastData.end(), data, data + len);
    return true;
  }
  void fileEnd(bool keep) override { fileKept = keep; }

  // -- queries --
  std::vector<BookEntry> listBooks() override { return books; }
  bool removeFile(const std::string& path) override {
    removed.push_back(path);
    return removeResult;
  }
  std::string statusLine() override { return "heap=12345"; }
  std::string uploadDestination(const std::string& name) override { return "/books/" + name; }

  // -- helpers for building the input stream --
  void push(const std::string& s) {
    for (char c : s) in.push_back(static_cast<uint8_t>(c));
  }
  void push(const uint8_t* d, size_t n) {
    for (size_t i = 0; i < n; ++i) in.push_back(d[i]);
  }
  void pushU16(uint16_t v) {
    in.push_back(v & 0xFF);
    in.push_back((v >> 8) & 0xFF);
  }
  void pushU32(uint32_t v) {
    in.push_back(v & 0xFF);
    in.push_back((v >> 8) & 0xFF);
    in.push_back((v >> 16) & 0xFF);
    in.push_back((v >> 24) & 0xFF);
  }

  std::string outStr() const { return std::string(out.begin(), out.end()); }
  size_t ackCount() const {
    size_t n = 0;
    for (uint8_t b : out)
      if (b == 0x06) ++n;
    return n;
  }
};

uint32_t zlibCrc(const std::vector<uint8_t>& d) { return crc32Update(0, d.data(), d.size()); }

}  // namespace

// CRC32 must match zlib.crc32 for known vectors (the host tool uses zlib).
TEST(SerialTransferCrc, KnownVectors) {
  // zlib.crc32(b"") == 0
  EXPECT_EQ(crc32Update(0, nullptr, 0), 0u);
  // zlib.crc32(b"123456789") == 0xCBF43926
  const std::string s = "123456789";
  EXPECT_EQ(crc32Update(0, reinterpret_cast<const uint8_t*>(s.data()), s.size()), 0xCBF43926u);
}

TEST(SerialTransferCrc, IncrementalEqualsOneShot) {
  std::vector<uint8_t> data;
  for (int i = 0; i < 5000; ++i) data.push_back(static_cast<uint8_t>(i * 7 + 1));
  uint32_t whole = crc32Update(0, data.data(), data.size());
  uint32_t split = 0;
  split = crc32Update(split, data.data(), 2048);
  split = crc32Update(split, data.data() + 2048, 2048);
  split = crc32Update(split, data.data() + 4096, data.size() - 4096);
  EXPECT_EQ(whole, split);
}

// Non-magic leading bytes must not be consumed-as-handled; poll() returns false
// so the caller can fall through to its line-based protocol.
TEST(SerialTransferDispatch, IgnoresForeignMagic) {
  FakeHost h;
  const std::string line = "CMD:SCREENSHOT\n";
  h.push(line);  // the firmware's existing line protocol
  SerialTransferProtocol proto(h);
  EXPECT_FALSE(proto.poll());
  EXPECT_TRUE(h.out.empty());
  // Crucial: a non-matching probe must NOT consume any bytes, so the caller's
  // line handler still sees the full "CMD:SCREENSHOT\n".
  EXPECT_EQ(h.in.size(), line.size());
}

// A magic that only partially arrived must not be consumed; poll() waits.
TEST(SerialTransferDispatch, PartialMagicNotConsumed) {
  FakeHost h;
  h.push("CM");  // only 2 of 4 magic bytes present
  SerialTransferProtocol proto(h);
  EXPECT_FALSE(proto.poll());
  EXPECT_EQ(h.in.size(), 2u);  // nothing consumed
}

TEST(SerialTransferDispatch, ListBooks) {
  FakeHost h;
  h.books = {{"/books/a.epub", "Alpha", "AuthA"}, {"/books/b.epub", "Beta", "AuthB"}};
  h.push("CMNDL");
  SerialTransferProtocol proto(h);
  EXPECT_TRUE(proto.poll());
  EXPECT_EQ(h.outStr(), "BOOKS:\n/books/a.epub|Alpha|AuthA\n/books/b.epub|Beta|AuthB\nEND\n");
}

TEST(SerialTransferDispatch, RemoveFile) {
  FakeHost h;
  const std::string path = "/books/old.epub";
  h.push("CMNDR");
  h.pushU16(static_cast<uint16_t>(path.size()));
  h.push(path);
  SerialTransferProtocol proto(h);
  EXPECT_TRUE(proto.poll());
  ASSERT_EQ(h.removed.size(), 1u);
  EXPECT_EQ(h.removed[0], path);
  EXPECT_EQ(h.outStr(), "OK\n");
}

TEST(SerialTransferDispatch, RemoveFileFailure) {
  FakeHost h;
  h.removeResult = false;
  const std::string path = "/books/x.epub";
  h.push("CMNDR");
  h.pushU16(static_cast<uint16_t>(path.size()));
  h.push(path);
  SerialTransferProtocol proto(h);
  EXPECT_TRUE(proto.poll());
  EXPECT_EQ(h.outStr(), "ERR:remove\n");
}

TEST(SerialTransferDispatch, Status) {
  FakeHost h;
  h.push("CMNDS");
  SerialTransferProtocol proto(h);
  EXPECT_TRUE(proto.poll());
  EXPECT_EQ(h.outStr(), "STATUS:heap=12345\n");
}

TEST(SerialTransferDispatch, UnsupportedOpcode) {
  FakeHost h;
  h.push("CMNDX");  // bench opcode we don't implement
  SerialTransferProtocol proto(h);
  EXPECT_TRUE(proto.poll());
  EXPECT_EQ(h.outStr(), "ERR:unsupported\n");
}

// Full upload round-trip: header, READY, chunked data with per-chunk ACK,
// trailing CRC, final OK. Data spans multiple chunks plus a partial.
TEST(SerialTransferUpload, RoundTripMultiChunk) {
  FakeHost h;
  const std::string name = "book.epub";
  std::vector<uint8_t> data;
  for (int i = 0; i < 2048 * 2 + 100; ++i) data.push_back(static_cast<uint8_t>((i * 31 + 5) & 0xFF));

  h.push("EPUB");
  h.pushU16(static_cast<uint16_t>(name.size()));
  h.push(name);
  h.pushU32(static_cast<uint32_t>(data.size()));
  h.push(data.data(), data.size());
  h.pushU32(zlibCrc(data));

  SerialTransferProtocol proto(h);
  EXPECT_TRUE(proto.poll());

  EXPECT_EQ(h.lastPath, "/books/book.epub");
  EXPECT_EQ(h.lastData, data);
  EXPECT_TRUE(h.fileKept);
  // 3 chunks (2048 + 2048 + 100) => 3 ACKs.
  EXPECT_EQ(h.ackCount(), 3u);
  // Reply ends with READY then OK (ACK bytes interleaved as 0x06).
  const std::string s = h.outStr();
  EXPECT_NE(s.find("READY\n"), std::string::npos);
  EXPECT_NE(s.find("OK\n"), std::string::npos);
}

TEST(SerialTransferUpload, BadCrcRejectsAndDeletes) {
  FakeHost h;
  const std::string name = "bad.epub";
  std::vector<uint8_t> data = {1, 2, 3, 4, 5};
  h.push("EPUB");
  h.pushU16(static_cast<uint16_t>(name.size()));
  h.push(name);
  h.pushU32(static_cast<uint32_t>(data.size()));
  h.push(data.data(), data.size());
  h.pushU32(zlibCrc(data) ^ 0xFFFFFFFFu);  // wrong CRC

  SerialTransferProtocol proto(h);
  EXPECT_TRUE(proto.poll());
  EXPECT_FALSE(h.fileKept);  // partial file removed
  EXPECT_NE(h.outStr().find("ERR:crc\n"), std::string::npos);
}

TEST(SerialTransferUpload, CreateFailureReportsErr) {
  FakeHost h;
  h.failCreate = true;
  const std::string name = "x.epub";
  h.push("EPUB");
  h.pushU16(static_cast<uint16_t>(name.size()));
  h.push(name);
  h.pushU32(0);
  SerialTransferProtocol proto(h);
  EXPECT_TRUE(proto.poll());
  EXPECT_EQ(h.outStr(), "ERR:create\n");
}

TEST(SerialTransferUpload, TruncatedStreamTimesOut) {
  FakeHost h;
  const std::string name = "t.epub";
  h.push("EPUB");
  h.pushU16(static_cast<uint16_t>(name.size()));
  h.push(name);
  h.pushU32(100);   // claims 100 bytes...
  h.push("short");  // ...but only 5 arrive, then stream ends (readByte -> -1)
  SerialTransferProtocol proto(h);
  EXPECT_TRUE(proto.poll());
  EXPECT_FALSE(h.fileKept);
  EXPECT_NE(h.outStr().find("ERR:io\n"), std::string::npos);
}
