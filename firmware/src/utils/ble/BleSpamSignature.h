#pragma once

#include <NimBLEDevice.h>
#include <stdint.h>

// BLE spam recognition shared by BLE Watchdog and BLE Detector.
//
// signatureType() names the attack an advertisement looks like (the same
// names as the BLE Spam attack: Apple Pairing, Apple Action, Android Fast
// Pair, ...). Real devices send some of these advertisements too (AirPods,
// iPhones, Fast Pair headphones), always from one stable address; a spammer
// draws a new random address for every advertisement. SpamTracker therefore
// reports a spam only once one type comes from several distinct addresses
// within a short window.
namespace BleSpam {

const char* signatureType(NimBLEAdvertisedDevice* dev);

class SpamTracker {
public:
  static constexpr uint8_t       MIN_ADDRESSES = 3;
  static constexpr unsigned long WINDOW_MS     = 5000UL;

  // Records one advertisement of `type` (a pointer returned by
  // signatureType()) from `address`. Returns true while the type is a spam:
  // MIN_ADDRESSES distinct addresses within WINDOW_MS.
  bool observe(const char* type, const char* address, unsigned long now);
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
  };
  Slot _slots[MAX_TYPES];
  bool _justDetected = false;
};

}  // namespace BleSpam
