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

  // ── Ephemeral ECDH (CTAP2 ClientPIN / hmac-secret platform key) ────────
  // Per CTAP2 §6.5: the authenticator holds a single ephemeral P-256 keypair
  // that is regenerated each power cycle (and on Reset). Hosts obtain the
  // public side via authenticatorClientPIN.getKeyAgreement and use it to
  // negotiate a shared secret via ECDH for both PIN setup and the
  // hmac-secret extension.

  // (Re)generate the ephemeral keypair. Call from WebAuthnScreen::onInit and
  // after CTAP2_RESET. Idempotent — subsequent calls regenerate fresh.
  static bool initEphemeralEcdh();

  // Returns the ephemeral public key as `0x04 || X(32) || Y(32)` (65 bytes).
  // False if `initEphemeralEcdh()` has not run.
  static bool getEphemeralPublicKey(uint8_t pub[65]);

  // Compute ECDH shared X-coordinate against a peer's uncompressed pubkey.
  // For pinUvAuthProtocol v1 the caller hashes the result via SHA-256 to
  // form the 32-byte sharedSecret; we just return the raw X here.
  static bool ecdhComputeSharedX(const uint8_t peer_pub[65], uint8_t sharedX[32]);

  // ── X.509 self-signed cert builder ─────────────────────────────────────
  // Generates a v3 self-signed P-256 certificate for the given private key.
  // Writes ASN.1 DER bytes into `out`, sets *outLen on success. Used once at
  // first-boot to produce the U2F batch-attestation cert. Returns false if
  // mbedTLS x509write APIs aren't compiled in or the buffer is too small
  // (typical cert size is ~500 B; budget 768 B).
  static bool buildSelfSignedX509(const uint8_t priv[32],
                                  uint8_t* out, size_t outCap, size_t* outLen);

  // ── HMAC-secret extension key derivation ──────────────────────────────
  // Per the SLIP-0022 chain pico-fido uses (lifted into UniGeek without the
  // HD-wallet master refactor): the per-credential 64-byte secret is
  //   HMAC(masterKey, "SLIP-0022")
  //   HMAC(_,         "hmac-secret")
  //   HMAC(_,         cred_id, cred_id_len)             → out[0..32]   no-UV
  //   HMAC(out[0..32],"hmac-secret-uv")
  //   HMAC(_,         cred_id, cred_id_len)             → out[32..64]  UV
  // The masterKey input is read from CredentialStore; caller must have
  // already initialized the store. Returns false if storage isn't ready.
  static bool deriveHmacSecret(const uint8_t* cred_id, size_t cred_id_len,
                               uint8_t out[64]);
};

}  // namespace webauthn
