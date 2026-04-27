#pragma once

#include <stdint.h>
#include <stddef.h>

namespace webauthn {

// Crypto facade over mbedTLS. All entry points are static and use a process-
// wide CTR-DRBG seeded from `esp_random()`. The FIDO subsystem is single-
// threaded so no per-call locking is needed.
//
// `init()` must be called once at boot (idempotent — subsequent calls are
// no-ops). Calling any other function before init() returns false / zeros.
class WebAuthnCrypto {
public:
  static bool init();

  // RNG
  static void random(uint8_t* out, size_t len);

  // SHA-256 oneshot
  static void sha256(const uint8_t* in, size_t len, uint8_t out[32]);

  // HMAC-SHA-256
  static void hmacSha256(const uint8_t* key, size_t keyLen,
                         const uint8_t* msg, size_t msgLen,
                         uint8_t out[32]);

  // HKDF-SHA-256 (RFC 5869). `salt`/`info` may be null with respective
  // length zero.
  static bool hkdfSha256(const uint8_t* ikm,  size_t ikmLen,
                         const uint8_t* salt, size_t saltLen,
                         const uint8_t* info, size_t infoLen,
                         uint8_t* okm, size_t okmLen);

  // AES-256-CBC. `len` must be a multiple of 16. PKCS#7 / padding is the
  // caller's responsibility — we only do the raw block transform.
  static bool aes256CbcEncrypt(const uint8_t key[32], const uint8_t iv[16],
                               const uint8_t* in, size_t len, uint8_t* out);
  static bool aes256CbcDecrypt(const uint8_t key[32], const uint8_t iv[16],
                               const uint8_t* in, size_t len, uint8_t* out);

  // ECDSA P-256 keygen. Returns the raw 32-byte big-endian private key and
  // the uncompressed public key as `0x04 || X(32) || Y(32)` (65 bytes).
  static bool ecdsaP256Keygen(uint8_t priv[32], uint8_t pub[65]);

  // Derive the uncompressed public key from a private key.
  static bool ecdsaP256DerivePub(const uint8_t priv[32], uint8_t pub[65]);

  // Sign a 32-byte digest. The signature is written as ASN.1 DER (Sequence
  // of two INTEGERs r, s); maximum length is 72 bytes. `*outLen` is set on
  // success.
  static bool ecdsaP256SignDer(const uint8_t priv[32], const uint8_t hash[32],
                               uint8_t* outDer, size_t* outLen);
};

}  // namespace webauthn
