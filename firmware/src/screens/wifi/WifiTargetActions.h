#pragma once

#include <stdint.h>

// A Wi-Fi target picked in a monitoring screen (Scanner, Traffic Analyzer, and
// later Watchdog / Watchcat) and the actions offered for it in the "Options"
// popup. Shared so every screen offers the same actions, in the same order,
// and opens them the same way.
struct WifiTarget {
  bool    client   = false;  // station (true) or access point (false)
  uint8_t mac[6]   = {};     // station MAC, or the AP's BSSID
  bool    hasAp    = false;  // station only: its AP is known
  uint8_t bssid[6] = {};     // the AP: the target itself, or the station's AP
  uint8_t channel  = 0;
  char    ssid[33] = {};     // the AP's SSID, empty if unknown or hidden
};

namespace WifiTargetActions {

enum Action : uint8_t {
  NONE     = 0,
  DETAILS  = 1 << 0,
  MONITOR  = 1 << 1,
  SNIFFER  = 1 << 2,
  FOX_HUNT = 1 << 3,
  DEAUTH   = 1 << 4,
  // Fox Hunt the station's AP (target.bssid) instead of the station. When
  // both are offered they read "Fox Hunt (Client)" and "Fox Hunt (AP)".
  FOX_HUNT_AP = 1 << 5,
};

// Show the "Options" popup with the allowed actions, always in the same order.
// Returns NONE when cancelled. A live screen must stop its own capture only
// after a choice, so cancelling leaves it untouched.
Action choose(uint8_t allowed);

// Push the screen for MONITOR, SNIFFER, FOX_HUNT or DEAUTH aimed at the target.
// DETAILS is left to the caller: each screen shows its own details.
void open(Action action, const WifiTarget& target);

// Channel-only Packet Monitor / Packet Sniffer (no AP or station filter).
void openChannel(Action action, uint8_t channel);

}  // namespace WifiTargetActions
