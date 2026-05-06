#include "CredentialStore.h"

#include <Arduino.h>

#ifdef DEVICE_HAS_WEBAUTHN

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

bool loadOrGenMaster()
{
  if (g_masterLoaded) return true;
  if (!ensureDir()) {
    WA_LOG("CS loadOrGenMaster fail: ensureDir");
    return false;
  }
  if (readBytes(kMasterPath, g_master, sizeof(g_master))) {
    WA_LOG("CS master.bin loaded (%u B)", (unsigned)sizeof(g_master));
    g_masterLoaded = true;
    return true;
  }
  // First boot — generate.
  WA_LOG("CS master.bin not found, generating");
  WebAuthnCrypto::random(g_master, sizeof(g_master));
  if (!writeBytes(kMasterPath, g_master, sizeof(g_master))) {
    WA_LOG("CS loadOrGenMaster fail: writeBytes %s", kMasterPath);
    return false;
  }
  WA_LOG("CS master.bin generated + written");
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
  if (!loadOrGenMaster())      return false;
  loadCounter();
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

bool CredentialStore::wipe()
{
  if (!storage()) return false;
  storage()->deleteFile(kMasterPath);
  storage()->deleteFile(kCounterPath);
  storage()->deleteFile(kDevKeyPath);
  storage()->deleteFile(kDevCertPath);
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
