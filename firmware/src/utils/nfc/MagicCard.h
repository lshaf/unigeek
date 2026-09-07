#pragma once

#include <stdint.h>

// Non-destructive Magic MIFARE Classic classification shared by NFC backends.
// Some families intentionally remain UNKNOWN until we have a reliable read-only
// discriminator for them (notably Gen2/CUID and Gen4 variants).
enum class MagicCardType : uint8_t {
  NONE = 0,
  GEN1A,
  GEN2,
  GEN3,
  GEN4,
  UNKNOWN,
};

inline const char* magicCardTypeName(MagicCardType type) {
  switch (type) {
    case MagicCardType::GEN1A: return "Gen1A-compatible";
    case MagicCardType::GEN2:  return "Gen2 / CUID";
    case MagicCardType::GEN3:  return "Gen3 / APDU";
    case MagicCardType::GEN4:  return "Gen4";
    case MagicCardType::UNKNOWN:return "Unknown Magic";
    case MagicCardType::NONE:
    default:                   return "None";
  }
}
