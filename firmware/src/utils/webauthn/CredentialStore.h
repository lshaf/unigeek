#pragma once

#include <stdint.h>
#include <stddef.h>

namespace webauthn {

// Credential persistence and credential-ID wrapping.
//
// Layout under primary storage (SD if mounted, else LFS):
//   /unigeek/utility/fido/master.bin         — 32 B master key (one-time generated)
//   /unigeek/utility/fido/counter.bin        — 4 B big-endian global signature counter
//   /unigeek/utility/fido/pin.bin            — retries(1) | pinLen(1) | pinHash(16) = 18 B
//   /unigeek/utility/fido/credentials/<n>.bin — one ResidentCredRecord per discoverable cred
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

  // Load the master key from disk if present + load the global counter.
  // Idempotent. Does NOT auto-generate the master — see generateMaster().
  // Returns true even when master.bin is absent (counter still loads); use
  // hasMaster() to check whether key material is available.
  static bool init();

  // True when /unigeek/utility/fido/master.bin exists (or has already been
  // loaded into RAM this session). All credential operations gate on this.
  static bool hasMaster();

  // Explicitly create a fresh master key with the *current* CTR_DRBG state.
  // Caller is expected to have arranged WiFi/BT-active + NTP-synced RTC so
  // the underlying esp_random() entropy is high quality (call
  // WebAuthnCrypto::reseed() before this).
  //   force=false: refuse if master.bin already exists.
  //   force=true:  wipe everything (counter, PIN, config, resident creds)
  //                first, since old credentials are bound to the old master.
  static bool generateMaster(bool force);

  // Restore the master key from caller-provided entropy (e.g. BIP-39 decode
  // output). Always destructive: existing master + creds + PIN + counter +
  // config are wiped first, since old credentials were bound to the old
  // master. `len` must equal kMasterKeySize.
  static bool restoreMaster(const uint8_t* entropy, size_t len);

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

  // ── ClientPIN ──────────────────────────────────────────────────────────
  // PIN file at /unigeek/utility/fido/pin.bin = retries(1) | pinLen(1) |
  // pinHash(16) — pinHash is SHA-256(utf8 PIN)[0..16]. retries decrements on
  // bad PIN, resets to kPinMaxRetries on success, hits 0 → PIN_BLOCKED until
  // CredentialStore::wipe().
  static bool isPinSet();
  static bool setPinHash       (const uint8_t pinHash[16], uint8_t pinLen);
  static bool getPinHash       (uint8_t pinHash[16], uint8_t* outPinLen,
                                uint8_t* outRetries);
  static bool resetPinRetries  ();
  static bool decrementPinRetries();
  static bool clearPin         ();

  // ── Resident credentials (Phase 5) ────────────────────────────────────
  // One file per discoverable credential under
  // /unigeek/utility/fido/credentials/<hex16_rp>_<hex16_user>.bin
  // hex16_rp   = lowercase hex of rpIdHash[0..7]
  // hex16_user = lowercase hex of SHA-256(userId)[0..7]

  struct ResidentCredRecord {
    uint8_t credId[kCredIdSize];     // wrapped credential ID (96 B)
    uint8_t rpIdHash[kRpIdHashSize]; // full rpIdHash for collision check (32 B)
    uint8_t userIdLen;               // actual byte length of userId (≤ 64)
    uint8_t userId[64];              // raw user.id bytes
    char    rpId[128];               // NUL-padded RP identifier
    char    userName[65];            // NUL-padded user.name (for CredentialManagement)
    // Total: 386 bytes — all 1-byte fields, no padding
  };

  using ResidentCredCb = void (*)(const ResidentCredRecord& rec, void* ctx);

  // Write (or overwrite) a resident credential for the given RP + userId.
  static bool writeResidentCred(const ResidentCredRecord& rec);

  // Enumerate all resident creds whose rpIdHash matches.
  // Calls cb for each match; returns the total number of calls made.
  static int  enumResidentCreds(const uint8_t rpIdHash[kRpIdHashSize],
                                 ResidentCredCb cb, void* ctx);

  // Enumerate ALL resident creds regardless of RP (for CredentialManagement).
  static int  enumAllResidentCreds(ResidentCredCb cb, void* ctx);

  // Delete the resident cred whose credId bytes match exactly.
  // Extracts rpIdHash from credId[16..48] to narrow the search.
  static bool deleteResidentCredById(const uint8_t credId[kCredIdSize]);

  // Delete all resident credential files (called from wipe).
  static void deleteAllResidentCreds();

  // ── AuthenticatorConfig (CTAP 2.1 §6.11) ───────────────────────────────
  // /unigeek/utility/fido/config.bin = flags(1) | minPinLen(1) | reserved(2)
  //   flags bit 0 = alwaysUv (always require UV on MC/GA)
  // Defaults (no file present): flags=0, minPinLen=4.
  static constexpr uint8_t kCfgFlagAlwaysUv = 0x01;
  static constexpr uint8_t kCfgPinLenDefault = 4;

  static bool    getAlwaysUv   ();
  static bool    setAlwaysUv   (bool on);
  static uint8_t getMinPinLen  ();
  static bool    setMinPinLen  (uint8_t len);

  // Wipe master + counter + resident creds + PIN + config. Master key is
  // then regenerated lazily on next encode call. All previously issued
  // credentials are rendered useless (intended).
  static bool wipe();
};

}  // namespace webauthn
