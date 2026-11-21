//
// Shared "Options" popup for a network host, used by IP Scan, Monitor Hosts and
// the Network Watchdog/Watchcat. Builds a curated menu (only the actions that
// make sense for the target) and dispatches the choice to the matching screen.
//
#pragma once

#include <Arduino.h>
#include <functional>

// A host the popup can act on. `ip` is in network byte order (same as
// WiFi.*IP()). `mac` is optional: when `hasMac` is false the helper resolves it
// from the ARP cache (and, for Fox Hunt, probes when the user picks it).
struct HostTarget {
  uint32_t    ip = 0;
  bool        hasMac = false;
  uint8_t     mac[6] = {};
  const char* name = nullptr;   // hostname/label, optional (shown in Details)
};

namespace NetworkHostActions {

// Read a host's MAC. Always checks the ARP cache; when `probe` is true and the
// entry is absent, sends an ARP request and waits briefly (blocking ~1s max).
// Only same-subnet peers are probed. Returns true and fills `mac` on success.
bool lookupHostMac(const char* ip, uint8_t mac[6], bool probe);

// Show the curated "Options" popup for `t` and run the chosen action.
// `onLeave` (if set) is invoked right before pushing any child screen — callers
// that run a live radio capture (e.g. the promiscuous watches) use it to stop
// the capture; they re-arm it from their own resume hook.
// Returns true if a child screen was pushed (so the caller can decide whether to
// redraw itself; on cancel or a guard error it returns false).
bool openOptions(const HostTarget& t, const std::function<void()>& onLeave = {});

}  // namespace NetworkHostActions
