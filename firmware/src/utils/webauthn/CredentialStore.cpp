#include "CredentialStore.h"

#include <Arduino.h>

#ifdef DEVICE_HAS_WEBAUTHN

#include "WebAuthnCrypto.h"

#include "core/Device.h"

#include <FS.h>
#include <string.h>

namespace webauthn {

namespace {

constexpr const char* kDir         = "/unigeek/webauthn";
constexpr const char* kMasterPath  = "/unigeek/webauthn/master.bin";
constexpr const char* kCounterPath = "/unigeek/webauthn/counter.bin";

uint8_t  g_master[CredentialStore::kMasterKeySize];
bool     g_masterLoaded = false;
uint32_t g_counter      = 0;
bool     g_counterLoaded = false;

IStorage* lfs() { return Uni.StorageLFS; }

bool ensureDir()
{
  if (!lfs() || !lfs()->isAvailable()) return false;
  if (lfs()->exists(kDir)) return true;
  // Create the parent /unigeek if missing so makeDir doesn't fail on LFS.
  if (!lfs()->exists("/unigeek")) lfs()->makeDir("/unigeek");
  return lfs()->makeDir(kDir);
}

bool readBytes(const char* path, uint8_t* buf, size_t expected)
{
  if (!lfs() || !lfs()->exists(path)) return false;
  fs::File f = lfs()->open(path, "r");
  if (!f) return false;
  size_t n = f.read(buf, expected);
  f.close();
  return n == expected;
}

bool writeBytes(const char* path, const uint8_t* buf, size_t len)
{
  if (!lfs()) return false;
  fs::File f = lfs()->open(path, "w");
  if (!f) return false;
  size_t n = f.write(buf, len);
  f.close();
  return n == len;
}

bool loadOrGenMaster()
{
  if (g_masterLoaded) return true;
  if (!ensureDir())   return false;
  if (readBytes(kMasterPath, g_master, sizeof(g_master))) {
    g_masterLoaded = true;
    return true;
  }
  // First boot — generate.
  WebAuthnCrypto::random(g_master, sizeof(g_master));
  if (!writeBytes(kMasterPath, g_master, sizeof(g_master))) return false;
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
  if (!init()) return false;
  // Layout: nonce(16) || rpIdHash(32) || ct(32) || tag(16)
  uint8_t* nonce = out;
  uint8_t* rid   = out + 16;
  uint8_t* ct    = out + 16 + 32;
  uint8_t* tag   = out + 16 + 32 + 32;

  WebAuthnCrypto::random(nonce, 16);
  memcpy(rid, rpIdHash, 32);
  if (!WebAuthnCrypto::aes256CbcEncrypt(g_master, nonce, priv, 32, ct))
    return false;

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

bool CredentialStore::wipe()
{
  if (!lfs()) return false;
  lfs()->deleteFile(kMasterPath);
  lfs()->deleteFile(kCounterPath);
  g_masterLoaded  = false;
  g_counterLoaded = false;
  g_counter       = 0;
  memset(g_master, 0, sizeof(g_master));
  return true;
}

}  // namespace webauthn

#endif  // DEVICE_HAS_WEBAUTHN
