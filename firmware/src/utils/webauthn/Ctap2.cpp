#include "Ctap2.h"
#include "Cbor.h"
#include "WebAuthnConfig.h"
#include "WebAuthnCrypto.h"
#include "CredentialStore.h"

#include <string.h>

namespace webauthn {

namespace {

Ctap2::UserPresenceFn g_upFn   = nullptr;
void*                 g_upUser = nullptr;

// authData flag bits
constexpr uint8_t FLAG_UP = 0x01;
constexpr uint8_t FLAG_UV = 0x04;
constexpr uint8_t FLAG_AT = 0x40;

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

// Compose authData into `out` and return its length.
//   authData = rpIdHash(32) | flags(1) | counter(4 BE) | [attCredData] | [ext]
size_t writeAuthData(uint8_t* out, size_t cap,
                     const uint8_t rpIdHash[32], uint8_t flags, uint32_t counter,
                     const uint8_t* attCred, size_t attCredLen)
{
  size_t need = 32 + 1 + 4 + attCredLen;
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

  w.beginMap(5);

  // 0x01 versions
  w.putUint(0x01);
  w.beginArray(2);
    w.putText("FIDO_2_0");
    w.putText("U2F_V2");

  // 0x03 aaguid
  w.putUint(0x03);
  w.putBytes(kAAGUID, sizeof(kAAGUID));

  // 0x04 options
  w.putUint(0x04);
  w.beginMap(3);
    w.putText("rk");        w.putBool(false);  // resident keys deferred (Phase 5b)
    w.putText("up");        w.putBool(true);
    w.putText("clientPin"); w.putBool(false);  // PIN deferred (Phase 5c)

  // 0x05 maxMsgSize
  w.putUint(0x05);
  w.putUint(kCtapMaxMsgSize);

  // 0x09 transports
  w.putUint(0x09);
  w.beginArray(1);
    w.putText("usb");

  if (!w.ok()) return statusOnly(out, CTAP2_ERR_OTHER);
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
      default:
        r.skip();  // ignore unknown / not-yet-supported parameters
        break;
    }
    if (!r.ok()) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
  }

  if (!gotCdh || rpIdLen == 0)
    return statusOnly(out, CTAP2_ERR_MISSING_PARAMETER);
  if (!hasEs256)
    return statusOnly(out, CTAP2_ERR_UNSUPPORTED_ALGORITHM);

  // ── User presence ───────────────────────────────────────────────────
  if (!requestUserPresence(rpId))
    return statusOnly(out, CTAP2_ERR_OPERATION_DENIED);

  // ── Generate credential keypair ────────────────────────────────────
  uint8_t priv[32], pub[65];
  if (!WebAuthnCrypto::ecdsaP256Keygen(priv, pub))
    return statusOnly(out, CTAP2_ERR_PROCESSING);

  // rpIdHash = SHA-256(rpId)
  uint8_t rpIdHash[32];
  WebAuthnCrypto::sha256((const uint8_t*)rpId, rpIdLen, rpIdHash);

  // Wrap private key into a credentialId (96 bytes).
  uint8_t credId[CredentialStore::kCredIdSize];
  if (!CredentialStore::encodeCredentialId(priv, rpIdHash, credId))
    return statusOnly(out, CTAP2_ERR_PROCESSING);

  // ── Build authData ─────────────────────────────────────────────────
  uint8_t cose[128];
  size_t  coseLen = writeCoseKey(pub, cose, sizeof(cose));
  if (!coseLen) return statusOnly(out, CTAP2_ERR_OTHER);

  uint8_t attCred[16 + 2 + sizeof(credId) + 128];
  size_t  attCredLen = writeAttCredData(attCred, sizeof(attCred),
                                        credId, sizeof(credId),
                                        cose, coseLen);
  if (!attCredLen) return statusOnly(out, CTAP2_ERR_OTHER);

  uint32_t counter = CredentialStore::bumpCounter();
  uint8_t  authData[256];
  size_t   authLen = writeAuthData(authData, sizeof(authData), rpIdHash,
                                   FLAG_UP | FLAG_AT, counter,
                                   attCred, attCredLen);
  if (!authLen) return statusOnly(out, CTAP2_ERR_OTHER);

  // ── Self-attestation signature ─────────────────────────────────────
  // sig = ECDSA(privKey, SHA-256(authData || clientDataHash))
  uint8_t sigInput[256 + 32];
  size_t  sigInputLen = authLen + 32;
  if (sigInputLen > sizeof(sigInput)) return statusOnly(out, CTAP2_ERR_OTHER);
  memcpy(sigInput, authData, authLen);
  memcpy(sigInput + authLen, clientDataHash, 32);

  uint8_t sigHash[32];
  WebAuthnCrypto::sha256(sigInput, sigInputLen, sigHash);
  uint8_t sigDer[72]; size_t sigLen = 0;
  if (!WebAuthnCrypto::ecdsaP256SignDer(priv, sigHash, sigDer, &sigLen))
    return statusOnly(out, CTAP2_ERR_PROCESSING);

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

  if (!w.ok()) return statusOnly(out, CTAP2_ERR_OTHER);
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
      default:
        r.skip();
        break;
    }
    if (!r.ok()) return statusOnly(out, CTAP2_ERR_INVALID_CBOR);
  }

  if (!gotCdh || rpIdLen == 0)
    return statusOnly(out, CTAP2_ERR_MISSING_PARAMETER);
  if (!found)
    return statusOnly(out, CTAP2_ERR_NO_CREDENTIALS);

  // ── User presence ───────────────────────────────────────────────────
  if (!requestUserPresence(rpId))
    return statusOnly(out, CTAP2_ERR_OPERATION_DENIED);

  // ── Build authData (no attestedCredentialData this time) ────────────
  uint8_t rpIdHash[32];
  WebAuthnCrypto::sha256((const uint8_t*)rpId, rpIdLen, rpIdHash);
  uint32_t counter = CredentialStore::bumpCounter();
  uint8_t  authData[64];
  size_t   authLen = writeAuthData(authData, sizeof(authData), rpIdHash,
                                   FLAG_UP, counter, nullptr, 0);
  if (!authLen) return statusOnly(out, CTAP2_ERR_OTHER);

  // sig = ECDSA(privKey, SHA-256(authData || clientDataHash))
  uint8_t sigInput[64 + 32];
  size_t  sigInputLen = authLen + 32;
  memcpy(sigInput, authData, authLen);
  memcpy(sigInput + authLen, clientDataHash, 32);
  uint8_t sigHash[32];
  WebAuthnCrypto::sha256(sigInput, sigInputLen, sigHash);
  uint8_t sigDer[72]; size_t sigLen = 0;
  if (!WebAuthnCrypto::ecdsaP256SignDer(winnerPriv, sigHash, sigDer, &sigLen))
    return statusOnly(out, CTAP2_ERR_PROCESSING);

  // ── Encode response ────────────────────────────────────────────────
  out[0] = CTAP2_OK;
  CborWriter w(out + 1, outMax - 1);
  w.beginMap(3);
    w.putUint(0x01);
      w.beginMap(2);
        w.putText("type"); w.putText("public-key");
        w.putText("id");   w.putBytes(winnerCredId, winnerCredIdLen);
    w.putUint(0x02);  w.putBytes(authData, authLen);
    w.putUint(0x03);  w.putBytes(sigDer, sigLen);

  if (!w.ok()) return statusOnly(out, CTAP2_ERR_OTHER);
  return (uint16_t)(1 + w.size());
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
  // CTAPHID_CBOR payload starts with the CTAP2 command byte.
  if (cmd != CTAPHID_CBOR) {
    // CTAPHID_MSG (U2F) lands here too; Phase 7 will route it.
    return statusOnly(resp, CTAP2_ERR_INVALID_OPTION);
  }
  if (reqLen < 1) return statusOnly(resp, CTAP2_ERR_INVALID_CBOR);

  uint8_t  ctapCmd = req[0];
  const uint8_t* p = req + 1;
  uint16_t pLen    = (uint16_t)(reqLen - 1);

  switch (ctapCmd) {
    case CTAP2_GET_INFO:           return _handleGetInfo(p, pLen, resp, respMax);
    case CTAP2_MAKE_CREDENTIAL:    return _handleMakeCredential(p, pLen, resp, respMax);
    case CTAP2_GET_ASSERTION:      return _handleGetAssertion(p, pLen, resp, respMax);
    case CTAP2_RESET:              return _handleReset(resp, respMax);
    case CTAP2_CLIENT_PIN:         return statusOnly(resp, CTAP2_ERR_UNSUPPORTED_OPTION);
    case CTAP2_GET_NEXT_ASSERTION: return statusOnly(resp, CTAP2_ERR_NO_OPERATION_PENDING);
    default:                       return statusOnly(resp, CTAP2_ERR_INVALID_OPTION);
  }
}

}  // namespace webauthn
