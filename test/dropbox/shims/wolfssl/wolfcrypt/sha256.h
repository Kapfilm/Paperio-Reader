#pragma once
#include <cstdint>
using word32 = uint32_t;
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
using wc_Sha256 = CC_SHA256_CTX;
inline int wc_InitSha256(wc_Sha256* c) { return CC_SHA256_Init(c) == 1 ? 0 : -1; }
inline int wc_Sha256Update(wc_Sha256* c, const uint8_t* b, word32 n) { return CC_SHA256_Update(c, b, n) == 1 ? 0 : -1; }
inline int wc_Sha256Final(wc_Sha256* c, uint8_t* b) { return CC_SHA256_Final(b, c) == 1 ? 0 : -1; }
#else
#include <openssl/sha.h>
using wc_Sha256 = SHA256_CTX;
inline int wc_InitSha256(wc_Sha256* c) { return SHA256_Init(c) == 1 ? 0 : -1; }
inline int wc_Sha256Update(wc_Sha256* c, const uint8_t* b, word32 n) { return SHA256_Update(c, b, n) == 1 ? 0 : -1; }
inline int wc_Sha256Final(wc_Sha256* c, uint8_t* b) { return SHA256_Final(b, c) == 1 ? 0 : -1; }
#endif
inline void wc_Sha256Free(wc_Sha256*) {}
