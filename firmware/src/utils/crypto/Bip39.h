#pragma once

#include <stdint.h>
#include <stddef.h>

namespace unigeek {
namespace crypto {

// BIP-39 mnemonic encoding (English wordlist only).
//
// Standard mappings:
//   16 B entropy -> 12 words (CS=4 bits)
//   20 B         -> 15 words (CS=5 bits)
//   24 B         -> 18 words (CS=6 bits)
//   28 B         -> 21 words (CS=7 bits)
//   32 B entropy -> 24 words (CS=8 bits)   <-- used for FIDO master key backup
//
// Encoder takes the entropy bytes and returns indices into the BIP-39 English
// wordlist, NOT the words themselves; callers look up words via
// `kBip39EnglishWordlist[idx]` to keep this module Arduino-free / testable.
class Bip39 {
public:
  // Returns the number of words for `entropyLen` bytes, or 0 if invalid.
  // Valid entropy lengths: 16, 20, 24, 28, 32.
  static size_t wordCountForEntropy(size_t entropyLen);

  // Encode `entropy` into BIP-39 word indices. `outIdx` must hold at least
  // wordCountForEntropy(entropyLen) entries. Returns true on success.
  // Computes the SHA-256-based checksum internally; caller does not provide it.
  static bool encode(const uint8_t* entropy, size_t entropyLen,
                     uint16_t* outIdx);

  // Decode `wordCount` BIP-39 word indices back to entropy. Verifies the
  // checksum (last entropyLen/4 bits must match SHA-256(entropy)[0]).
  // `wordCount` must be 12/15/18/21/24; `outEntropy` must hold the
  // corresponding 16/20/24/28/32 bytes. Returns false on invalid count,
  // out-of-range index (>=2048), or checksum mismatch.
  static bool decode(const uint16_t* idx, size_t wordCount,
                     uint8_t* outEntropy);

  // Exact lookup: binary search the lex-sorted English wordlist.
  // Returns 0..2047 on hit, -1 on miss. Case-sensitive; caller should
  // pre-lowercase if needed.
  static int wordIndex(const char* word);

  // Prefix lookup: counts how many words start with `prefix`. Also writes
  // the *first* matching word's index to *outFirstIdx (when non-null) —
  // useful when count==1 (unique autocomplete) AND when count<=N (show
  // the matching slice in a UI).
  static int prefixMatchCount(const char* prefix, int* outFirstIdx = nullptr);

  // For a given `prefix`, walk the matching slice and emit each distinct
  // character that appears at position strlen(prefix) — i.e. the legal
  // next-letter set. Output is lowercase ASCII, lex-sorted, NUL-terminated.
  // Returns the count of unique next-chars (always <= 26). Words whose
  // length equals strlen(prefix) (full match) contribute no next-char.
  static int nextChars(const char* prefix, char* outChars, int maxOut);
};

}  // namespace crypto
}  // namespace unigeek
