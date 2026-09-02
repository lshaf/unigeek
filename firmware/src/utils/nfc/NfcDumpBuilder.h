#pragma once

#include <stddef.h>
#include <stdint.h>

class NfcDumpBuilder {
public:
  static constexpr size_t MIFARE_CLASSIC_1K_SIZE = 1024;
  static constexpr size_t MIFARE_CLASSIC_1K_NDEF_CAPACITY = 720;
  static constexpr size_t MIFARE_CLASSIC_4K_SIZE = 4096;
  static constexpr size_t MIFARE_CLASSIC_4K_NDEF_CAPACITY = 3360;

  static constexpr size_t NTAG210_SIZE = 80;
  static constexpr size_t NTAG212_SIZE = 164;
  static constexpr size_t NTAG213_SIZE = 180;
  static constexpr size_t NTAG215_SIZE = 540;
  static constexpr size_t NTAG216_SIZE = 924;

  static constexpr size_t NTAG210_NDEF_CAPACITY = 48;
  static constexpr size_t NTAG212_NDEF_CAPACITY = 128;
  static constexpr size_t NTAG213_NDEF_CAPACITY = 144;
  static constexpr size_t NTAG215_NDEF_CAPACITY = 496;
  static constexpr size_t NTAG216_NDEF_CAPACITY = 872;

  enum class Ntag21xType : uint8_t {
    NTAG210, NTAG212, NTAG213, NTAG215, NTAG216
  };

  // Build a raw 64-block MIFARE Classic 1K image formatted as an
  // NFC Forum NDEF tag (MAD1 + NFC Forum sectors).
  // `ndef == nullptr && ndefLen == 0` builds an empty NDEF-formatted tag.
  static bool buildMifareClassic1K(const uint8_t uid[4],
                                   const uint8_t* ndef, size_t ndefLen,
                                   uint8_t* out, size_t& outLen, size_t maxLen);

  // Build a raw 256-block MIFARE Classic 4K image formatted as an
  // NFC Forum NDEF tag (MAD1 + MAD2 + NFC Forum sectors).
  // `ndef == nullptr && ndefLen == 0` builds an empty NDEF-formatted tag.
  static bool buildMifareClassic4K(const uint8_t uid[4],
                                   const uint8_t* ndef, size_t ndefLen,
                                   uint8_t* out, size_t& outLen, size_t maxLen);

  // Build a raw NTAG21x page image for Chameleon Ultra.
  static bool buildNtag21x(Ntag21xType type, const uint8_t uid[7],
                           const uint8_t* ndef, size_t ndefLen,
                           uint8_t* out, size_t& outLen, size_t maxLen);

  static bool buildNtag215(const uint8_t uid[7],
                           const uint8_t* ndef, size_t ndefLen,
                           uint8_t* out, size_t& outLen, size_t maxLen);
};
