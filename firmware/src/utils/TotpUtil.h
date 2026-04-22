#pragma once
#include <Arduino.h>
#include <mbedtls/md.h>
#include <time.h>

enum class TotpAlgo : uint8_t { SHA1, SHA256, SHA512 };

namespace TotpUtil {

static inline const char* algoName(TotpAlgo a) {
  switch (a) {
    case TotpAlgo::SHA256: return "SHA256";
    case TotpAlgo::SHA512: return "SHA512";
    default:               return "SHA1";
  }
}

static inline TotpAlgo algoFromStr(const char* s) {
  if (strcmp(s, "SHA256") == 0) return TotpAlgo::SHA256;
  if (strcmp(s, "SHA512") == 0) return TotpAlgo::SHA512;
  return TotpAlgo::SHA1;
}

static inline int8_t _b32val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a';
  if (c >= '2' && c <= '7') return c - '2' + 26;
  return -1;
}

static inline int base32Decode(const char* in, uint8_t* out, int outMax) {
  int bits = 0, val = 0, outIdx = 0;
  for (int i = 0; in[i] && outIdx < outMax; i++) {
    int8_t v = _b32val(in[i]);
    if (v < 0) continue;
    val   = (val << 5) | v;
    bits += 5;
    if (bits >= 8) { bits -= 8; out[outIdx++] = (val >> bits) & 0xFF; }
  }
  return outIdx;
}

static inline uint32_t compute(const char* base32Secret, time_t now = 0,
                                uint8_t digits = 6, uint8_t period = 30,
                                TotpAlgo algo = TotpAlgo::SHA1) {
  if (now == 0) now = time(nullptr);

  uint8_t key[64] = {};
  int keyLen = base32Decode(base32Secret, key, (int)sizeof(key));
  if (keyLen == 0) return 0;

  uint64_t T = (uint64_t)now / period;
  uint8_t msg[8];
  for (int i = 7; i >= 0; i--) { msg[i] = T & 0xFF; T >>= 8; }

  mbedtls_md_type_t mdType;
  switch (algo) {
    case TotpAlgo::SHA256: mdType = MBEDTLS_MD_SHA256; break;
    case TotpAlgo::SHA512: mdType = MBEDTLS_MD_SHA512; break;
    default:               mdType = MBEDTLS_MD_SHA1;   break;
  }

  uint8_t hmac[64] = {};
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(mdType);
  if (!info) { mbedtls_md_free(&ctx); return 0; }
  mbedtls_md_setup(&ctx, info, 1);
  mbedtls_md_hmac_starts(&ctx, key, keyLen);
  mbedtls_md_hmac_update(&ctx, msg, 8);
  mbedtls_md_hmac_finish(&ctx, hmac);
  mbedtls_md_free(&ctx);

  uint8_t hashLen = mbedtls_md_get_size(info);
  int offset = hmac[hashLen - 1] & 0x0F;
  uint32_t code = ((hmac[offset]   & 0x7F) << 24)
                | ((hmac[offset+1] & 0xFF) << 16)
                | ((hmac[offset+2] & 0xFF) << 8)
                |  (hmac[offset+3] & 0xFF);

  uint32_t mod = 1;
  for (uint8_t i = 0; i < digits; i++) mod *= 10;
  return code % mod;
}

static inline uint8_t secondsLeft(time_t now = 0, uint8_t period = 30) {
  if (now == 0) now = time(nullptr);
  return (uint8_t)(period - now % period);
}

} // namespace TotpUtil
