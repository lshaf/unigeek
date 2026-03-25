#include "utils/network/FastWpaCrack.h"
#include <cstring>

// ==========================================================================
//  SOFTWARE SHA1 — O3/hot, no FreeRTOS mutex
//  Hardware SHA peripheral has a mutex that serializes dual-core.
//  This custom impl runs ~1.8x faster on dual-core ESP32-S3.
// ==========================================================================

#define SHA1_ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define SHA1_BLK(i) \
  (blk[i & 15] = SHA1_ROL(blk[(i+13)&15] ^ blk[(i+8)&15] ^ blk[(i+2)&15] ^ blk[i&15], 1))

#define R0(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk[i]+0x5A827999u+SHA1_ROL(v,5);w=SHA1_ROL(w,30)
#define R1(v,w,x,y,z,i) z+=((w&(x^y))^y)+SHA1_BLK(i)+0x5A827999u+SHA1_ROL(v,5);w=SHA1_ROL(w,30)
#define R2(v,w,x,y,z,i) z+=(w^x^y)+SHA1_BLK(i)+0x6ED9EBA1u+SHA1_ROL(v,5);w=SHA1_ROL(w,30)
#define R3(v,w,x,y,z,i) z+=(((w|x)&y)|(w&x))+SHA1_BLK(i)+0x8F1BBCDCu+SHA1_ROL(v,5);w=SHA1_ROL(w,30)
#define R4(v,w,x,y,z,i) z+=(w^x^y)+SHA1_BLK(i)+0xCA62C1D6u+SHA1_ROL(v,5);w=SHA1_ROL(w,30)

#define SHA1_80_ROUNDS() \
  R0(a,b,c,d,e, 0);R0(e,a,b,c,d, 1);R0(d,e,a,b,c, 2);R0(c,d,e,a,b, 3); \
  R0(b,c,d,e,a, 4);R0(a,b,c,d,e, 5);R0(e,a,b,c,d, 6);R0(d,e,a,b,c, 7); \
  R0(c,d,e,a,b, 8);R0(b,c,d,e,a, 9);R0(a,b,c,d,e,10);R0(e,a,b,c,d,11); \
  R0(d,e,a,b,c,12);R0(c,d,e,a,b,13);R0(b,c,d,e,a,14);R0(a,b,c,d,e,15); \
  R1(e,a,b,c,d,16);R1(d,e,a,b,c,17);R1(c,d,e,a,b,18);R1(b,c,d,e,a,19); \
  R2(a,b,c,d,e,20);R2(e,a,b,c,d,21);R2(d,e,a,b,c,22);R2(c,d,e,a,b,23); \
  R2(b,c,d,e,a,24);R2(a,b,c,d,e,25);R2(e,a,b,c,d,26);R2(d,e,a,b,c,27); \
  R2(c,d,e,a,b,28);R2(b,c,d,e,a,29);R2(a,b,c,d,e,30);R2(e,a,b,c,d,31); \
  R2(d,e,a,b,c,32);R2(c,d,e,a,b,33);R2(b,c,d,e,a,34);R2(a,b,c,d,e,35); \
  R2(e,a,b,c,d,36);R2(d,e,a,b,c,37);R2(c,d,e,a,b,38);R2(b,c,d,e,a,39); \
  R3(a,b,c,d,e,40);R3(e,a,b,c,d,41);R3(d,e,a,b,c,42);R3(c,d,e,a,b,43); \
  R3(b,c,d,e,a,44);R3(a,b,c,d,e,45);R3(e,a,b,c,d,46);R3(d,e,a,b,c,47); \
  R3(c,d,e,a,b,48);R3(b,c,d,e,a,49);R3(a,b,c,d,e,50);R3(e,a,b,c,d,51); \
  R3(d,e,a,b,c,52);R3(c,d,e,a,b,53);R3(b,c,d,e,a,54);R3(a,b,c,d,e,55); \
  R3(e,a,b,c,d,56);R3(d,e,a,b,c,57);R3(c,d,e,a,b,58);R3(b,c,d,e,a,59); \
  R4(a,b,c,d,e,60);R4(e,a,b,c,d,61);R4(d,e,a,b,c,62);R4(c,d,e,a,b,63); \
  R4(b,c,d,e,a,64);R4(a,b,c,d,e,65);R4(e,a,b,c,d,66);R4(d,e,a,b,c,67); \
  R4(c,d,e,a,b,68);R4(b,c,d,e,a,69);R4(a,b,c,d,e,70);R4(e,a,b,c,d,71); \
  R4(d,e,a,b,c,72);R4(c,d,e,a,b,73);R4(b,c,d,e,a,74);R4(a,b,c,d,e,75); \
  R4(e,a,b,c,d,76);R4(d,e,a,b,c,77);R4(c,d,e,a,b,78);R4(b,c,d,e,a,79)

__attribute__((optimize("O3"), hot))
static void sha1_transform(uint32_t state[5], const uint8_t buf[64]) {
  uint32_t a=state[0], b=state[1], c=state[2], d=state[3], e=state[4];
  uint32_t blk[16], tmp;
  for (int i=0; i<16; i++) { memcpy(&tmp, buf+i*4, 4); blk[i]=__builtin_bswap32(tmp); }
  SHA1_80_ROUNDS();
  state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d; state[4]+=e;
}

// Specialized: exactly 20 bytes with SHA1 padding pre-baked (hot PBKDF2 path)
__attribute__((optimize("O3"), hot))
static void sha1_transform_20b(uint32_t state[5], const uint8_t data[20]) {
  uint32_t a=state[0], b=state[1], c=state[2], d=state[3], e=state[4];
  uint32_t blk[16], tmp;
  memcpy(&tmp, data+ 0, 4); blk[0]=__builtin_bswap32(tmp);
  memcpy(&tmp, data+ 4, 4); blk[1]=__builtin_bswap32(tmp);
  memcpy(&tmp, data+ 8, 4); blk[2]=__builtin_bswap32(tmp);
  memcpy(&tmp, data+12, 4); blk[3]=__builtin_bswap32(tmp);
  memcpy(&tmp, data+16, 4); blk[4]=__builtin_bswap32(tmp);
  blk[5]=0x80000000u;
  blk[6]=blk[7]=blk[8]=blk[9]=blk[10]=blk[11]=blk[12]=blk[13]=0;
  blk[14]=0;
  blk[15]=0x000002A0u; // (64+20)*8 = 672
  SHA1_80_ROUNDS();
  state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d; state[4]+=e;
}

// Specialized: 20 bytes as uint32_t words (already big-endian)
__attribute__((optimize("O3"), hot))
static void sha1_transform_20w(uint32_t state[5], const uint32_t words[5]) {
  uint32_t a=state[0], b=state[1], c=state[2], d=state[3], e=state[4];
  uint32_t blk[16];
  blk[0]=words[0]; blk[1]=words[1]; blk[2]=words[2]; blk[3]=words[3]; blk[4]=words[4];
  blk[5]=0x80000000u;
  blk[6]=blk[7]=blk[8]=blk[9]=blk[10]=blk[11]=blk[12]=blk[13]=0;
  blk[14]=0;
  blk[15]=0x000002A0u;
  SHA1_80_ROUNDS();
  state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d; state[4]+=e;
}

static inline void sha1_extract(const uint32_t state[5], uint8_t digest[20]) {
  for (int i=0; i<5; i++) { uint32_t s=__builtin_bswap32(state[i]); memcpy(digest+i*4, &s, 4); }
}

static void sha1_init(FastSha1Ctx &ctx) {
  ctx.state[0]=0x67452301u; ctx.state[1]=0xEFCDAB89u;
  ctx.state[2]=0x98BADCFEu; ctx.state[3]=0x10325476u;
  ctx.state[4]=0xC3D2E1F0u;
  ctx.count[0]=ctx.count[1]=0;
}

static void sha1_update(FastSha1Ctx &ctx, const uint8_t *data, size_t len) {
  uint32_t j = (ctx.count[0] >> 3) & 63;
  if ((ctx.count[0] += (uint32_t)(len << 3)) < (uint32_t)(len << 3)) ctx.count[1]++;
  ctx.count[1] += (uint32_t)(len >> 29);
  if (j + len > 63) {
    size_t i = 64 - j;
    memcpy(ctx.buf + j, data, i);
    sha1_transform(ctx.state, ctx.buf);
    for (; i + 63 < len; i += 64) sha1_transform(ctx.state, data + i);
    j = 0;
    memcpy(ctx.buf, data + i, len - i);
  } else {
    memcpy(ctx.buf + j, data, len);
  }
}

static void sha1_final(FastSha1Ctx &ctx, uint8_t digest[20]) {
  uint64_t total_bits = ((uint64_t)ctx.count[1] << 32) | ctx.count[0];
  uint32_t j = (ctx.count[0] >> 3) & 63;
  ctx.buf[j++] = 0x80;
  if (j > 56) { memset(ctx.buf+j, 0, 64-j); sha1_transform(ctx.state, ctx.buf); j=0; }
  memset(ctx.buf+j, 0, 56-j);
  for (int i=0; i<8; i++) ctx.buf[56+i] = (uint8_t)(total_bits >> (56 - i*8));
  sha1_transform(ctx.state, ctx.buf);
  sha1_extract(ctx.state, digest);
}

// ── HMAC-SHA1 ─────────────────────────────────────────────────────────────

void fast_hmac_precompute(const uint8_t *key, size_t klen, FastHmacPre &out) {
  uint8_t k_ipad[64], k_opad[64];
  memset(k_ipad, 0x36, 64);
  memset(k_opad, 0x5C, 64);
  uint8_t hk[20];
  if (klen > 64) {
    FastSha1Ctx t; sha1_init(t);
    sha1_update(t, key, klen);
    sha1_final(t, hk);
    key = hk; klen = 20;
  }
  for (size_t i=0; i<klen; i++) { k_ipad[i] ^= key[i]; k_opad[i] ^= key[i]; }
  sha1_init(out.inner); sha1_update(out.inner, k_ipad, 64);
  sha1_init(out.outer); sha1_update(out.outer, k_opad, 64);
}

// HMAC-SHA1 20-byte word-level (hot PBKDF2 inner loop)
__attribute__((optimize("O3"), hot))
static void hmac_sha1_20w(const FastHmacPre &pre, const uint32_t data[5], uint32_t out[5]) {
  uint32_t state[5];
  memcpy(state, pre.inner.state, 20);
  sha1_transform_20w(state, data);
  uint32_t ih[5] = {state[0], state[1], state[2], state[3], state[4]};
  memcpy(state, pre.outer.state, 20);
  sha1_transform_20w(state, ih);
  memcpy(out, state, 20);
}

void fast_hmac_sha1_pre(const FastHmacPre &pre, const uint8_t *data, size_t dlen, uint8_t *out20) {
  uint8_t inner_hash[20];
  FastSha1Ctx ctx = pre.inner;
  sha1_update(ctx, data, dlen);
  sha1_final(ctx, inner_hash);
  ctx = pre.outer;
  sha1_update(ctx, inner_hash, 20);
  sha1_final(ctx, out20);
}

// ── PBKDF2-HMAC-SHA1 (4096 iterations, 32-byte output) ───────────────────

__attribute__((optimize("O3"), hot))
void fast_pbkdf2(const FastHmacPre &pre, const uint8_t *salt, size_t slen,
                 uint32_t iters, uint8_t out[32]) {
  uint8_t salt_int[40]; // max SSID 32 + 4
  memcpy(salt_int, salt, slen);
  uint8_t U8[20];
  uint32_t U[5], T[5];

  // Block 1 (bytes 0-19)
  salt_int[slen]=0; salt_int[slen+1]=0; salt_int[slen+2]=0; salt_int[slen+3]=1;
  fast_hmac_sha1_pre(pre, salt_int, slen+4, U8);
  for (int i=0; i<5; i++) { uint32_t t; memcpy(&t, U8+i*4, 4); U[i]=__builtin_bswap32(t); }
  memcpy(T, U, 20);
  for (uint32_t i=1; i<iters; i++) {
    hmac_sha1_20w(pre, U, U);
    T[0]^=U[0]; T[1]^=U[1]; T[2]^=U[2]; T[3]^=U[3]; T[4]^=U[4];
  }
  sha1_extract(T, out);

  // Block 2 (bytes 20-31)
  salt_int[slen+3]=2;
  fast_hmac_sha1_pre(pre, salt_int, slen+4, U8);
  for (int i=0; i<5; i++) { uint32_t t; memcpy(&t, U8+i*4, 4); U[i]=__builtin_bswap32(t); }
  memcpy(T, U, 20);
  for (uint32_t i=1; i<iters; i++) {
    hmac_sha1_20w(pre, U, U);
    T[0]^=U[0]; T[1]^=U[1]; T[2]^=U[2]; T[3]^=U[3]; T[4]^=U[4];
  }
  sha1_extract(T, out+20);
}

// ── PRF-512 ───────────────────────────────────────────────────────────────

void fast_prf512(const uint8_t pmk[32], const uint8_t *prf_data, uint8_t ptk[64]) {
  static const char label[] = "Pairwise key expansion";
  uint8_t ctr = 0;
  size_t pos = 0;
  FastHmacPre pre;
  fast_hmac_precompute(pmk, 32, pre);

  while (pos < 64) {
    FastSha1Ctx ctx = pre.inner;
    sha1_update(ctx, (const uint8_t*)label, sizeof(label));
    sha1_update(ctx, prf_data, 6+6+32+32);
    sha1_update(ctx, &ctr, 1);
    uint8_t inner_hash[20];
    sha1_final(ctx, inner_hash);

    FastSha1Ctx octx = pre.outer;
    sha1_update(octx, inner_hash, 20);
    uint8_t digest[20];
    sha1_final(octx, digest);

    size_t c = (64-pos < 20) ? (64-pos) : 20;
    memcpy(ptk+pos, digest, c);
    pos += c; ctr++;
  }
}

// ── Full password test ────────────────────────────────────────────────────

__attribute__((optimize("O3"), hot))
bool fast_wpa2_try_password(const char *pw, uint8_t pw_len,
                            const char *ssid, uint8_t ssid_len,
                            const uint8_t *prf_data,
                            const uint8_t *eapol, uint16_t eapol_len,
                            const uint8_t *mic) {
  FastHmacPre pw_pre;
  fast_hmac_precompute((const uint8_t*)pw, pw_len, pw_pre);
  uint8_t pmk[32];
  fast_pbkdf2(pw_pre, (const uint8_t*)ssid, ssid_len, 4096, pmk);
  uint8_t ptk[64];
  fast_prf512(pmk, prf_data, ptk);
  // MIC verification
  uint8_t calc_mic[20];
  FastHmacPre mic_pre;
  fast_hmac_precompute(ptk, 16, mic_pre);
  fast_hmac_sha1_pre(mic_pre, eapol, eapol_len, calc_mic);
  return memcmp(calc_mic, mic, 16) == 0;
}
