#include "Ctap2.h"

#include <Arduino.h>

#ifdef DEVICE_HAS_WEBAUTHN

#include "Cbor.h"
#include "WebAuthnConfig.h"
#include "WebAuthnCrypto.h"
#include "CredentialStore.h"
#include "U2f.h"
#include "WebAuthnLog.h"

#include <string.h>

namespace webauthn {

namespace {

Ctap2::UserPresenceFn g_upFn   = nullptr;
void*                 g_upUser = nullptr;

// authData flag bits
constexpr uint8_t FLAG_UP = 0x01;
constexpr uint8_t FLAG_UV = 0x04;
constexpr uint8_t FLAG_AT = 0x40;
constexpr uint8_t FLAG_ED = 0x80;  // extension data present

// Single-byte CTAP2 status response
inline uint16_t statusOnly(uint8_t* out, uint8_t status)
{
  out[0] = status;
  return 1;
}

// Convenience: copy a CBOR text string at the reader position into a heap-
// safe stack buffer; returns false on error or if cap is exceeded. Sets
// `*outLen` to the trimmed length. The buffer is NUL-terminated when
// possible (for use as a regular C string).
bool readTextInto(CborReader& r, char* dst, size_t cap, size_t* outLen)
{
  const char* s; size_t n;
  if (!r.readText(&s, &n)) return false;
  if (n + 1 > cap) return false;
  memcpy(dst, s, n);
  dst[n] = '\0';
  if (outLen) *outLen = n;
  return true;
}

// Build the COSE EC2 public key CBOR for the given uncompressed pubkey
// (0x04 || X(32) || Y(32)). Writes into `dst` and returns bytes written
// or 0 on error.
size_t writeCoseKey(const uint8_t pub65[65], uint8_t* dst, size_t cap)
{
  CborWriter w(dst, cap);
  w.beginMap(5);
  w.putUint(1);         w.putUint(2);     // kty: EC2
  w.putUint(3);         w.putInt(-7);     // alg: ES256
  w.putInt(-1);         w.putUint(1);     // crv: P-256
  w.putInt(-2);         w.putBytes(pub65 + 1, 32);   // x
  w.putInt(-3);         w.putBytes(pub65 + 33, 32);  // y
  return w.ok() ? w.size() : 0;
}

// Same shape as writeCoseKey but advertised with alg -25 (ECDH-ES + HKDF-256)
// — the COSE alg identifier the platform expects for the ClientPIN /
// hmac-secret key-agreement key, NOT the signing key.
size_t writeCoseEcdhKey(const uint8_t pub65[65], uint8_t* dst, size_t cap)
{
  CborWriter w(dst, cap);
  w.beginMap(5);
  w.putUint(1);         w.putUint(2);                       // kty: EC2
  w.putUint(3);         w.putInt(COSE_ECDH_HKDF_256);       // alg: -25
  w.putInt(-1);         w.putUint(1);                       // crv: P-256
  w.putInt(-2);         w.putBytes(pub65 + 1, 32);          // x
  w.putInt(-3);         w.putBytes(pub65 + 33, 32);         // y
  return w.ok() ? w.size() : 0;
}

// Parse a COSE_Key map (peer's ECDH P-256 public key) from a CBOR reader
// already positioned at the map header. Writes uncompressed `0x04 || X || Y`
// into pub65[65]. Returns false on any structural / type error.
bool readCoseEcdhKey(CborReader& r, uint8_t pub65[65])
{
  size_t mapCount = 0;
  if (!r.readMapHeader(&mapCount)) return false;
  pub65[0] = 0x04;
  bool gotX = false, gotY = false;
  for (size_t i = 0; i < mapCount; i++) {
    int64_t k;
    if (!r.readInt(&k)) return false;
    if (k == -2) {
      const uint8_t* p; size_t n;
      if (!r.readBytes(&p, &n) || n != 32) return false;
      memcpy(pub65 + 1, p, 32);
      gotX = true;
    } else if (k == -3) {
      const uint8_t* p; size_t n;
      if (!r.readBytes(&p, &n) || n != 32) return false;
      memcpy(pub65 + 33, p, 32);
      gotY = true;
    } else {
      r.skip();  // ignore kty / alg / crv — caller only needs the point
    }
  }
  return gotX && gotY;
}

// Compose authData into `out` and return its length.
//   authData = rpIdHash(32) | flags(1) | counter(4 BE) | [attCredData] | [ext]
size_t writeAuthData(uint8_t* out, size_t cap,
                     const uint8_t rpIdHash[32], uint8_t flags, uint32_t counter,
                     const uint8_t* attCred, size_t attCredLen,
                     const uint8_t* ext = nullptr, size_t extLen = 0)
{
  size_t need = 32 + 1 + 4 + attCredLen + extLen;
  if (need > cap) return 0;
  size_t off = 0;
  memcpy(out + off, rpIdHash, 32);            off += 32;
  out[off++] = flags;
  out[off++] = (uint8_t)((counter >> 24) & 0xFF);
  out[off++] = (uint8_t)((counter >> 16) & 0xFF);
  out[off++] = (uint8_t)((counter >>  8) & 0xFF);
  out[off++] = (uint8_t)( counter        & 0xFF);
  if (attCred && attCredLen) {
    memcpy(out + off, attCred, attCredLen);
    off += attCredLen;
  }
  if (ext && extLen) {
    memcpy(out + off, ext, extLen);
    off += extLen;
  }
  return off;
}

// Compose attestedCredentialData
//   aaguid(16) | credIdLen(2 BE) | credId(L) | credPubKeyCBOR
size_t writeAttCredData(uint8_t* out, size_t cap,
                        const uint8_t* credId, size_t credIdLen,
                        const uint8_t* coseKey, size_t coseKeyLen)
{
  size_t need = 16 + 2 + credIdLen + coseKeyLen;
  if (need > cap) return 0;
  size_t off = 0;
  memcpy(out + off, kAAGUID, 16);             off += 16;
  out[off++] = (uint8_t)((credIdLen >> 8) & 0xFF);
  out[off++] = (uint8_t)( credIdLen        & 0xFF);
  memcpy(out + off, credId, credIdLen);       off += credIdLen;
  memcpy(out + off, coseKey, coseKeyLen);     off += coseKeyLen;
  return off;
}

bool requestUserPresence(const char* rpId)
{
  if (!g_upFn) return true;  // auto-confirm if no UI hook installed
  return g_upFn(rpId, g_upUser);
}

}  // namespace

void Ctap2::setUserPresenceFn(UserPresenceFn fn, void* user)
{
  g_upFn   = fn;
  g_upUser = user;
}

// ── GetInfo ────────────────────────────────────────────────────────────

uint16_t Ctap2::_handleGetInfo(const uint8_t*, uint16_t,
                               uint8_t* out, uint16_t outMax)
{
  // Status byte at out[0]; CBOR map starts at out[1].
  out[0] = CTAP2_OK;
  CborWriter w(out + 1, outMax - 1);

  // Map keys must be in canonical (ascending) order:
  //   1 versions, 2 extensions, 3 aaguid, 4 options, 5 maxMsgSize,
  //   6 pinUvAuthProtocols, 9 transports, 10 algorithms
  w.beginMap(8);

  // 0x01 versions — advertise CTAP 2.1 since we emit 2.1-only keys
  // (transports 0x09, algorithms 0x0A). U2F_V2 is for CTAP1/AUTHENTICATE
  // backward compat.
  w.putUint(0x01);
  w.beginArray(3);
    w.putText("FIDO_2_0");
    w.putText("FIDO_2_1");
    w.putText("U2F_V2");

  // 0x02 extensions
  w.putUint(0x02);
  w.beginArray(1);
    w.putText("hmac-secret");

  // 0x03 aaguid
  w.putUint(0x03);
  w.putBytes(kAAGUID, sizeof(kAAGUID));

  // 0x04 options. Per CTAP2 spec:
  //   rk default = false → omit, since we don't support resident keys yet.
  //   clientPin: presence-with-false would advertise PIN *support*, which we
  //     don't have. Omitting the key signals "not supported".
  //   up default = true → still emit explicitly so hosts don't have to assume.
  w.putUint(0x04);
  w.beginMap(1);
    w.putText("up");        w.putBool(true);

  // 0x05 maxMsgSize
  w.putUint(0x05);
  w.putUint(kCtapMaxMsgSize);

  // 0x06 pinUvAuthProtocols — advertise v1 because hmac-secret negotiation
  // shares the same getKeyAgreement subcommand used by ClientPIN. Setting
  // a PIN is not supported (clientPin option not advertised).
  w.putUint(0x06);
  w.beginArray(1);
    w.putUint(1);

  // 0x09 transports
  w.putUint(0x09);
  w.beginArray(1);
    w.putText("usb");

  // 0x0A algorithms — CTAP 2.1 wants this even for ES256-only authenticators.
  // Each entry: { "alg": -7, "type": "public-key" }. Map keys ("alg" before
  // "type") are in canonical text-string order: same length (3 < 4), so lex.
  w.putUint(0x0A);
  w.beginArray(1);
    w.beginMap(2);
      w.putText("alg");  w.putInt(COSE_ES256);
      w.putText("type"); w.putText("public-key");

  if (!w.ok()) return statusOnly(out, CTAP2_ERR_PROCESSING);
  return (uint16_t)(1 + w.size());
}

// ── MakeCredential ─────────────────────────────────────────────────────

uint16_t Ctap2::_handleMakeCredential(const uint8_t* req, uint16_t reqLen,
                                       uint8_t* out, uint16_t outMax)
{
  CborReader r(req, reqLen);
  size_t mapCount = 0;
  if (!r.readMapHeader(&mapCount)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);

  uint8_t  clientDataHash[32];
  bool     gotCdh = false;
  char     rpId[128]; size_t rpIdLen = 0;
  uint8_t  userId[64]; size_t userIdLen = 0;
  bool     hasEs256 = false;
  bool     reqHmacSecret = false;

  for (size_t i = 0; i < mapCount; i++) {
    uint64_t k;
    if (!r.readUint(&k)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
    switch (k) {
      case 0x01: {  // clientDataHash
        const uint8_t* p; size_t n;
        if (!r.readBytes(&p, &n) || n != 32) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
        memcpy(clientDataHash, p, 32);
        gotCdh = true;
        break;
      }
      case 0x02: {  // rp = { id: text, name?: text }
        size_t rpMap; if (!r.readMapHeader(&rpMap)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
        for (size_t j = 0; j < rpMap; j++) {
          const char* key; size_t klen;
          if (!r.readText(&key, &klen)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
          if (klen == 2 && memcmp(key, "id", 2) == 0) {
            if (!readTextInto(r, rpId, sizeof(rpId), &rpIdLen))
              return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
          } else {
            r.skip();
          }
        }
        break;
      }
      case 0x03: {  // user = { id: bytes, name?: text, displayName?: text }
        size_t uMap; if (!r.readMapHeader(&uMap)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
        for (size_t j = 0; j < uMap; j++) {
          const char* key; size_t klen;
          if (!r.readText(&key, &klen)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
          if (klen == 2 && memcmp(key, "id", 2) == 0) {
            const uint8_t* p; size_t n;
            if (!r.readBytes(&p, &n) || n > sizeof(userId))
              return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
            memcpy(userId, p, n);
            userIdLen = n;
          } else {
            r.skip();
          }
        }
        break;
      }
      case 0x04: {  // pubKeyCredParams = [{ alg, type } ...]
        size_t arr; if (!r.readArrayHeader(&arr)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
        for (size_t j = 0; j < arr; j++) {
          size_t pMap; if (!r.readMapHeader(&pMap)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
          int64_t alg = 0;
          bool    isPubKey = false;
          for (size_t k2 = 0; k2 < pMap; k2++) {
            const char* key; size_t klen;
            if (!r.readText(&key, &klen)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
            if (klen == 3 && memcmp(key, "alg", 3) == 0) {
              if (!r.readInt(&alg)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
            } else if (klen == 4 && memcmp(key, "type", 4) == 0) {
              const char* t; size_t tlen;
              if (!r.readText(&t, &tlen)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
              if (tlen == 10 && memcmp(t, "public-key", 10) == 0) isPubKey = true;
            } else {
              r.skip();
            }
          }
          if (isPubKey && alg == COSE_ES256) hasEs256 = true;
        }
        break;
      }
      case 0x06: {  // extensions — text-keyed map
        size_t extMap;
        if (!r.readMapHeader(&extMap)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
        for (size_t j = 0; j < extMap; j++) {
          const char* key; size_t klen;
          if (!r.readText(&key, &klen)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
          if (klen == 11 && memcmp(key, "hmac-secret", 11) == 0) {
            bool b;
            if (!r.readBool(&b)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
            if (b) reqHmacSecret = true;
          } else {
            r.skip();
          }
        }
        break;
      }
      default:
        r.skip();  // ignore unknown / not-yet-supported parameters
        break;
    }
    if (!r.ok()) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
  }

  if (!gotCdh || rpIdLen == 0) {
    WA_LOG("MC fail: missing cdh/rpId (gotCdh=%d rpIdLen=%u)", (int)gotCdh, (unsigned)rpIdLen);
    return statusOnly(out, CTAP2_ERR_MISSING_PARAMETER);
  }
  if (!hasEs256) {
    WA_LOG("MC fail: no ES256 in pubKeyCredParams");
    return statusOnly(out, CTAP2_ERR_UNSUPPORTED_ALGORITHM);
  }

  // ── User presence ───────────────────────────────────────────────────
  WA_LOG("MC: requesting UP for rp=%s", rpId);
  if (!requestUserPresence(rpId)) {
    WA_LOG("MC fail: UP denied/timeout");
    return statusOnly(out, CTAP2_ERR_OPERATION_DENIED);
  }
  WA_LOG("MC: UP granted, generating keypair");

  // ── Generate credential keypair ────────────────────────────────────
  uint8_t priv[32], pub[65];
  if (!WebAuthnCrypto::ecdsaP256Keygen(priv, pub)) {
    WA_LOG("MC fail: ecdsaP256Keygen");
    return statusOnly(out, CTAP2_ERR_PROCESSING);
  }

  // rpIdHash = SHA-256(rpId)
  uint8_t rpIdHash[32];
  WebAuthnCrypto::sha256((const uint8_t*)rpId, rpIdLen, rpIdHash);

  // Wrap private key into a credentialId (96 bytes).
  uint8_t credId[CredentialStore::kCredIdSize];
  if (!CredentialStore::encodeCredentialId(priv, rpIdHash, credId)) {
    WA_LOG("MC fail: encodeCredentialId");
    return statusOnly(out, CTAP2_ERR_PROCESSING);
  }

  // ── Build authData ─────────────────────────────────────────────────
  // Big working buffers held as function-scope statics: dispatch is
  // single-threaded (driven by the WebAuthn screen's onUpdate), and these
  // would otherwise eat ~900 B of stack on top of mbedTLS ECDSA's internal
  // big-integer scratch — observed to crash the loop task on cardputer_adv.
  static uint8_t cose[128];
  size_t  coseLen = writeCoseKey(pub, cose, sizeof(cose));
  if (!coseLen) {
    WA_LOG("MC fail: writeCoseKey");
    return statusOnly(out, CTAP2_ERR_PROCESSING);
  }

  static uint8_t attCred[16 + 2 + CredentialStore::kCredIdSize + 128];
  size_t  attCredLen = writeAttCredData(attCred, sizeof(attCred),
                                        credId, sizeof(credId),
                                        cose, coseLen);
  if (!attCredLen) {
    WA_LOG("MC fail: writeAttCredData (coseLen=%u)", (unsigned)coseLen);
    return statusOnly(out, CTAP2_ERR_PROCESSING);
  }

  uint32_t counter = CredentialStore::bumpCounter();

  // Build the extensions CBOR map if any extensions were requested. For
  // hmac-secret on MakeCredential the response is just the boolean flag.
  static uint8_t extBuf[64];
  size_t  extLen = 0;
  uint8_t mcFlags = FLAG_UP | FLAG_AT;
  if (reqHmacSecret) {
    CborWriter we(extBuf, sizeof(extBuf));
    we.beginMap(1);
      we.putText("hmac-secret"); we.putBool(true);
    if (!we.ok()) {
      WA_LOG("MC fail: extensions CBOR overflow");
      return statusOnly(out, CTAP2_ERR_PROCESSING);
    }
    extLen = we.size();
    mcFlags |= FLAG_ED;
  }

  static uint8_t authData[512];
  size_t   authLen = writeAuthData(authData, sizeof(authData), rpIdHash,
                                   mcFlags, counter,
                                   attCred, attCredLen,
                                   extLen ? extBuf : nullptr, extLen);
  if (!authLen) {
    WA_LOG("MC fail: writeAuthData (attCredLen=%u extLen=%u counter=%lu)",
           (unsigned)attCredLen, (unsigned)extLen, (unsigned long)counter);
    return statusOnly(out, CTAP2_ERR_PROCESSING);
  }

  // ── Self-attestation signature ─────────────────────────────────────
  // sig = ECDSA(privKey, SHA-256(authData || clientDataHash))
  static uint8_t sigInput[512 + 32];
  size_t  sigInputLen = authLen + 32;
  if (sigInputLen > sizeof(sigInput)) {
    WA_LOG("MC fail: sigInput overflow (%u > %u)",
           (unsigned)sigInputLen, (unsigned)sizeof(sigInput));
    return statusOnly(out, CTAP2_ERR_PROCESSING);
  }
  memcpy(sigInput, authData, authLen);
  memcpy(sigInput + authLen, clientDataHash, 32);

  uint8_t sigHash[32];
  WebAuthnCrypto::sha256(sigInput, sigInputLen, sigHash);
  uint8_t sigDer[72]; size_t sigLen = 0;
  if (!WebAuthnCrypto::ecdsaP256SignDer(priv, sigHash, sigDer, &sigLen)) {
    WA_LOG("MC fail: ecdsaP256SignDer");
    return statusOnly(out, CTAP2_ERR_PROCESSING);
  }
  WA_LOG("MC: signed, encoding response (authLen=%u sigLen=%u)",
         (unsigned)authLen, (unsigned)sigLen);

  // ── Encode response ────────────────────────────────────────────────
  out[0] = CTAP2_OK;
  CborWriter w(out + 1, outMax - 1);
  w.beginMap(3);
    w.putUint(0x01);    w.putText("packed");
    w.putUint(0x02);    w.putBytes(authData, authLen);
    w.putUint(0x03);
      w.beginMap(2);
        w.putText("alg"); w.putInt(COSE_ES256);
        w.putText("sig"); w.putBytes(sigDer, sigLen);

  if (!w.ok()) {
    WA_LOG("MC fail: response CBOR encoder overflow (outMax=%u)", (unsigned)outMax);
    return statusOnly(out, CTAP2_ERR_PROCESSING);
  }
  WA_LOG("MC ok: respLen=%u", (unsigned)(1 + w.size()));
  return (uint16_t)(1 + w.size());
}

// ── GetAssertion ───────────────────────────────────────────────────────

uint16_t Ctap2::_handleGetAssertion(const uint8_t* req, uint16_t reqLen,
                                    uint8_t* out, uint16_t outMax)
{
  CborReader r(req, reqLen);
  size_t mapCount = 0;
  if (!r.readMapHeader(&mapCount)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);

  char    rpId[128]; size_t rpIdLen = 0;
  uint8_t clientDataHash[32]; bool gotCdh = false;

  // We try each entry in allowList in order; first one that decodes wins.
  uint8_t winnerCredId[CredentialStore::kCredIdSize];
  size_t  winnerCredIdLen = 0;
  uint8_t winnerPriv[32];
  bool    found = false;

  // hmac-secret extension input (parsed from request key 0x04). All four
  // fields are populated together when the host requests hmac-secret.
  bool     reqHmacSecret = false;
  uint8_t  hmacPeerPub[65];                  // host's ECDH P-256 pubkey
  uint8_t  hmacSaltEnc[64 + 16] = {0};       // up to 2 × 32 B salts AES-CBC encrypted
  size_t   hmacSaltEncLen = 0;
  uint8_t  hmacSaltAuth[32] = {0};
  size_t   hmacSaltAuthLen = 0;
  uint64_t hmacProto = 1;

  for (size_t i = 0; i < mapCount; i++) {
    uint64_t k;
    if (!r.readUint(&k)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
    switch (k) {
      case 0x01:  // rpId
        if (!readTextInto(r, rpId, sizeof(rpId), &rpIdLen))
          return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
        break;
      case 0x02: {  // clientDataHash
        const uint8_t* p; size_t n;
        if (!r.readBytes(&p, &n) || n != 32) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
        memcpy(clientDataHash, p, 32);
        gotCdh = true;
        break;
      }
      case 0x03: {  // allowList
        size_t arr; if (!r.readArrayHeader(&arr)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
        // We need rpId before we can decode credential IDs. If rpId hasn't
        // been seen yet (allowList came before key 0x01 — possible but
        // canonical CBOR puts integer keys in ascending order so 0x01
        // already came first). Defensive fallback: re-scan the map.
        uint8_t rpIdHashCalc[32];
        if (rpIdLen == 0) return statusOnly(out, CTAP2_ERR_MISSING_PARAMETER);
        WebAuthnCrypto::sha256((const uint8_t*)rpId, rpIdLen, rpIdHashCalc);

        for (size_t j = 0; j < arr; j++) {
          size_t cMap;
          if (!r.readMapHeader(&cMap)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
          const uint8_t* candId = nullptr;
          size_t         candLen = 0;
          for (size_t kk = 0; kk < cMap; kk++) {
            const char* key; size_t klen;
            if (!r.readText(&key, &klen)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
            if (klen == 2 && memcmp(key, "id", 2) == 0) {
              if (!r.readBytes(&candId, &candLen))
                return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
            } else {
              r.skip();
            }
          }
          if (!found && candId && candLen == CredentialStore::kCredIdSize) {
            uint8_t priv[32];
            if (CredentialStore::decodeCredentialId(candId, candLen,
                                                    rpIdHashCalc, priv)) {
              memcpy(winnerCredId, candId, candLen);
              winnerCredIdLen = candLen;
              memcpy(winnerPriv, priv, 32);
              found = true;
            }
          }
        }
        break;
      }
      case 0x04: {  // extensions — text-keyed map
        size_t extMap;
        if (!r.readMapHeader(&extMap)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
        for (size_t j = 0; j < extMap; j++) {
          const char* key; size_t klen;
          if (!r.readText(&key, &klen)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
          if (klen == 11 && memcmp(key, "hmac-secret", 11) == 0) {
            // Inner map: 1=keyAgreement, 2=saltEnc, 3=saltAuth, 4=protocol
            size_t hsMap;
            if (!r.readMapHeader(&hsMap)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
            for (size_t kk = 0; kk < hsMap; kk++) {
              uint64_t hk;
              if (!r.readUint(&hk)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
              if (hk == 0x01) {
                if (!readCoseEcdhKey(r, hmacPeerPub))
                  return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
              } else if (hk == 0x02) {
                const uint8_t* p; size_t n;
                if (!r.readBytes(&p, &n) || n > sizeof(hmacSaltEnc))
                  return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
                memcpy(hmacSaltEnc, p, n);
                hmacSaltEncLen = n;
              } else if (hk == 0x03) {
                const uint8_t* p; size_t n;
                if (!r.readBytes(&p, &n) || n > sizeof(hmacSaltAuth))
                  return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
                memcpy(hmacSaltAuth, p, n);
                hmacSaltAuthLen = n;
              } else if (hk == 0x04) {
                if (!r.readUint(&hmacProto)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
              } else {
                r.skip();
              }
            }
            reqHmacSecret = true;
          } else {
            r.skip();
          }
        }
        break;
      }
      default:
        r.skip();
        break;
    }
    if (!r.ok()) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
  }

  if (!gotCdh || rpIdLen == 0) {
    WA_LOG("GA fail: missing cdh/rpId (gotCdh=%d rpIdLen=%u)",
           (int)gotCdh, (unsigned)rpIdLen);
    return statusOnly(out, CTAP2_ERR_MISSING_PARAMETER);
  }
  if (!found) {
    WA_LOG("GA fail: no matching cred in allowList (rpId=%s)", rpId);
    return statusOnly(out, CTAP2_ERR_NO_CREDENTIALS);
  }
  WA_LOG("GA: cred matched, requesting UP for rp=%s", rpId);

  // ── User presence ───────────────────────────────────────────────────
  if (!requestUserPresence(rpId)) {
    WA_LOG("GA fail: UP denied/timeout");
    return statusOnly(out, CTAP2_ERR_OPERATION_DENIED);
  }
  WA_LOG("GA: UP granted, signing");

  // ── hmac-secret extension processing ─────────────────────────────────
  // Per CTAP2 §12.5: only single-salt (32 B) or double-salt (64 B) is
  // valid, with the encrypted form being one AES-CBC block longer.
  static uint8_t hmacResp[64];
  size_t   hmacRespLen = 0;
  if (reqHmacSecret) {
    if (hmacProto != 1) {
      WA_LOG("GA fail: hmac-secret unsupported proto=%llu", (unsigned long long)hmacProto);
      return statusOnly(out, CTAP2_ERR_INVALID_OPTION);
    }
    if (hmacSaltEncLen != 32 && hmacSaltEncLen != 64) {
      WA_LOG("GA fail: hmac-secret saltEnc len=%u (want 32 or 64)",
             (unsigned)hmacSaltEncLen);
      return statusOnly(out, CTAP2_ERR_INVALID_OPTION);
    }

    // Proto v1 sharedSecret = SHA-256(ECDH(devPriv, hostPub).X)
    uint8_t sharedX[32], shared[32];
    if (!WebAuthnCrypto::ecdhComputeSharedX(hmacPeerPub, sharedX)) {
      WA_LOG("GA fail: hmac-secret ECDH compute");
      return statusOnly(out, CTAP2_ERR_PROCESSING);
    }
    WebAuthnCrypto::sha256(sharedX, 32, shared);

    // Verify saltAuth = HMAC(shared, saltEnc) truncated to 16 B (proto v1)
    uint8_t calcAuth[32];
    WebAuthnCrypto::hmacSha256(shared, 32, hmacSaltEnc, hmacSaltEncLen, calcAuth);
    if (hmacSaltAuthLen != 16 || memcmp(calcAuth, hmacSaltAuth, 16) != 0) {
      WA_LOG("GA fail: hmac-secret saltAuth mismatch");
      return statusOnly(out, CTAP2_ERR_INVALID_OPTION);
    }

    // Decrypt saltEnc with zero IV → salt_dec
    uint8_t salt_dec[64] = {0};
    static const uint8_t zeroIv[16] = {0};
    if (!WebAuthnCrypto::aes256CbcDecrypt(shared, zeroIv,
                                          hmacSaltEnc, hmacSaltEncLen, salt_dec)) {
      WA_LOG("GA fail: hmac-secret saltEnc decrypt");
      return statusOnly(out, CTAP2_ERR_PROCESSING);
    }

    // Per-cred secret (64 B; we use the no-UV half since no PIN/UV state)
    uint8_t cred_random[64];
    if (!WebAuthnCrypto::deriveHmacSecret(winnerCredId, winnerCredIdLen, cred_random)) {
      WA_LOG("GA fail: hmac-secret deriveHmacSecret");
      return statusOnly(out, CTAP2_ERR_PROCESSING);
    }
    const uint8_t* variant = cred_random;  // FLAG_UV not set → no-UV half

    // out_plain[i] = HMAC(variant, salt_dec[i*32..(i+1)*32])
    uint8_t out_plain[64];
    WebAuthnCrypto::hmacSha256(variant, 32, salt_dec, 32, out_plain);
    if (hmacSaltEncLen == 64) {
      WebAuthnCrypto::hmacSha256(variant, 32, salt_dec + 32, 32, out_plain + 32);
    }

    // Encrypt back with the shared key, zero IV
    if (!WebAuthnCrypto::aes256CbcEncrypt(shared, zeroIv, out_plain,
                                          hmacSaltEncLen, hmacResp)) {
      WA_LOG("GA fail: hmac-secret response encrypt");
      return statusOnly(out, CTAP2_ERR_PROCESSING);
    }
    hmacRespLen = hmacSaltEncLen;
    memset(cred_random, 0, sizeof(cred_random));
  }

  // ── Build authData (no attestedCredentialData this time) ────────────
  uint8_t rpIdHash[32];
  WebAuthnCrypto::sha256((const uint8_t*)rpId, rpIdLen, rpIdHash);
  uint32_t counter = CredentialStore::bumpCounter();

  // Build the response extensions CBOR if hmac-secret was processed.
  static uint8_t gaExt[128];
  size_t  gaExtLen = 0;
  uint8_t gaFlags = FLAG_UP;
  if (hmacRespLen) {
    CborWriter we(gaExt, sizeof(gaExt));
    we.beginMap(1);
      we.putText("hmac-secret"); we.putBytes(hmacResp, hmacRespLen);
    if (!we.ok()) {
      WA_LOG("GA fail: extensions CBOR overflow");
      return statusOnly(out, CTAP2_ERR_PROCESSING);
    }
    gaExtLen = we.size();
    gaFlags |= FLAG_ED;
  }

  static uint8_t authData[256];
  size_t   authLen = writeAuthData(authData, sizeof(authData), rpIdHash,
                                   gaFlags, counter, nullptr, 0,
                                   gaExtLen ? gaExt : nullptr, gaExtLen);
  if (!authLen) {
    WA_LOG("GA fail: writeAuthData (counter=%lu extLen=%u)",
           (unsigned long)counter, (unsigned)gaExtLen);
    return statusOnly(out, CTAP2_ERR_PROCESSING);
  }

  // sig = ECDSA(privKey, SHA-256(authData || clientDataHash))
  static uint8_t sigInput[256 + 32];
  size_t  sigInputLen = authLen + 32;
  if (sigInputLen > sizeof(sigInput)) {
    WA_LOG("GA fail: sigInput overflow (%u > %u)",
           (unsigned)sigInputLen, (unsigned)sizeof(sigInput));
    return statusOnly(out, CTAP2_ERR_PROCESSING);
  }
  memcpy(sigInput, authData, authLen);
  memcpy(sigInput + authLen, clientDataHash, 32);
  uint8_t sigHash[32];
  WebAuthnCrypto::sha256(sigInput, sigInputLen, sigHash);
  uint8_t sigDer[72]; size_t sigLen = 0;
  if (!WebAuthnCrypto::ecdsaP256SignDer(winnerPriv, sigHash, sigDer, &sigLen)) {
    WA_LOG("GA fail: ecdsaP256SignDer");
    return statusOnly(out, CTAP2_ERR_PROCESSING);
  }
  WA_LOG("GA: signed (authLen=%u sigLen=%u credIdLen=%u)",
         (unsigned)authLen, (unsigned)sigLen, (unsigned)winnerCredIdLen);

  // ── Encode response ────────────────────────────────────────────────
  out[0] = CTAP2_OK;
  CborWriter w(out + 1, outMax - 1);
  // Canonical CBOR: shorter text key first → "id" (2) before "type" (4).
  // Some hosts (Chrome's webauthn stack) reject GetAssertion responses
  // when keys aren't in canonical order, even if they parse otherwise.
  w.beginMap(3);
    w.putUint(0x01);
      w.beginMap(2);
        w.putText("id");   w.putBytes(winnerCredId, winnerCredIdLen);
        w.putText("type"); w.putText("public-key");
    w.putUint(0x02);  w.putBytes(authData, authLen);
    w.putUint(0x03);  w.putBytes(sigDer, sigLen);

  if (!w.ok()) {
    WA_LOG("GA fail: response CBOR encoder overflow (outMax=%u)", (unsigned)outMax);
    return statusOnly(out, CTAP2_ERR_PROCESSING);
  }
  WA_LOG("GA ok: respLen=%u", (unsigned)(1 + w.size()));
  return (uint16_t)(1 + w.size());
}

// ── ClientPIN ─────────────────────────────────────────────────────────
// Partial implementation: only subcommands needed by hmac-secret negotiation
// are supported (0x01 getPINRetries returning a stub 0, and 0x02
// getKeyAgreement returning our ephemeral COSE_Key). The rest return
// CTAP2_ERR_INVALID_OPTION until full Phase 5c lands.

uint16_t Ctap2::_handleClientPin(const uint8_t* req, uint16_t reqLen,
                                 uint8_t* out, uint16_t outMax)
{
  CborReader r(req, reqLen);
  size_t mapCount = 0;
  if (!r.readMapHeader(&mapCount)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);

  uint64_t protocol = 0;
  uint64_t subcmd   = 0;

  for (size_t i = 0; i < mapCount; i++) {
    uint64_t k;
    if (!r.readUint(&k)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
    switch (k) {
      case 0x01:  // pinUvAuthProtocol
        if (!r.readUint(&protocol)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
        break;
      case 0x02:  // subCommand
        if (!r.readUint(&subcmd)) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
        break;
      default:
        r.skip();
        break;
    }
    if (!r.ok()) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
  }

  // Only proto v1 is advertised in GetInfo; reject anything else.
  if (protocol != 0 && protocol != 1) {
    WA_LOG("CP fail: unsupported pinUvAuthProtocol=%llu", (unsigned long long)protocol);
    return statusOnly(out, CTAP2_ERR_INVALID_OPTION);
  }

  if (subcmd == 0x01) {
    // getPINRetries — no PIN set yet, so always returns max retries.
    WA_LOG("CP getPINRetries");
    out[0] = CTAP2_OK;
    CborWriter w(out + 1, outMax - 1);
    w.beginMap(1);
      w.putUint(0x03);  w.putUint(kPinMaxRetries);
    if (!w.ok()) return statusOnly(out, CTAP2_ERR_PROCESSING);
    return (uint16_t)(1 + w.size());
  }

  if (subcmd == 0x02) {
    // getKeyAgreement — return the device's ephemeral COSE_Key.
    uint8_t pub[65];
    if (!WebAuthnCrypto::getEphemeralPublicKey(pub)) {
      WA_LOG("CP fail: getEphemeralPublicKey (not initialized?)");
      return statusOnly(out, CTAP2_ERR_PROCESSING);
    }
    out[0] = CTAP2_OK;
    CborWriter w(out + 1, outMax - 1);
    w.beginMap(1);
      w.putUint(0x01);
      // Inline the COSE_Key map directly (writeCoseEcdhKey writes into a
      // separate buffer; here we want it nested in this writer).
      w.beginMap(5);
        w.putUint(1);         w.putUint(2);                       // kty: EC2
        w.putUint(3);         w.putInt(COSE_ECDH_HKDF_256);       // alg: -25
        w.putInt(-1);         w.putUint(1);                       // crv: P-256
        w.putInt(-2);         w.putBytes(pub + 1, 32);            // x
        w.putInt(-3);         w.putBytes(pub + 33, 32);           // y
    if (!w.ok()) {
      WA_LOG("CP fail: getKeyAgreement CBOR overflow");
      return statusOnly(out, CTAP2_ERR_PROCESSING);
    }
    WA_LOG("CP getKeyAgreement ok respLen=%u", (unsigned)(1 + w.size()));
    return (uint16_t)(1 + w.size());
  }

  // Setting / changing the PIN, getting a PIN token, getUVRetries — none
  // supported until full ClientPIN lands.
  WA_LOG("CP fail: unsupported subcommand=%llu", (unsigned long long)subcmd);
  return statusOnly(out, CTAP2_ERR_INVALID_OPTION);
}

// ── Reset ──────────────────────────────────────────────────────────────

uint16_t Ctap2::_handleReset(uint8_t* out, uint16_t)
{
  // Spec: must be confirmed by user gesture and within 10 s of power-on or
  // last gesture. Phase 8 will install a confirm screen here. For now: if
  // a user-presence callback is installed, ask it; otherwise fail closed.
  if (g_upFn) {
    if (!g_upFn("(reset)", g_upUser))
      return statusOnly(out, CTAP2_ERR_OPERATION_DENIED);
  } else {
    return statusOnly(out, CTAP2_ERR_OPERATION_DENIED);
  }
  if (!CredentialStore::wipe())
    return statusOnly(out, CTAP2_ERR_PROCESSING);
  return statusOnly(out, CTAP2_OK);
}

// ── Dispatch ──────────────────────────────────────────────────────────

uint16_t Ctap2::dispatch(uint8_t cmd,
                         const uint8_t* req, uint16_t reqLen,
                         uint8_t* resp, uint16_t respMax,
                         uint8_t* /*respCmd*/, void*)
{
  // CTAPHID_MSG carries a U2F APDU — different protocol and response
  // format. The response is raw APDU data with a status word, so we
  // signal that to the caller by leaving the response untouched as bytes.
  if (cmd == CTAPHID_MSG) {
    WA_LOG("CTAP2 dispatch MSG (U2F) reqLen=%u", reqLen);
    waLogHex("U2F req", req, reqLen, 16);
    uint16_t r = U2f::handleApdu(req, reqLen, resp, respMax);
    waLogHex("U2F resp", resp, r, 16);
    return r;
  }
  if (cmd != CTAPHID_CBOR) {
    WA_LOG("CTAP2 dispatch unknown ctaphid cmd=0x%02x", cmd);
    return statusOnly(resp, CTAP2_ERR_INVALID_OPTION);
  }
  if (reqLen < 1) return statusOnly(resp, CTAP2_ERR_INVALID_CBOR);

  uint8_t  ctapCmd = req[0];
  const uint8_t* p = req + 1;
  uint16_t pLen    = (uint16_t)(reqLen - 1);
  WA_LOG("CTAP2 cmd=0x%02x payloadLen=%u", ctapCmd, pLen);

  uint16_t r;
  switch (ctapCmd) {
    case CTAP2_GET_INFO:           r = _handleGetInfo(p, pLen, resp, respMax);          break;
    case CTAP2_MAKE_CREDENTIAL:    r = _handleMakeCredential(p, pLen, resp, respMax);   break;
    case CTAP2_GET_ASSERTION:      r = _handleGetAssertion(p, pLen, resp, respMax);     break;
    case CTAP2_RESET:              r = _handleReset(resp, respMax);                     break;
    case CTAP2_CLIENT_PIN:         r = _handleClientPin(p, pLen, resp, respMax);        break;
    case CTAP2_GET_NEXT_ASSERTION: r = statusOnly(resp, CTAP2_ERR_NO_OPERATION_PENDING); break;
    default:                       r = statusOnly(resp, CTAP2_ERR_INVALID_OPTION);      break;
  }
  WA_LOG("CTAP2 cmd=0x%02x status=0x%02x respLen=%u", ctapCmd, resp[0], r);
  if (r > 1) waLogHex("CTAP2 resp body", resp + 1, r - 1, 32);
  return r;
}

}  // namespace webauthn

#endif  // DEVICE_HAS_WEBAUTHN
