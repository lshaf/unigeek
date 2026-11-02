#include "BleSpamSignature.h"

#include <string.h>

namespace BleSpam {

namespace {

// Manufacturer-data signatures, as hex nibbles ('_' = any). Only the given
// nibbles are compared, so a short payload (Apple Action: 9 bytes) matches.
// More specific signatures come first.
struct Signature {
  const char* pattern;
  const char* type;
};

constexpr Signature kSignatures[] = {
  {"4c00071907",               "Apple Pairing"},          // proximity pairing, new device
  {"4c00071901",               "Apple Not-Your-Device"},
  {"4c0004042a0000000f05c1__604c950", "Apple Setup"},
  {"4c000f05",                 "Apple Action"},           // nearby action, any flags
  {"0600030080",               "Windows Swift Pair"},
  {"750042098102141503210109", "Samsung Buds"},
  {"7500010002000101ff000043", "Samsung Watch"},
  {"ff006db643ce97fe427c",     "Love Toys"},
};

const NimBLEUUID kFastPairService((uint16_t)0xFE2C);

bool matchPattern(const char* pattern, const uint8_t* data, size_t len)
{
  const size_t patLen = strlen(pattern);
  if (len * 2 < patLen) return false;
  for (size_t i = 0; i < patLen; ++i) {
    const char c = pattern[i];
    if (c == '_') continue;
    int nibble;
    if (c >= '0' && c <= '9')      nibble = c - '0';
    else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
    else return false;
    const uint8_t actual = (i & 1) ? (data[i / 2] & 0x0F) : (data[i / 2] >> 4);
    if (actual != nibble) return false;
  }
  return true;
}

}  // namespace

const char* signatureType(NimBLEAdvertisedDevice* dev)
{
  if (!dev) return nullptr;
  for (size_t m = 0; m < dev->getManufacturerDataCount(); ++m) {
    const std::string& data = dev->getManufacturerData(m);
    const uint8_t* p = reinterpret_cast<const uint8_t*>(data.data());
    for (const auto& sig : kSignatures)
      if (matchPattern(sig.pattern, p, data.size())) return sig.type;
  }
  // Android Fast Pair announces itself in service data (UUID 0xFE2C), not in
  // manufacturer data: a 3-byte model ID, or longer formats from other tools.
  if (dev->getServiceDataCount() > 0 && dev->getServiceData(kFastPairService).size() >= 3)
    return "Android Fast Pair";
  return nullptr;
}

uint32_t payloadHash(NimBLEAdvertisedDevice* dev)
{
  uint32_t h = 2166136261u;
  auto mix = [&h](const std::string& s) {
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    h ^= 0xFF; h *= 16777619u;  // record separator
  };
  if (!dev) return h;
  for (size_t m = 0; m < dev->getManufacturerDataCount(); ++m) mix(dev->getManufacturerData(m));
  if (dev->getServiceDataCount() > 0) mix(dev->getServiceData(kFastPairService));
  return h;
}

bool SpamTracker::observe(const char* type, const char* address, uint32_t payload, unsigned long now)
{
  _justDetected = false;
  if (!type || !address) return false;

  Slot* slot = nullptr;
  for (auto& s : _slots) {
    if (s.type == type) { slot = &s; break; }
  }
  if (!slot) {
    for (auto& s : _slots) {
      if (!s.type) { slot = &s; break; }
    }
    if (!slot) {  // all used: reuse the stalest
      slot = &_slots[0];
      for (auto& s : _slots)
        if (s.windowStart < slot->windowStart) slot = &s;
    }
    *slot = Slot{};
    slot->type = type;
    slot->windowStart = now;
  }

  if (now - slot->windowStart > WINDOW_MS) {
    // A new window keeps a running spam alive: start from this address.
    slot->windowStart = now;
    const bool wasSpam = slot->count >= MIN_ADDRESSES || slot->payloadCount >= MIN_PAYLOADS;
    slot->count = 0;
    slot->payloadCount = 0;
    strncpy(slot->addresses[slot->count++], address, sizeof(slot->addresses[0]) - 1);
    slot->payloads[slot->payloadCount++] = payload;
    return wasSpam;
  }

  if (slot->count >= MIN_ADDRESSES || slot->payloadCount >= MIN_PAYLOADS) return true;

  bool knownAddress = false;
  for (uint8_t i = 0; i < slot->count; ++i)
    if (strcmp(slot->addresses[i], address) == 0) { knownAddress = true; break; }
  if (!knownAddress)
    strncpy(slot->addresses[slot->count++], address, sizeof(slot->addresses[0]) - 1);

  bool knownPayload = false;
  for (uint8_t i = 0; i < slot->payloadCount; ++i)
    if (slot->payloads[i] == payload) { knownPayload = true; break; }
  if (!knownPayload) slot->payloads[slot->payloadCount++] = payload;

  if (slot->count >= MIN_ADDRESSES || slot->payloadCount >= MIN_PAYLOADS) {
    _justDetected = true;
    return true;
  }
  return false;
}

void SpamTracker::clear()
{
  for (auto& s : _slots) s = Slot{};
  _justDetected = false;
}

}  // namespace BleSpam
