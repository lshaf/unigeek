#include "WebAuthnCrypto.h"

#include <Arduino.h>

#ifdef DEVICE_HAS_WEBAUTHN

#include <string.h>
#include <esp_system.h>

#include <mbedtls/sha256.h>
#include <mbedtls/md.h>
#include <mbedtls/aes.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/bignum.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/hkdf.h>

namespace webauthn {

namespace {
mbedtls_entropy_context  g_entropy;
mbedtls_ctr_drbg_context g_drbg;
bool                     g_inited = false;

// Entropy callback that pulls from esp_random() — much faster than the
// default mbedTLS entropy collector and already cryptographically strong on
// ESP32 hardware (SAR-ADC + RC noise + WiFi/BT activity).
int esp_entropy_callback(void* /*ctx*/, unsigned char* out, size_t len, size_t* olen)
{
  size_t want = len;
  while (want) {
    uint32_t r = esp_random();
    size_t   n = want < 4 ? want : 4;
    memcpy(out, &r, n);
    out += n; want -= n;
  }
  *olen = len;
  return 0;
}
}  // namespace

bool WebAuthnCrypto::init()
{
  if (g_inited) return true;
  mbedtls_entropy_init(&g_entropy);
  mbedtls_ctr_drbg_init(&g_drbg);
  // Replace the default entropy source with esp_random — strong + fast.
  mbedtls_entropy_add_source(&g_entropy, esp_entropy_callback,
                             nullptr, 32, MBEDTLS_ENTROPY_SOURCE_STRONG);
  static const char* kPers = "unigeek-webauthn";
  if (mbedtls_ctr_drbg_seed(&g_drbg, mbedtls_entropy_func, &g_entropy,
                            (const unsigned char*)kPers, strlen(kPers)) != 0) {
    return false;
  }
  g_inited = true;
  return true;
}

void WebAuthnCrypto::random(uint8_t* out, size_t len)
{
  if (!g_inited) { memset(out, 0, len); return; }
  mbedtls_ctr_drbg_random(&g_drbg, out, len);
}

void WebAuthnCrypto::sha256(const uint8_t* in, size_t len, uint8_t out[32])
{
  mbedtls_sha256(in, len, out, 0);
}

void WebAuthnCrypto::hmacSha256(const uint8_t* key, size_t keyLen,
                                const uint8_t* msg, size_t msgLen,
                                uint8_t out[32])
{
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                  key, keyLen, msg, msgLen, out);
}

bool WebAuthnCrypto::hkdfSha256(const uint8_t* ikm,  size_t ikmLen,
                                const uint8_t* salt, size_t saltLen,
                                const uint8_t* info, size_t infoLen,
                                uint8_t* okm, size_t okmLen)
{
  return mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                      salt, saltLen, ikm, ikmLen, info, infoLen,
                      okm, okmLen) == 0;
}

bool WebAuthnCrypto::aes256CbcEncrypt(const uint8_t key[32], const uint8_t iv[16],
                                      const uint8_t* in, size_t len, uint8_t* out)
{
  if (len % 16) return false;
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  bool ok = mbedtls_aes_setkey_enc(&aes, key, 256) == 0;
  uint8_t ivCopy[16];
  memcpy(ivCopy, iv, 16);  // mbedTLS mutates the IV during CBC
  if (ok) {
    ok = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, len,
                               ivCopy, in, out) == 0;
  }
  mbedtls_aes_free(&aes);
  return ok;
}

bool WebAuthnCrypto::aes256CbcDecrypt(const uint8_t key[32], const uint8_t iv[16],
                                      const uint8_t* in, size_t len, uint8_t* out)
{
  if (len % 16) return false;
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  bool ok = mbedtls_aes_setkey_dec(&aes, key, 256) == 0;
  uint8_t ivCopy[16];
  memcpy(ivCopy, iv, 16);
  if (ok) {
    ok = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, len,
                               ivCopy, in, out) == 0;
  }
  mbedtls_aes_free(&aes);
  return ok;
}

bool WebAuthnCrypto::ecdsaP256Keygen(uint8_t priv[32], uint8_t pub[65])
{
  mbedtls_ecdsa_context ctx;
  mbedtls_ecdsa_init(&ctx);
  bool ok = mbedtls_ecdsa_genkey(&ctx, MBEDTLS_ECP_DP_SECP256R1,
                                 mbedtls_ctr_drbg_random, &g_drbg) == 0;
  if (ok) {
    ok = mbedtls_mpi_write_binary(&ctx.d, priv, 32) == 0;
  }
  if (ok) {
    size_t plen = 0;
    ok = mbedtls_ecp_point_write_binary(&ctx.grp, &ctx.Q,
                                        MBEDTLS_ECP_PF_UNCOMPRESSED,
                                        &plen, pub, 65) == 0
         && plen == 65;
  }
  mbedtls_ecdsa_free(&ctx);
  return ok;
}

bool WebAuthnCrypto::ecdsaP256DerivePub(const uint8_t priv[32], uint8_t pub[65])
{
  mbedtls_ecp_group grp;
  mbedtls_ecp_point Q;
  mbedtls_mpi       d;
  mbedtls_ecp_group_init(&grp);
  mbedtls_ecp_point_init(&Q);
  mbedtls_mpi_init(&d);

  bool ok = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0
         && mbedtls_mpi_read_binary(&d, priv, 32) == 0
         && mbedtls_ecp_mul(&grp, &Q, &d, &grp.G,
                            mbedtls_ctr_drbg_random, &g_drbg) == 0;
  if (ok) {
    size_t plen = 0;
    ok = mbedtls_ecp_point_write_binary(&grp, &Q,
                                        MBEDTLS_ECP_PF_UNCOMPRESSED,
                                        &plen, pub, 65) == 0
         && plen == 65;
  }
  mbedtls_mpi_free(&d);
  mbedtls_ecp_point_free(&Q);
  mbedtls_ecp_group_free(&grp);
  return ok;
}

bool WebAuthnCrypto::ecdsaP256SignDer(const uint8_t priv[32], const uint8_t hash[32],
                                      uint8_t* outDer, size_t* outLen)
{
  mbedtls_ecdsa_context ctx;
  mbedtls_ecdsa_init(&ctx);
  bool ok = mbedtls_ecp_group_load(&ctx.grp, MBEDTLS_ECP_DP_SECP256R1) == 0
         && mbedtls_mpi_read_binary(&ctx.d, priv, 32) == 0;
  if (ok) {
    *outLen = 0;
    ok = mbedtls_ecdsa_write_signature(&ctx, MBEDTLS_MD_SHA256,
                                       hash, 32, outDer, outLen,
                                       mbedtls_ctr_drbg_random, &g_drbg) == 0;
  }
  mbedtls_ecdsa_free(&ctx);
  return ok;
}

}  // namespace webauthn

#endif  // DEVICE_HAS_WEBAUTHN
