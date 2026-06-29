// tinflate.c's uzlib_uncompress_chksum() references uzlib_adler32/uzlib_crc32,
// which live in upstream uzlib source files that are not vendored here (the
// firmware link strips the unused chksum path). The host link pulls the whole
// tinflate.c object in, so provide no-op stubs to satisfy the references; the
// LZ-copy tests use uzlib_uncompress(), not the checksum variant.
#include <cstdint>

extern "C" {
uint32_t uzlib_adler32(const void*, unsigned int, uint32_t prev) { return prev; }
uint32_t uzlib_crc32(const void*, unsigned int, uint32_t crc) { return crc; }
}
