#include "CredentialStore.h"

#include <Arduino.h>

#ifdef DEVICE_HAS_WEBAUTHN

#include "WebAuthnConfig.h"
#include "WebAuthnCrypto.h"
#include "WebAuthnLog.h"

#include "core/Device.h"

#include <FS.h>
#include <string.h>

namespace webauthn {

namespace {

constexpr const char* kDir         = "/unigeek/utility/fido";
constexpr const char* kMasterPath  = "/unigeek/utility/fido/master.bin";
constexpr const char* kCounterPath = "/unigeek/utility/fido/counter.bin";
constexpr const char* kDevKeyPath  = "/unigeek/utility/fido/u2f_priv.bin";
constexpr const char* kDevCertPath = "/unigeek/utility/fido/u2f_cert.der";
constexpr const char* kPinPath     = "/unigeek/utility/fido/pin.bin";
constexpr const char* kCfgPath     = "/unigeek/utility/fido/config.bin";
constexpr const char* kCredsDir    = "/unigeek/utility/fido/credentials";

static const char kHex[] = "0123456789abcdef";

uint8_t  g_master[CredentialStore::kMasterKeySize];
bool     g_masterLoaded = false;
uint32_t g_counter      = 0;
bool     g_counterLoaded = false;

uint8_t  g_devKey[CredentialStore::kPrivKeySize];
bool     g_devKeyLoaded = false;

constexpr size_t kDevCertCap = 768;
uint8_t  g_devCert[kDevCertCap];
size_t   g_devCertLen = 0;
bool     g_devCertLoaded = false;

// Use the primary storage (SD if present, else LFS) — `Uni.Storage` is set
// by `Device::initStorage()`. Direct StorageLFS sometimes isn't populated
// on boards that mount SD as primary.
IStorage* storage() { return Uni.Storage; }

// Build the 37-char filename (+ NUL) for a resident cred file.
// Format: <hex16_rpIdHash[0..7]>_<hex16_sha256(userId)[0..7]>.bin
void credFileName(const uint8_t rpIdHash[32],
                  const uint8_t* userId, uint8_t userIdLen,
                  char out[38])
{
  for (int i = 0; i < 8; i++) {
    out[i * 2]     = kHex[rpIdHash[i] >> 4];
    out[i * 2 + 1] = kHex[rpIdHash[i] & 0x0F];
  }
  out[16] = '_';
  uint8_t uHash[32];
  WebAuthnCrypto::sha256(userId, userIdLen, uHash);
  for (int i = 0; i < 8; i++) {
    out[17 + i * 2]     = kHex[uHash[i] >> 4];
    out[17 + i * 2 + 1] = kHex[uHash[i] & 0x0F];
  }
  memcpy(out + 33, ".bin", 5);  // includes NUL
}

bool ensureDir()
{
  if (!storage()) {
    WA_LOG("CS ensureDir fail: Uni.Storage is null");
    return false;
  }
  if (!storage()->isAvailable()) {
    WA_LOG("CS ensureDir fail: Storage not available");
    return false;
  }
  if (storage()->exists(kDir)) return true;
  // Create parents recursively so makeDir succeeds on LFS (which doesn't
  // implicitly create intermediate dirs).
  if (!storage()->exists("/unigeek")) {
    bool ok = storage()->makeDir("/unigeek");
    WA_LOG("CS makeDir /unigeek -> %d", (int)ok);
  }
  if (!storage()->exists("/unigeek/utility")) {
    bool ok = storage()->makeDir("/unigeek/utility");
    WA_LOG("CS makeDir /unigeek/utility -> %d", (int)ok);
  }
  bool ok = storage()->makeDir(kDir);
  WA_LOG("CS makeDir %s -> %d", kDir, (int)ok);
  return ok;
}

bool ensureCredsDir()
{
  if (!ensureDir()) return false;
  if (!storage()) return false;
  if (storage()->exists(kCredsDir)) return true;
  bool ok = storage()->makeDir(kCredsDir);
  WA_LOG("CS makeDir %s -> %d", kCredsDir, (int)ok);
  return ok;
}

bool readBytes(const char* path, uint8_t* buf, size_t expected)
{
  if (!storage() || !storage()->exists(path)) return false;
  fs::File f = storage()->open(path, "r");
  if (!f) return false;
  size_t n = f.read(buf, expected);
  f.close();
  return n == expected;
}

bool writeBytes(const char* path, const uint8_t* buf, size_t len)
{
  if (!storage()) return false;
  fs::File f = storage()->open(path, "w");
  if (!f) return false;
  size_t n = f.write(buf, len);
  f.close();
  return n == len;
}

// Load master from disk. Does NOT auto-generate — use
// CredentialStore::generateMaster() once WiFi+NTP entropy is in place.
// Returns false when master.bin is absent.
bool loadMaster()
{
  if (g_masterLoaded) return true;
  if (!storage()) return false;
  if (!storage()->exists(kMasterPath)) return false;
  if (!readBytes(kMasterPath, g_master, sizeof(g_master))) {
    WA_LOG("CS loadMaster fail: read %s", kMasterPath);
    return false;
  }
  WA_LOG("CS master.bin loaded (%u B)", (unsigned)sizeof(g_master));
  g_masterLoaded = true;
  return true;
}

bool loadCounter()
{
  if (g_counterLoaded) { return true; }
  uint8_t buf[4];
  if (readBytes(kCounterPath, buf, 4)) {
    g_counter = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16)
              | ((uint32_t)buf[2] << 8)  |  (uint32_t)buf[3];
  } else {
    g_counter = 0;
  }
  g_counterLoaded = true;
  return true;
}

bool saveCounter()
{
  uint8_t buf[4] = {
    (uint8_t)((g_counter >> 24) & 0xFF), (uint8_t)((g_counter >> 16) & 0xFF),
    (uint8_t)((g_counter >>  8) & 0xFF), (uint8_t)( g_counter        & 0xFF),
  };
  return writeBytes(kCounterPath, buf, 4);
}

}  // namespace

bool CredentialStore::init()
{
  if (!WebAuthnCrypto::init()) return false;
  loadMaster();   // tolerates missing master.bin — see hasMaster()/generateMaster()
  loadCounter();
  return true;
}

bool CredentialStore::hasMaster()
{
  if (g_masterLoaded) return true;
  if (!storage())    return false;
  return storage()->exists(kMasterPath);
}

bool CredentialStore::generateMaster(bool force)
{
  if (!WebAuthnCrypto::init()) return false;
  if (!ensureDir())            return false;

  if (storage() && storage()->exists(kMasterPath)) {
    if (!force) {
      WA_LOG("CS generateMaster: master exists and force=false");
      return false;
    }
    // Wipe everything bound to the old master (creds, counter, PIN, config),
    // then reset RAM-side state so the next op picks up the fresh material.
    WA_LOG("CS generateMaster: regenerating - wiping prior state");
    if (!wipe()) {
      WA_LOG("CS generateMaster: wipe failed");
      return false;
    }
  }

  WebAuthnCrypto::random(g_master, sizeof(g_master));
  if (!writeBytes(kMasterPath, g_master, sizeof(g_master))) {
    WA_LOG("CS generateMaster fail: write %s", kMasterPath);
    memset(g_master, 0, sizeof(g_master));
    return false;
  }
  g_masterLoaded = true;
  WA_LOG("CS generateMaster ok");
  return true;
}

bool CredentialStore::restoreMaster(const uint8_t* entropy, size_t len)
{
  if (!entropy || len != kMasterKeySize) return false;
  if (!WebAuthnCrypto::init()) return false;
  if (!ensureDir())            return false;

  // Always wipe — restore replaces every secret bound to the old master.
  // wipe() handles the case where master.bin doesn't exist yet (no-op on
  // the missing files).
  if (!wipe()) {
    WA_LOG("CS restoreMaster: wipe failed");
    return false;
  }
  memcpy(g_master, entropy, kMasterKeySize);
  if (!writeBytes(kMasterPath, g_master, kMasterKeySize)) {
    WA_LOG("CS restoreMaster fail: write %s", kMasterPath);
    memset(g_master, 0, kMasterKeySize);
    return false;
  }
  g_masterLoaded = true;
  WA_LOG("CS restoreMaster ok");
  return true;
}

uint32_t CredentialStore::bumpCounter()
{
  if (!init()) return 0;
  g_counter++;
  saveCounter();
  return g_counter;
}

bool CredentialStore::encodeCredentialId(const uint8_t priv[kPrivKeySize],
                                         const uint8_t rpIdHash[kRpIdHashSize],
                                         uint8_t out[kCredIdSize])
{
  if (!init()) {
    WA_LOG("CS encodeCredentialId fail: init() returned false");
    return false;
  }
  if (!g_masterLoaded) {
    WA_LOG("CS encodeCredentialId fail: no master key (Generate BIP39 first)");
    return false;
  }
  // Layout: nonce(16) || rpIdHash(32) || ct(32) || tag(16)
  uint8_t* nonce = out;
  uint8_t* rid   = out + 16;
  uint8_t* ct    = out + 16 + 32;
  uint8_t* tag   = out + 16 + 32 + 32;

  WebAuthnCrypto::random(nonce, 16);
  memcpy(rid, rpIdHash, 32);
  if (!WebAuthnCrypto::aes256CbcEncrypt(g_master, nonce, priv, 32, ct)) {
    WA_LOG("CS encodeCredentialId fail: aes256CbcEncrypt");
    return false;
  }

  // tag = HMAC(masterKey, nonce || rpIdHash || ct), truncated to 16 bytes
  uint8_t mac[32];
  uint8_t macIn[16 + 32 + 32];
  memcpy(macIn,         nonce, 16);
  memcpy(macIn + 16,    rpIdHash, 32);
  memcpy(macIn + 16+32, ct, 32);
  WebAuthnCrypto::hmacSha256(g_master, sizeof(g_master),
                             macIn, sizeof(macIn), mac);
  memcpy(tag, mac, 16);
  return true;
}

bool CredentialStore::decodeCredentialId(const uint8_t* idBytes, size_t idLen,
                                         const uint8_t rpIdHash[kRpIdHashSize],
                                         uint8_t priv[kPrivKeySize])
{
  if (idLen != kCredIdSize) return false;
  if (!init())              return false;
  if (!g_masterLoaded)      return false;   // no master, nothing decrypts

  const uint8_t* nonce = idBytes;
  const uint8_t* rid   = idBytes + 16;
  const uint8_t* ct    = idBytes + 16 + 32;
  const uint8_t* tag   = idBytes + 16 + 32 + 32;

  // Bind to caller-provided rpIdHash — the embedded one must match.
  if (memcmp(rid, rpIdHash, 32) != 0) return false;

  // Verify tag (constant-time-ish — short array, the timing leak is the
  // 16-byte memcmp itself, which is acceptable per CTAP2 threat model).
  uint8_t mac[32];
  uint8_t macIn[16 + 32 + 32];
  memcpy(macIn,         nonce, 16);
  memcpy(macIn + 16,    rid,   32);
  memcpy(macIn + 16+32, ct,    32);
  WebAuthnCrypto::hmacSha256(g_master, sizeof(g_master),
                             macIn, sizeof(macIn), mac);

  uint8_t diff = 0;
  for (size_t i = 0; i < 16; i++) diff |= (uint8_t)(mac[i] ^ tag[i]);
  if (diff != 0) return false;

  return WebAuthnCrypto::aes256CbcDecrypt(g_master, nonce, ct, 32, priv);
}

bool CredentialStore::getMasterKey(uint8_t out[kMasterKeySize])
{
  if (!init()) return false;
  if (!g_masterLoaded) return false;
  memcpy(out, g_master, kMasterKeySize);
  return true;
}

bool CredentialStore::getDeviceKey(uint8_t out[kPrivKeySize])
{
  if (!init()) return false;
  if (g_devKeyLoaded) {
    memcpy(out, g_devKey, kPrivKeySize);
    return true;
  }

  if (readBytes(kDevKeyPath, g_devKey, kPrivKeySize)) {
    WA_LOG("CS u2f_priv.bin loaded");
    g_devKeyLoaded = true;
    memcpy(out, g_devKey, kPrivKeySize);
    return true;
  }

  // First boot — generate a fresh ECDSA P-256 keypair and persist the priv.
  WA_LOG("CS u2f_priv.bin not found, generating");
  uint8_t pub[65];
  if (!WebAuthnCrypto::ecdsaP256Keygen(g_devKey, pub)) {
    WA_LOG("CS getDeviceKey fail: ecdsaP256Keygen");
    return false;
  }
  if (!writeBytes(kDevKeyPath, g_devKey, kPrivKeySize)) {
    WA_LOG("CS getDeviceKey fail: writeBytes %s", kDevKeyPath);
    return false;
  }
  WA_LOG("CS u2f_priv.bin generated + written");
  g_devKeyLoaded = true;
  memcpy(out, g_devKey, kPrivKeySize);
  return true;
}

bool CredentialStore::getDeviceCert(const uint8_t** outCert, size_t* outLen)
{
  if (!init()) return false;
  if (g_devCertLoaded) {
    *outCert = g_devCert;
    *outLen  = g_devCertLen;
    return true;
  }

  if (storage()->exists(kDevCertPath)) {
    fs::File f = storage()->open(kDevCertPath, "r");
    if (f) {
      size_t n = f.read(g_devCert, sizeof(g_devCert));
      f.close();
      if (n > 0 && n <= sizeof(g_devCert)) {
        WA_LOG("CS u2f_cert.der loaded (%u B)", (unsigned)n);
        g_devCertLen = n;
        g_devCertLoaded = true;
        *outCert = g_devCert;
        *outLen  = g_devCertLen;
        return true;
      }
    }
  }

  // First boot — build a self-signed cert over the device priv key.
  WA_LOG("CS u2f_cert.der not found, generating");
  uint8_t devPriv[kPrivKeySize];
  if (!getDeviceKey(devPriv)) {
    WA_LOG("CS getDeviceCert fail: getDeviceKey");
    return false;
  }
  size_t n = 0;
  if (!WebAuthnCrypto::buildSelfSignedX509(devPriv, g_devCert, sizeof(g_devCert), &n)) {
    WA_LOG("CS getDeviceCert fail: buildSelfSignedX509");
    memset(devPriv, 0, sizeof(devPriv));
    return false;
  }
  memset(devPriv, 0, sizeof(devPriv));
  if (!writeBytes(kDevCertPath, g_devCert, n)) {
    WA_LOG("CS getDeviceCert fail: writeBytes %s", kDevCertPath);
    return false;
  }
  WA_LOG("CS u2f_cert.der generated + written (%u B)", (unsigned)n);
  g_devCertLen = n;
  g_devCertLoaded = true;
  *outCert = g_devCert;
  *outLen  = g_devCertLen;
  return true;
}

bool CredentialStore::isPinSet()
{
  if (!init()) return false;
  if (!storage()->exists(kPinPath)) return false;
  // Validate file is the expected 18 bytes; treat anything else as "not set".
  uint8_t buf[18];
  return readBytes(kPinPath, buf, sizeof(buf));
}

bool CredentialStore::setPinHash(const uint8_t pinHash[16], uint8_t pinLen)
{
  if (!init() || !ensureDir()) return false;
  uint8_t buf[18];
  buf[0] = kPinMaxRetries;
  buf[1] = pinLen;
  memcpy(buf + 2, pinHash, 16);
  return writeBytes(kPinPath, buf, sizeof(buf));
}

bool CredentialStore::getPinHash(uint8_t pinHash[16], uint8_t* outPinLen,
                                  uint8_t* outRetries)
{
  if (!init()) return false;
  uint8_t buf[18];
  if (!readBytes(kPinPath, buf, sizeof(buf))) return false;
  if (outRetries) *outRetries = buf[0];
  if (outPinLen)  *outPinLen  = buf[1];
  memcpy(pinHash, buf + 2, 16);
  return true;
}

bool CredentialStore::resetPinRetries()
{
  if (!init()) return false;
  uint8_t buf[18];
  if (!readBytes(kPinPath, buf, sizeof(buf))) return false;
  if (buf[0] == kPinMaxRetries) return true;  // no-op
  buf[0] = kPinMaxRetries;
  return writeBytes(kPinPath, buf, sizeof(buf));
}

bool CredentialStore::decrementPinRetries()
{
  if (!init()) return false;
  uint8_t buf[18];
  if (!readBytes(kPinPath, buf, sizeof(buf))) return false;
  if (buf[0] > 0) buf[0]--;
  return writeBytes(kPinPath, buf, sizeof(buf));
}

bool CredentialStore::clearPin()
{
  if (!storage()) return false;
  storage()->deleteFile(kPinPath);
  return true;
}

// ── AuthenticatorConfig storage ───────────────────────────────────────
// Config layout: byte0 flags, byte1 minPinLen, bytes2-3 reserved.
// Read/write through the same primary storage as everything else.

namespace {
bool readConfig(uint8_t out[4])
{
  if (!storage() || !storage()->exists(kCfgPath)) {
    out[0] = 0;
    out[1] = CredentialStore::kCfgPinLenDefault;
    out[2] = 0;
    out[3] = 0;
    return true;
  }
  return readBytes(kCfgPath, out, 4);
}

bool writeConfig(const uint8_t in[4])
{
  if (!storage() || !ensureDir()) return false;
  return writeBytes(kCfgPath, in, 4);
}
}  // namespace

bool CredentialStore::getAlwaysUv()
{
  uint8_t cfg[4];
  if (!readConfig(cfg)) return false;
  return (cfg[0] & kCfgFlagAlwaysUv) != 0;
}

bool CredentialStore::setAlwaysUv(bool on)
{
  uint8_t cfg[4];
  if (!readConfig(cfg)) return false;
  if (on)  cfg[0] |=  kCfgFlagAlwaysUv;
  else     cfg[0] &= ~kCfgFlagAlwaysUv;
  return writeConfig(cfg);
}

uint8_t CredentialStore::getMinPinLen()
{
  uint8_t cfg[4];
  if (!readConfig(cfg)) return kCfgPinLenDefault;
  uint8_t v = cfg[1];
  return (v < kCfgPinLenDefault) ? kCfgPinLenDefault : v;
}

bool CredentialStore::setMinPinLen(uint8_t len)
{
  if (len < kCfgPinLenDefault) return false;  // CTAP forbids shrinking
  uint8_t cfg[4];
  if (!readConfig(cfg)) return false;
  if (len < cfg[1]) return false;             // must only increase
  cfg[1] = len;
  return writeConfig(cfg);
}

bool CredentialStore::writeResidentCred(const ResidentCredRecord& rec)
{
  if (!init() || !ensureCredsDir()) return false;
  char fname[38];
  credFileName(rec.rpIdHash, rec.userId, rec.userIdLen, fname);
  char path[80];
  snprintf(path, sizeof(path), "%s/%s", kCredsDir, fname);
  bool ok = writeBytes(path, reinterpret_cast<const uint8_t*>(&rec), sizeof(rec));
  WA_LOG("CS writeResidentCred %s -> %d", fname, (int)ok);
  return ok;
}

int CredentialStore::enumResidentCreds(const uint8_t rpIdHash[kRpIdHashSize],
                                        ResidentCredCb cb, void* ctx)
{
  if (!storage() || !storage()->exists(kCredsDir)) return 0;

  // Build the 16-char hex prefix for rpIdHash[0..7] for fast pre-filter.
  char prefix[17];
  for (int i = 0; i < 8; i++) {
    prefix[i * 2]     = kHex[rpIdHash[i] >> 4];
    prefix[i * 2 + 1] = kHex[rpIdHash[i] & 0x0F];
  }
  prefix[16] = '\0';

  constexpr uint8_t kMaxEntries = 64;
  static IStorage::DirEntry entries[kMaxEntries];  // static: dispatch is single-threaded
  uint8_t n = storage()->listDir(kCredsDir, entries, kMaxEntries);

  int count = 0;
  static ResidentCredRecord rec;
  static char path[80];

  for (uint8_t i = 0; i < n; i++) {
    if (entries[i].isDir) continue;
    if (strncmp(entries[i].name.c_str(), prefix, 16) != 0) continue;
    snprintf(path, sizeof(path), "%s/%s", kCredsDir, entries[i].name.c_str());
    if (!readBytes(path, reinterpret_cast<uint8_t*>(&rec), sizeof(rec))) continue;
    if (memcmp(rec.rpIdHash, rpIdHash, kRpIdHashSize) != 0) continue;
    count++;
    cb(rec, ctx);
  }
  memset(&rec, 0, sizeof(rec));
  return count;
}

int CredentialStore::enumAllResidentCreds(ResidentCredCb cb, void* ctx)
{
  if (!storage() || !storage()->exists(kCredsDir)) return 0;

  constexpr uint8_t kMaxEntries = 64;
  static IStorage::DirEntry entries[kMaxEntries];
  uint8_t n = storage()->listDir(kCredsDir, entries, kMaxEntries);

  int count = 0;
  static ResidentCredRecord rec;
  static char path[80];

  for (uint8_t i = 0; i < n; i++) {
    if (entries[i].isDir) continue;
    const char* name = entries[i].name.c_str();
    size_t nlen = strlen(name);
    if (nlen < 4 || strcmp(name + nlen - 4, ".bin") != 0) continue;
    snprintf(path, sizeof(path), "%s/%s", kCredsDir, name);
    if (!readBytes(path, reinterpret_cast<uint8_t*>(&rec), sizeof(rec))) continue;
    count++;
    cb(rec, ctx);
  }
  memset(&rec, 0, sizeof(rec));
  return count;
}

bool CredentialStore::deleteResidentCredById(const uint8_t credId[kCredIdSize])
{
  if (!storage() || !storage()->exists(kCredsDir)) return false;

  // rpIdHash is embedded at credId[16..48]
  const uint8_t* rpIdHash = credId + 16;

  char prefix[17];
  for (int i = 0; i < 8; i++) {
    prefix[i * 2]     = kHex[rpIdHash[i] >> 4];
    prefix[i * 2 + 1] = kHex[rpIdHash[i] & 0x0F];
  }
  prefix[16] = '\0';

  constexpr uint8_t kMaxEntries = 64;
  static IStorage::DirEntry entries[kMaxEntries];
  uint8_t n = storage()->listDir(kCredsDir, entries, kMaxEntries);

  static ResidentCredRecord rec;
  static char path[80];

  for (uint8_t i = 0; i < n; i++) {
    if (entries[i].isDir) continue;
    if (strncmp(entries[i].name.c_str(), prefix, 16) != 0) continue;
    snprintf(path, sizeof(path), "%s/%s", kCredsDir, entries[i].name.c_str());
    if (!readBytes(path, reinterpret_cast<uint8_t*>(&rec), sizeof(rec))) continue;
    if (memcmp(rec.credId, credId, kCredIdSize) != 0) continue;
    bool ok = storage()->deleteFile(path);
    memset(&rec, 0, sizeof(rec));
    WA_LOG("CS deleteResidentCredById %s -> %d", entries[i].name.c_str(), (int)ok);
    return ok;
  }
  memset(&rec, 0, sizeof(rec));
  WA_LOG("CS deleteResidentCredById: cred not found");
  return false;
}

void CredentialStore::deleteAllResidentCreds()
{
  if (!storage() || !storage()->exists(kCredsDir)) return;

  constexpr uint8_t kMaxEntries = 64;
  static IStorage::DirEntry entries[kMaxEntries];
  uint8_t n = storage()->listDir(kCredsDir, entries, kMaxEntries);

  static char path[80];
  for (uint8_t i = 0; i < n; i++) {
    if (entries[i].isDir) continue;
    snprintf(path, sizeof(path), "%s/%s", kCredsDir, entries[i].name.c_str());
    storage()->deleteFile(path);
  }
  storage()->removeDir(kCredsDir);
  WA_LOG("CS deleteAllResidentCreds: removed %u files", (unsigned)n);
}

bool CredentialStore::wipe()
{
  if (!storage()) return false;
  storage()->deleteFile(kMasterPath);
  storage()->deleteFile(kCounterPath);
  storage()->deleteFile(kDevKeyPath);
  storage()->deleteFile(kDevCertPath);
  storage()->deleteFile(kPinPath);
  storage()->deleteFile(kCfgPath);
  deleteAllResidentCreds();
  g_devKeyLoaded   = false;
  g_devCertLoaded  = false;
  g_devCertLen     = 0;
  memset(g_devKey,  0, sizeof(g_devKey));
  memset(g_devCert, 0, sizeof(g_devCert));
  g_masterLoaded  = false;
  g_counterLoaded = false;
  g_counter       = 0;
  memset(g_master, 0, sizeof(g_master));
  return true;
}

}  // namespace webauthn

#endif  // DEVICE_HAS_WEBAUTHN
