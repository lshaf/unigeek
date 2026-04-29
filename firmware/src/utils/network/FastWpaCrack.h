#pragma once

#include <cstdint>
#include <cstddef>

// Software SHA1 context — no mbedtls mutex, safe for dual-core
struct FastSha1Ctx {
  uint32_t state[5];
  uint32_t count[2];
  uint8_t  buf[64];
};

struct FastHmacPre {
  FastSha1Ctx inner;
  FastSha1Ctx outer;
};

// Pre-compute HMAC-SHA1 inner/outer pad state from key
void fast_hmac_precompute(const uint8_t *key, size_t klen, FastHmacPre &out);

// PBKDF2-HMAC-SHA1, 32-byte output (WPA2 PMK derivation)
void fast_pbkdf2(const FastHmacPre &pre, const uint8_t *salt, size_t slen,
                 uint32_t iters, uint8_t out[32]);

// PRF-512 (WPA2 PTK derivation from PMK + prf_data)
void fast_prf512(const uint8_t pmk[32], const uint8_t *prf_data, uint8_t ptk[64]);

// Generic HMAC-SHA1 with precomputed pads
void fast_hmac_sha1_pre(const FastHmacPre &pre, const uint8_t *data, size_t dlen, uint8_t *out20);

// Full WPA2 password test: PBKDF2 → PRF-512 → MIC verify
// Returns true if password matches the handshake MIC
bool fast_wpa2_try_password(const char *pw, uint8_t pw_len,
                            const char *ssid, uint8_t ssid_len,
                            const uint8_t *prf_data,
                            const uint8_t *eapol, uint16_t eapol_len,
                            const uint8_t *mic);

// WPA2 handshake extracted from a PCAP file
struct CrackHandshake {
  char     ssid[33]     = {};
  uint8_t  ssid_len     = 0;
  uint8_t  ap[6]        = {};
  uint8_t  sta[6]       = {};
  uint8_t  anonce[32]   = {};
  uint8_t  snonce[32]   = {};
  uint8_t  mic[16]      = {};
  uint8_t  eapol[300]   = {};
  uint16_t eapol_len    = 0;
  uint8_t  prf_data[76] = {};
};

// Parse a WPA2 handshake from a PCAP file via Uni.Storage.
// reason (optional): 1=open failed, 2=bad magic, 3=no M1, 4=no M2, 5=no SSID
bool fast_pcap_parse(const char *path, CrackHandshake &hs, int *reason = nullptr);