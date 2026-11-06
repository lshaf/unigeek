#pragma once

#include <NimBLEDevice.h>
#include <stdint.h>

// BLE spam recognition shared by BLE Watchdog and BLE Detector.
//
// signatureType() names the attack an advertisement looks like (the same
// names as the BLE Spam attack: Apple Pairing, Apple Action, Android Fast
// Pair, ...). Real devices send some of these advertisements too (AirPods,
// iPhones, Fast Pair headphones), always from one stable address and with a
// steady payload; a spammer draws a new random address for every
// advertisement, or, with a fixed address, keeps changing the payload
// (random bytes, models, actions). SpamTracker therefore reports a spam once
// one type comes from several distinct addresses, or with several distinct
// payloads, within a short window.
namespace BleSpam {

const char* signatureType(NimBLEAdvertisedDevice* dev);
// Hash of the advertisement's manufacturer and service data (FNV-1a).
uint32_t payloadHash(NimBLEAdvertisedDevice* dev);

class SpamTracker {
public:
  static constexpr uint8_t       MIN_ADDRESSES = 3;
  static constexpr uint8_t       MIN_PAYLOADS  = 5;  // fixed-address tools
  static constexpr unsigned long WINDOW_MS     = 5000UL;

  // Records one advertisement of `type` (a pointer returned by
  // signatureType()) from `address` with payloadHash(). Returns true while
  // the type is a spam: MIN_ADDRESSES distinct addresses, or MIN_PAYLOADS
  // distinct payloads, within WINDOW_MS.
  bool observe(const char* type, const char* address, uint32_t payload, unsigned long now);
  // True only on the advertisement that makes the type a spam in a window.
  bool justDetected() const { return _justDetected; }
  void clear();

private:
  static constexpr uint8_t MAX_TYPES = 12;
  struct Slot {
    const char*   type = nullptr;
    unsigned long windowStart = 0;
    uint8_t       count = 0;
    char          addresses[MIN_ADDRESSES][18] = {};
    uint8_t       payloadCount = 0;
    uint32_t      payloads[MIN_PAYLOADS] = {};
  };
  Slot _slots[MAX_TYPES];
  bool _justDetected = false;
};

}  // namespace BleSpam
