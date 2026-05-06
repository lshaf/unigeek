#include "U2f.h"

#include <Arduino.h>

#ifdef DEVICE_HAS_WEBAUTHN

#include "WebAuthnCrypto.h"
#include "CredentialStore.h"
#include "WebAuthnLog.h"

#include <string.h>

namespace webauthn {

namespace {

U2f::UserPresenceFn g_upFn   = nullptr;
void*               g_upUser = nullptr;

bool requestUp(const char* rpId)
{
  if (!g_upFn) return true;  // auto-confirm if no UI hook installed
  return g_upFn(rpId, g_upUser);
}

// ISO 7816 status words
constexpr uint16_t SW_NO_ERROR             = 0x9000;
constexpr uint16_t SW_CONDITIONS_NOT_MET   = 0x6985;
constexpr uint16_t SW_WRONG_DATA           = 0x6A80;
constexpr uint16_t SW_WRONG_LENGTH         = 0x6700;
constexpr uint16_t SW_INS_NOT_SUPPORTED    = 0x6D00;
constexpr uint16_t SW_GENERIC_ERROR        = 0x6F00;

constexpr uint8_t U2F_INS_REGISTER     = 0x01;
constexpr uint8_t U2F_INS_AUTHENTICATE = 0x02;
constexpr uint8_t U2F_INS_VERSION      = 0x03;

constexpr uint8_t U2F_AUTH_CHECK_ONLY      = 0x07;
constexpr uint8_t U2F_AUTH_ENFORCE         = 0x03;
constexpr uint8_t U2F_AUTH_DONT_ENFORCE    = 0x08;

uint16_t finishStatus(uint8_t* out, uint16_t off, uint16_t sw)
{
  out[off++] = (uint8_t)(sw >> 8);
  out[off++] = (uint8_t)(sw & 0xFF);
  return off;
}

uint16_t shortStatus(uint8_t* out, uint16_t sw) { return finishStatus(out, 0, sw); }

// Decode an extended-length APDU. Returns true on success and fills out
// data pointer + length. Le is parsed but ignored (we always send max).
bool parseApdu(const uint8_t* apdu, uint16_t apduLen,
               uint8_t* cla, uint8_t* ins, uint8_t* p1, uint8_t* p2,
               const uint8_t** data, uint16_t* dataLen)
{
  if (apduLen < 4) return false;
  *cla = apdu[0]; *ins = apdu[1]; *p1 = apdu[2]; *p2 = apdu[3];
  if (apduLen == 4) { *data = nullptr; *dataLen = 0; return true; }
  // Extended length only: byte 4 must be 0x00, then Lc(2 BE).
  if (apduLen >= 7 && apdu[4] == 0x00) {
    uint16_t lc = ((uint16_t)apdu[5] << 8) | apdu[6];
    if ((uint32_t)7 + lc > apduLen) return false;
    *data = apdu + 7;
    *dataLen = lc;
    return true;
  }
  // Short length form (rare on USB but handle it).
  uint8_t lc = apdu[4];
  if ((uint16_t)5 + lc > apduLen) return false;
  *data = apdu + 5;
  *dataLen = lc;
  return true;
}

uint16_t handleRegister(const uint8_t* data, uint16_t dataLen,
                        uint8_t* out, uint16_t outMax)
{
  // data = challenge(32) || application(32)
  if (dataLen != 64) {
    WA_LOG("U2F REG fail: dataLen=%u (want 64)", (unsigned)dataLen);
    return shortStatus(out, SW_WRONG_DATA);
  }
  const uint8_t* challenge   = data;
  const uint8_t* application = data + 32;

  WA_LOG("U2F REG: requesting UP");
  if (!requestUp("(U2F register)")) {
    WA_LOG("U2F REG fail: UP denied/timeout");
    return shortStatus(out, SW_CONDITIONS_NOT_MET);
  }
  WA_LOG("U2F REG: UP granted");

  // 1. Per-cred ECDSA P-256 keypair.
  uint8_t credPriv[32], credPub[65];
  if (!WebAuthnCrypto::ecdsaP256Keygen(credPriv, credPub)) {
    WA_LOG("U2F REG fail: cred keygen");
    return shortStatus(out, SW_GENERIC_ERROR);
  }

  // 2. Wrap into our 96-byte handle (same format as CTAP2 cred IDs).
  uint8_t handle[CredentialStore::kCredIdSize];
  if (!CredentialStore::encodeCredentialId(credPriv, application, handle)) {
    WA_LOG("U2F REG fail: encodeCredentialId");
    memset(credPriv, 0, sizeof(credPriv));
    return shortStatus(out, SW_GENERIC_ERROR);
  }

  // 3. Pull the device key + cert (lazily generated on first use).
  uint8_t devPriv[32];
  if (!CredentialStore::getDeviceKey(devPriv)) {
    WA_LOG("U2F REG fail: getDeviceKey");
    memset(credPriv, 0, sizeof(credPriv));
    return shortStatus(out, SW_GENERIC_ERROR);
  }
  const uint8_t* cert = nullptr;
  size_t  certLen = 0;
  if (!CredentialStore::getDeviceCert(&cert, &certLen)) {
    WA_LOG("U2F REG fail: getDeviceCert");
    memset(credPriv, 0, sizeof(credPriv));
    memset(devPriv,  0, sizeof(devPriv));
    return shortStatus(out, SW_GENERIC_ERROR);
  }

  // 4. Attestation signature input:
  //      0x00 || application(32) || challenge(32) || handle(96) || pubKey(65)
  static uint8_t sigInput[1 + 32 + 32 + CredentialStore::kCredIdSize + 65];
  size_t off = 0;
  sigInput[off++] = 0x00;
  memcpy(sigInput + off, application, 32);             off += 32;
  memcpy(sigInput + off, challenge,   32);             off += 32;
  memcpy(sigInput + off, handle, sizeof(handle));      off += sizeof(handle);
  memcpy(sigInput + off, credPub, 65);                 off += 65;

  uint8_t sigHash[32];
  WebAuthnCrypto::sha256(sigInput, off, sigHash);
  uint8_t sigDer[72]; size_t sigLen = 0;
  bool sigOk = WebAuthnCrypto::ecdsaP256SignDer(devPriv, sigHash, sigDer, &sigLen);
  memset(devPriv,  0, sizeof(devPriv));
  memset(credPriv, 0, sizeof(credPriv));
  if (!sigOk) {
    WA_LOG("U2F REG fail: device-key sign");
    return shortStatus(out, SW_GENERIC_ERROR);
  }

  // 5. Build response: 0x05 || pubKey(65) || handleLen(1) || handle(96) || cert || sig || SW.
  size_t need = 1 + 65 + 1 + sizeof(handle) + certLen + sigLen + 2;
  if (need > outMax) {
    WA_LOG("U2F REG fail: response overflow (need=%u outMax=%u)",
           (unsigned)need, (unsigned)outMax);
    return shortStatus(out, SW_GENERIC_ERROR);
  }
  uint16_t r = 0;
  out[r++] = 0x05;
  memcpy(out + r, credPub, 65);                        r += 65;
  out[r++] = (uint8_t)sizeof(handle);
  memcpy(out + r, handle, sizeof(handle));             r += sizeof(handle);
  memcpy(out + r, cert, certLen);                      r += (uint16_t)certLen;
  memcpy(out + r, sigDer, sigLen);                     r += (uint16_t)sigLen;

  WA_LOG("U2F REG ok: certLen=%u sigLen=%u total=%u",
         (unsigned)certLen, (unsigned)sigLen, (unsigned)(r + 2));
  return finishStatus(out, r, SW_NO_ERROR);
}

uint16_t handleVersion(uint8_t* out, uint16_t outMax)
{
  static const char kVer[] = "U2F_V2";
  if (outMax < sizeof(kVer) - 1 + 2) return shortStatus(out, SW_GENERIC_ERROR);
  memcpy(out, kVer, sizeof(kVer) - 1);
  return finishStatus(out, sizeof(kVer) - 1, SW_NO_ERROR);
}

uint16_t handleAuthenticate(uint8_t p1, const uint8_t* data, uint16_t dataLen,
                            uint8_t* out, uint16_t outMax)
{
  // data = challenge(32) || application(32) || keyHandleLen(1) || keyHandle(L)
  if (dataLen < 32 + 32 + 1) return shortStatus(out, SW_WRONG_DATA);
  const uint8_t* challenge   = data;
  const uint8_t* application = data + 32;
  uint8_t        khLen       = data[64];
  if (dataLen != (uint16_t)(65 + khLen))    return shortStatus(out, SW_WRONG_DATA);
  if (khLen != CredentialStore::kCredIdSize) return shortStatus(out, SW_WRONG_DATA);
  const uint8_t* kh = data + 65;

  // Verify the key handle was issued for this application (rpIdHash).
  uint8_t priv[32];
  if (!CredentialStore::decodeCredentialId(kh, khLen, application, priv))
    return shortStatus(out, SW_WRONG_DATA);

  // Check-only: confirm the handle is valid without performing a sig.
  // Spec mandates SW_CONDITIONS_NOT_MET on success here (per U2F TUP rules).
  if (p1 == U2F_AUTH_CHECK_ONLY)
    return shortStatus(out, SW_CONDITIONS_NOT_MET);

  // ENFORCE / DONT_ENFORCE: produce signature. Gate on UP callback (the
  // spec says ENFORCE *requires* UP, and the same callback drives the
  // CONFIRM screen used by CTAP2 GetAssertion).
  if (p1 == U2F_AUTH_ENFORCE) {
    if (!requestUp("(U2F authenticate)")) {
      return shortStatus(out, SW_CONDITIONS_NOT_MET);
    }
  }
  uint32_t counter = CredentialStore::bumpCounter();
  uint8_t  userPresence = (p1 == U2F_AUTH_ENFORCE) ? 0x01 : 0x00;

  // Sig over: application(32) || userPresence(1) || counter(4 BE) || challenge(32)
  uint8_t sigInput[32 + 1 + 4 + 32];
  size_t  off = 0;
  memcpy(sigInput + off, application, 32);   off += 32;
  sigInput[off++] = userPresence;
  sigInput[off++] = (uint8_t)((counter >> 24) & 0xFF);
  sigInput[off++] = (uint8_t)((counter >> 16) & 0xFF);
  sigInput[off++] = (uint8_t)((counter >>  8) & 0xFF);
  sigInput[off++] = (uint8_t)( counter        & 0xFF);
  memcpy(sigInput + off, challenge, 32);     off += 32;

  uint8_t hash[32];
  WebAuthnCrypto::sha256(sigInput, off, hash);
  uint8_t sigDer[72]; size_t sigLen = 0;
  if (!WebAuthnCrypto::ecdsaP256SignDer(priv, hash, sigDer, &sigLen))
    return shortStatus(out, SW_GENERIC_ERROR);

  // Response: userPresence(1) | counter(4 BE) | sig(DER) | SW
  uint16_t respOff = 0;
  if ((size_t)5 + sigLen + 2 > outMax) return shortStatus(out, SW_GENERIC_ERROR);
  out[respOff++] = userPresence;
  out[respOff++] = (uint8_t)((counter >> 24) & 0xFF);
  out[respOff++] = (uint8_t)((counter >> 16) & 0xFF);
  out[respOff++] = (uint8_t)((counter >>  8) & 0xFF);
  out[respOff++] = (uint8_t)( counter        & 0xFF);
  memcpy(out + respOff, sigDer, sigLen);     respOff += sigLen;
  return finishStatus(out, respOff, SW_NO_ERROR);
}

}  // namespace

uint16_t U2f::handleApdu(const uint8_t* apdu, uint16_t apduLen,
                         uint8_t* out, uint16_t outMax)
{
  uint8_t cla, ins, p1, p2;
  const uint8_t* data; uint16_t dataLen;
  if (!parseApdu(apdu, apduLen, &cla, &ins, &p1, &p2, &data, &dataLen))
    return shortStatus(out, SW_WRONG_LENGTH);

  if (cla != 0x00) return shortStatus(out, SW_INS_NOT_SUPPORTED);

  switch (ins) {
    case U2F_INS_VERSION:      return handleVersion(out, outMax);
    case U2F_INS_AUTHENTICATE: return handleAuthenticate(p1, data, dataLen, out, outMax);
    case U2F_INS_REGISTER:     return handleRegister(data, dataLen, out, outMax);
    default:                   return shortStatus(out, SW_INS_NOT_SUPPORTED);
  }
}

void U2f::setUserPresenceFn(UserPresenceFn fn, void* user)
{
  g_upFn   = fn;
  g_upUser = user;
}

}  // namespace webauthn

#endif  // DEVICE_HAS_WEBAUTHN
