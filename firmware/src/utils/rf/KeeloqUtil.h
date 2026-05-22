//
// KeeloqUtil — KeeLoq block cipher + decode of RcSwitch-protocol-23 captures.
// Ported from Bruce firmware (rf_utils.cpp / rf_scan.cpp). Algorithm constants
// (NLF, g5 selector) are identical so .sub files captured here are compatible
// with Bruce-generated keystores at /unigeek/mfcodes.
//

#pragma once
#include <Arduino.h>
#include "CC1101Util.h"

class KeeloqUtil {
public:
  static constexpr uint8_t LEARNING_SIMPLE = 1;
  static constexpr uint8_t LEARNING_NORMAL = 2;

  // Block cipher primitives (528 rounds, 64-bit key, 32-bit block).
  static uint32_t encrypt(uint32_t data, uint64_t key);
  static uint32_t decrypt(uint32_t data, uint64_t key);

  // Derives the per-fob manufacturer key for "normal learning" mode by
  // double-decrypting the fob's fix word.
  static uint64_t normalLearning(uint32_t data, uint64_t key);

  // Reverses the lowest `bits` bits of `num`. RcSwitch decodes KeeLoq frames
  // MSB-first; the cipher operates on the LSB-first representation.
  static uint64_t reverseBits(uint64_t num, uint8_t bits);

  // Unpacks a 64-bit RcSwitch-protocol-23 capture into the four structured
  // KeeLoq fields (fix word + serial + button + encrypted hop). Always safe
  // to call on a fresh capture — does no cipher work, no keystore lookup.
  static void unpack(uint64_t decoded, uint32_t& fix, uint32_t& encrypted,
                     uint8_t& btn, uint32_t& serial);

  // Tries every manufacturer key in KeeloqKeystore to decrypt sig.encrypted.
  // On the first match sets sig.mf_name, sig.hop, sig.cnt and returns true.
  // sig.fix / sig.encrypted / sig.btn / sig.serial must already be populated
  // (call unpack() first). Silently no-op when the keystore is empty/absent.
  static bool identify(CC1101Util::Signal& sig);

  // Counter-step replay (Bruce's keeloq_step). Increments sig.cnt by `step`,
  // rebuilds the hop word with the manufacturer-specific bit layout, looks up
  // the manufacturer key in KeeloqKeystore, re-encrypts, and updates sig.key
  // so a subsequent RCSwitch transmit emits the next-counter frame the
  // receiver expects.
  //
  // Requires sig.mf_name to be set (matched at capture time) AND the key for
  // that manufacturer to exist in the keystore. Returns true on success;
  // false if the manufacturer is not in the keystore (sig is left untouched).
  //
  // Mutates the signal — repeated calls advance the counter each time.
  static bool step(CC1101Util::Signal& sig, uint16_t increment = 1);
};
