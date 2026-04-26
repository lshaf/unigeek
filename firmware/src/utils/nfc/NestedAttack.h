#pragma once

#include <MFRC522_I2C.h>
#include <cstdint>

namespace NestedAttack {

struct Result {
  bool success = false;
  uint64_t key = 0;
};

using ProgressFn = bool(*)(const char* msg, int pct);

/**
 * Nested attack — for cards with a normal (non-static) PRNG nonce.
 * Also works on static-nonce cards.
 *
 * Requires one known key on any sector. Collects encrypted nested nonces,
 * enumerates all 65535 PRNG distances to find the target nonce, then uses
 * lfsr_recovery32 to recover the target key.
 *
 * @param module       MFRC522_I2C instance
 * @param uid          32-bit card UID
 * @param authCmd      key type for exploit sector (KEY_A or KEY_B)
 * @param exploitBlock trailer block with known key
 * @param knownKey     48-bit known key
 * @param targetCmd    key type to attack on target sector
 * @param targetBlock  trailer block of sector to crack
 * @param progress     optional progress callback
 */
Result crack(MFRC522_I2C* module, uint32_t uid,
             uint8_t authCmd, uint8_t exploitBlock, uint64_t knownKey,
             uint8_t targetCmd, uint8_t targetBlock,
             ProgressFn progress = nullptr);

}