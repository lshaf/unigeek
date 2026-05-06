#pragma once

#include <stdint.h>
#include <stddef.h>

namespace webauthn {

// Credential persistence and credential-ID wrapping.
//
// Layout under primary storage (SD if mounted, else LFS):
//   /unigeek/utility/fido/master.bin    — 32 B master key (one-time generated)
//   /unigeek/utility/fido/counter.bin   — 4 B big-endian global signature counter
//   /unigeek/utility/fido/residents.bin — array of ResidentCred records (Phase 5b)
//   /unigeek/utility/fido/pin.bin       — 16 B PIN HMAC + 1 B retry counter (Phase 5c)
//
// Credential ID format (96 bytes opaque to RP):
//   nonce(16) || rpIdHash(32) || ct(32) || tag(16)
// where ct = AES-256-CBC(masterKey, nonce, privKey(32))
// and   tag = leftmost 16 bytes of HMAC-SHA-256(masterKey, nonce || rpIdHash || ct)
//
// Decode validates the tag (binding the credId to a specific RP) before
// returning the private key.
class CredentialStore {
public:
  static constexpr size_t kMasterKeySize = 32;
  static constexpr size_t kPrivKeySize   = 32;
  static constexpr size_t kRpIdHashSize  = 32;
  static constexpr size_t kCredIdSize    = 96;  // 16 + 32 + 32 + 16
  static constexpr size_t kHmacTagSize   = 16;

  // Load the master key (or generate + persist it on first call).
  // Loads the global counter as well. Idempotent.
  static bool init();

  // Atomically increment the global signature counter and return the new
  // value. Persists immediately. Returns 0 if storage is unavailable.
  static uint32_t bumpCounter();

  // Encode a fresh credential ID wrapping `priv`, bound to `rpIdHash`.
  static bool encodeCredentialId(const uint8_t priv[kPrivKeySize],
                                 const uint8_t rpIdHash[kRpIdHashSize],
                                 uint8_t out[kCredIdSize]);

  // Decode a credential ID. Returns true and writes `priv` only if the
  // tag is valid AND the embedded rpIdHash matches the provided one.
  static bool decodeCredentialId(const uint8_t* idBytes, size_t idLen,
                                 const uint8_t rpIdHash[kRpIdHashSize],
                                 uint8_t priv[kPrivKeySize]);

  // Copy out the 32-byte master key (loading + lazily generating on first
  // call). Used by the hmac-secret extension to derive per-cred secrets;
  // caller is responsible for zeroing the buffer after use.
  static bool getMasterKey(uint8_t out[kMasterKeySize]);

  // ── U2F batch-attestation device identity ──────────────────────────────
  // A separate ECDSA P-256 keypair from the AES-CBC master used for cred
  // wrapping. Generated on first call and persisted under
  // /unigeek/utility/fido/u2f_priv.bin + u2f_cert.der.
  //
  // The cert (~500 B) is cached in a static buffer after first load so
  // U2F REGISTER doesn't touch storage on every call. `*outCert` points into
  // that static buffer — DO NOT free; valid until next CredentialStore::wipe.
  static bool getDeviceKey  (uint8_t out[kPrivKeySize]);
  static bool getDeviceCert (const uint8_t** outCert, size_t* outLen);

  // Wipe master + counter + resident table + PIN. Master key is then
  // regenerated lazily on next encode call. All previously issued
  // credentials are rendered useless (intended).
  static bool wipe();
};

}  // namespace webauthn
