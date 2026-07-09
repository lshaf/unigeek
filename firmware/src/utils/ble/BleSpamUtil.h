//
// BLE Spam engine — unified advertising spammer.
// Device tables, attack types, TX-power / MAC-randomization / custom-name
// config on unigeek's NimBLE 1.4 advertising path. UI lives in the BLESpam*
// screens; this class is headless.
//
#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>


class BleSpamUtil {
public:
  enum Attack {
    APPLE_PAIRING = 0,   // ProximityPair popup (AirPods/Beats)
    APPLE_ACTION,        // NearbyAction modal (Setup New Phone, AppleTV…)
    APPLE_NYD,           // "Not Your Device" ProximityPair variant
    ANDROID,             // Google Fast Pair
    WINDOWS,             // Microsoft Swift Pair
    SAMSUNG,             // Galaxy Buds / Watch EasySetup
    BEACON,              // named BLE beacon
    RANDOM_ALL,          // pick a random attack+device each packet
    ATTACK_COUNT
  };
  enum TxPower { TX_MAX = 0, TX_HIGH, TX_MEDIUM, TX_LOW };
  enum MacRand { MAC_OFF = 0, MAC_1, MAC_2, MAC_3, MAC_5, MAC_10, MAC_25, MAC_50 };

  // ── Shared settings (persist across screens) ───────────────────────────────
  static TxPower  txPower;
  static MacRand  macRand;
  static uint32_t advMs;   // advertising window per packet (default 15)
  static uint32_t gapMs;   // idle gap between packets      (default 5)
  static char     customName[32];

  static const char* attackLabel(int a);
  static int         deviceCount(int a);                 // selectable devices (no Random/All)
  static const char* deviceLabel(int a, int idx);        // idx in [0, deviceCount)
  static const char* txLabel(int p);
  static const char* macLabel(int m);

  // ── Run ──────────────────────────────────────────────────────────────────
  // deviceIndex < 0 (or == deviceCount) means "Random / All".
  void begin(Attack attack, int deviceIndex);
  void end();
  void tick();                                   // send-tick state machine
  uint32_t packets() const { return _packets; }
  Attack   attack()  const { return _attack; }
  int      deviceIndex() const { return _deviceIndex; }

private:
  NimBLEAdvertising* _pAdv = nullptr;
  Attack   _attack      = APPLE_PAIRING;
  int      _deviceIndex = -1;
  uint32_t _packets     = 0;
  bool     _macInit     = false;
  bool     _started     = false;

  // Send-tick timing: next refresh time; the advertiser runs continuously between.
  uint32_t _nextSendMs  = 0;

  void _applyTxPower();
  bool _shouldRotateMac() const;
  void _rotateMacInterface();
  bool _build(Attack attack, int deviceIndex, NimBLEAdvertisementData& out);
};
