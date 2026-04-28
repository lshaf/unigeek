#pragma once
#include <stdint.h>

// Shared protocol for Karma Attack <-> Karma Support ESP-NOW communication.
// Magic bytes distinguish Karma frames from other ESP-NOW traffic.

static constexpr uint8_t KARMA_ESPNOW_MAGIC[4] = {0x4B, 0x52, 0x4D, 0x41}; // "KRMA"

enum KarmaCmd : uint8_t {
  KARMA_HELLO     = 0x01,  // Support -> broadcast: available and waiting
  KARMA_DEPLOY    = 0x02,  // Main    -> Support:   create AP (ssid + password)
  KARMA_TEARDOWN  = 0x03,  // Main    -> Support:   stop current AP
  KARMA_ACK       = 0x04,  // Support -> Main:      AP is up (includes bssid)
  KARMA_DONE      = 0x05,  // Main    -> Support:   attack finished or stopped
  KARMA_HEARTBEAT = 0x06,  // Main    -> Support:   still alive (keepalive)
};

#pragma pack(push, 1)
struct KarmaMsg {
  uint8_t  magic[4];
  KarmaCmd cmd;
  char     ssid[33];
  char     password[64];
  uint8_t  bssid[6];    // filled by KARMA_ACK
  uint8_t  success;     // 1 = ok, filled by KARMA_ACK
};
#pragma pack(pop)
