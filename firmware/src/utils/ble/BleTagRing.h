#pragma once
#include <NimBLEDevice.h>

// Rings ONE selected tracker so the user can find it (the Android/AirGuard
// "Play Sound" for a non-owner). Never a list: this takes a single device, so
// it cannot be the "ring every nearby tag" prank. Works only while the tag is
// separated from its owner (Apple rejects the command otherwise).
//
// Tries the tag's own protocol, then the open DULT standard:
//   AirTag : service 7DFC9000-..., char 7DFC9001-..., write 1 byte 0xAF.
//   DULT   : service 15190001-..., char 8E0C0001-..., write 0x00 0x03.
// (Samsung's own protocol is not public, so a SmartTag rings only if it
// supports DULT.) GATT details confirmed from AirGuard (AppleFindMy.kt).
class BleTagRing {
public:
  enum class Result : uint8_t {
    Ringing...,           // command accepted
    RejectedOwnerNearby,  // connected but the tag refused (owner in range)
    NotReachable,         // could not connect
    NotSupported,         // no known sound service on the device
  };

  // The caller must have stopped scanning / released the radio first.
  static Result play(const NimBLEAddress& address);
  static Result stop(const NimBLEAddress& address);

  static const char* resultText(Result r);

private:
  static Result send(const NimBLEAddress& address, bool start);
};
