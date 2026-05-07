#include "Bip39.h"
#include "Bip39Wordlist.h"

#include <mbedtls/sha256.h>
#include <string.h>

namespace unigeek {
namespace crypto {

size_t Bip39::wordCountForEntropy(size_t entropyLen)
{
  switch (entropyLen) {
    case 16: return 12;
    case 20: return 15;
    case 24: return 18;
    case 28: return 21;
    case 32: return 24;
    default: return 0;
  }
}

bool Bip39::encode(const uint8_t* entropy, size_t entropyLen, uint16_t* outIdx)
{
  if (!entropy || !outIdx) return false;
  size_t words = wordCountForEntropy(entropyLen);
  if (!words) return false;

  // Checksum = first (entropyLen / 4) bits of SHA-256(entropy).
  uint8_t hash[32];
  if (mbedtls_sha256_ret(entropy, entropyLen, hash, 0) != 0) return false;
  size_t csBits = entropyLen / 4;        // 4..8 bits

  // Concatenate entropy || checksum into a bit buffer (max 264 bits = 33 B).
  uint8_t buf[33] = {0};
  memcpy(buf, entropy, entropyLen);
  // Append the top csBits of hash[0] right after the entropy.
  // Since csBits <= 8, hash[0]'s high csBits are what we want.
  // Bits live MSB-first in this scheme.
  uint8_t csByte = (uint8_t)(hash[0] & (0xFF << (8 - csBits)));
  buf[entropyLen] = csByte;

  // Slice into 11-bit chunks, MSB-first.
  for (size_t w = 0; w < words; w++) {
    size_t bitPos = w * 11;
    size_t bytePos = bitPos / 8;
    size_t bitInByte = bitPos % 8;

    // Read 24 bits starting at bytePos, then shift.
    uint32_t v = ((uint32_t)buf[bytePos]     << 16)
               | ((uint32_t)buf[bytePos + 1] <<  8)
               | ((uint32_t)buf[bytePos + 2]);
    uint32_t shifted = v >> (24 - bitInByte - 11);
    outIdx[w] = (uint16_t)(shifted & 0x07FF);  // 11-bit mask
  }
  return true;
}

bool Bip39::decode(const uint16_t* idx, size_t wordCount, uint8_t* outEntropy)
{
  if (!idx || !outEntropy) return false;

  // Recover entropyLen from wordCount: (wc * 11) bits = entropy + cs.
  // entropy bits = wc * 11 * 32 / 33; entropyLen bytes = entropyBits / 8.
  // Equivalent table:
  size_t entropyLen = 0;
  switch (wordCount) {
    case 12: entropyLen = 16; break;
    case 15: entropyLen = 20; break;
    case 18: entropyLen = 24; break;
    case 21: entropyLen = 28; break;
    case 24: entropyLen = 32; break;
    default: return false;
  }
  size_t csBits  = entropyLen / 4;
  size_t totBits = entropyLen * 8 + csBits;
  size_t totBytes = (totBits + 7) / 8;   // 17/22/27/31/33

  // Reassemble the bit-stream from word indices, MSB-first per word.
  uint8_t buf[33] = {0};
  for (size_t w = 0; w < wordCount; w++) {
    if (idx[w] >= 2048) return false;
    uint16_t v = idx[w] & 0x07FF;
    size_t bitPos    = w * 11;
    size_t bytePos   = bitPos / 8;
    size_t bitInByte = bitPos % 8;

    // Splat the 11 bits into a 24-bit window starting at `bytePos`,
    // aligned so the high bit of v lands at (bytePos, bitInByte).
    uint32_t shifted = ((uint32_t)v) << (24 - bitInByte - 11);
    if (bytePos     < totBytes) buf[bytePos]     |= (uint8_t)((shifted >> 16) & 0xFF);
    if (bytePos + 1 < totBytes) buf[bytePos + 1] |= (uint8_t)((shifted >>  8) & 0xFF);
    if (bytePos + 2 < totBytes) buf[bytePos + 2] |= (uint8_t)((shifted      ) & 0xFF);
  }

  // Extract entropy + checksum byte.
  memcpy(outEntropy, buf, entropyLen);
  uint8_t csGiven = buf[entropyLen] & (uint8_t)(0xFF << (8 - csBits));

  // Verify checksum = leading csBits of SHA-256(entropy).
  uint8_t hash[32];
  if (mbedtls_sha256_ret(outEntropy, entropyLen, hash, 0) != 0) return false;
  uint8_t csCalc = hash[0] & (uint8_t)(0xFF << (8 - csBits));

  if (csGiven != csCalc) return false;
  return true;
}

int Bip39::wordIndex(const char* word)
{
  if (!word || !word[0]) return -1;
  int lo = 0, hi = 2047;
  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    int c = strcmp(word, kBip39EnglishWordlist[mid]);
    if (c == 0) return mid;
    if (c < 0)  hi = mid - 1;
    else        lo = mid + 1;
  }
  return -1;
}

int Bip39::prefixMatchCount(const char* prefix, int* outFirstIdx)
{
  // Empty prefix matches every word — let the caller iterate from index 0.
  if (!prefix || !prefix[0]) {
    if (outFirstIdx) *outFirstIdx = 0;
    return 2048;
  }
  size_t pLen = strlen(prefix);

  // Lower-bound: first index whose word >= prefix.
  int lo = 0, hi = 2048;
  while (lo < hi) {
    int mid = (lo + hi) >> 1;
    if (strcmp(kBip39EnglishWordlist[mid], prefix) < 0) lo = mid + 1;
    else                                                hi = mid;
  }
  int start = lo;

  // Walk forward while the entry still starts with `prefix`. The wordlist
  // is small (2048) and prefix runs are bounded by the 4-char uniqueness
  // property — at most a few entries per prefix.
  int count = 0;
  int firstIdx = -1;
  for (int i = start; i < 2048; i++) {
    if (strncmp(kBip39EnglishWordlist[i], prefix, pLen) != 0) break;
    if (firstIdx < 0) firstIdx = i;
    count++;
  }

  if (outFirstIdx) *outFirstIdx = firstIdx;
  return count;
}

int Bip39::nextChars(const char* prefix, char* outChars, int maxOut)
{
  if (!outChars || maxOut <= 0) return 0;
  outChars[0] = '\0';

  int firstIdx = 0;
  int count    = prefixMatchCount(prefix, &firstIdx);
  if (count == 0 || firstIdx < 0) return 0;

  size_t pLen = (prefix && prefix[0]) ? strlen(prefix) : 0;

  // Bitmap over a-z keeps the de-dup branchless and order-stable.
  uint32_t seen = 0;
  for (int i = firstIdx; i < firstIdx + count; i++) {
    const char* w = kBip39EnglishWordlist[i];
    if (strlen(w) <= pLen) continue;          // exact-length match: no next char
    char c = w[pLen];
    if (c < 'a' || c > 'z') continue;
    seen |= (1u << (uint32_t)(c - 'a'));
  }

  int outCount = 0;
  for (int b = 0; b < 26 && outCount + 1 < maxOut; b++) {
    if (seen & (1u << (uint32_t)b)) outChars[outCount++] = (char)('a' + b);
  }
  outChars[outCount] = '\0';
  return outCount;
}

}  // namespace crypto
}  // namespace unigeek
