#include "WifiTargetActions.h"

#include <stdio.h>
#include <string.h>
#include "core/ScreenManager.h"
#include "screens/wifi/network/NetworkMenuScreen.h"
#include "screens/wifi/WifiDeauthDisassocScreen.h"
#include "screens/wifi/WifiFoxHuntScreen.h"
#include "screens/wifi/WifiPacketMonitorScreen.h"
#include "screens/wifi/WifiPacketSnifferScreen.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/network/WifiUtility.h"

namespace WifiTargetActions {

namespace {

struct Entry {
  Action      action;
  const char* label;
  const char* value;
};

constexpr Entry kEntries[] = {
  {DETAILS,  "Details",         "details"},
  {CONNECT,  "Connect",         "connect"},
  {MONITOR,  "Packet Monitor",  "monitor"},
  {SNIFFER,  "Packet Sniffer",  "sniffer"},
  {FOX_HUNT, "Fox Hunt",        "foxhunt"},
  {FOX_HUNT_AP, "Fox Hunt (AP)",  "foxhunt_ap"},
  {DEAUTH,   "Deauth/Disassoc", "deauth"},
};
constexpr uint8_t kEntryCount = sizeof(kEntries) / sizeof(kEntries[0]);

}  // namespace

Action choose(uint8_t allowed) {
  InputSelectAction::Option options[kEntryCount];
  uint8_t count = 0;
  const bool bothHunts = (allowed & FOX_HUNT) && (allowed & FOX_HUNT_AP);
  for (const auto& e : kEntries) {
    if (!(allowed & e.action)) continue;
    const char* label = (e.action == FOX_HUNT && bothHunts) ? "Fox Hunt (Client)" : e.label;
    options[count++] = {label, e.value, nullptr, 0, false};
  }
  if (count == 0) return NONE;

  const char* choice = InputSelectAction::popup("Options", options, count);
  if (!choice) return NONE;
  for (const auto& e : kEntries) {
    if (strcmp(choice, e.value) == 0) return e.action;
  }
  return NONE;
}

void open(Action action, const WifiTarget& t) {
  // Filters: an AP target filters by its BSSID; a station filters by its MAC
  // and, when known, by its AP.
  const uint8_t* apFilter     = t.client ? (t.hasAp ? t.bssid : nullptr) : t.mac;
  const uint8_t* clientFilter = t.client ? t.mac : nullptr;
  const char*    ssid         = t.ssid[0] ? t.ssid : nullptr;

  switch (action) {
    case MONITOR: {
      auto* monitor = new WifiPacketMonitorScreen();
      monitor->configureFromChannel(t.channel, apFilter, clientFilter, ssid);
      Screen.push(monitor);
      break;
    }
    case SNIFFER: {
      auto* sniffer = new WifiPacketSnifferScreen();
      sniffer->configureFromChannel(t.channel, apFilter, clientFilter);
      Screen.push(sniffer);
      break;
    }
    case FOX_HUNT: {
      // A station is labelled by its MAC, as when it is picked inside Fox Hunt.
      char label[18];
      snprintf(label, sizeof(label), "%02X:%02X:%02X:%02X:%02X:%02X",
               t.mac[0], t.mac[1], t.mac[2], t.mac[3], t.mac[4], t.mac[5]);
      auto* hunt = new WifiFoxHuntScreen();
      hunt->configureTarget(t.client, t.mac, t.channel, t.client ? label : ssid);
      Screen.push(hunt);
      break;
    }
    case FOX_HUNT_AP: {
      if (!t.hasAp && t.client) break;
      auto* hunt = new WifiFoxHuntScreen();
      hunt->configureTarget(false, t.client ? t.bssid : t.mac, t.channel, ssid);
      Screen.push(hunt);
      break;
    }
    case DEAUTH: {
      auto* deauth = new WifiDeauthDisassocScreen();
      deauth->configureFromChannel(t.channel, apFilter, clientFilter, ssid);
      Screen.push(deauth);
      break;
    }
    case CONNECT: {
      // AP only. Tries a saved password, then asks. Hidden networks have no
      // SSID to join.
      if (!ssid) { ShowStatusAction::show("No SSID", 1500); break; }
      char bssid[18];
      snprintf(bssid, sizeof(bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
               t.mac[0], t.mac[1], t.mac[2], t.mac[3], t.mac[4], t.mac[5]);
      const auto result = WifiUtility::connectWithPrompt(bssid, ssid);
      if (result == WifiUtility::CONNECT_OK) {
        ShowStatusAction::show("Connected", 1500);
        Screen.replace(new NetworkMenuScreen());
      } else if (result == WifiUtility::CONNECT_FAILED)
        ShowStatusAction::show("Connection failed", 1500);
      break;
    }
    default:
      break;
  }
}

void openChannel(Action action, uint8_t channel) {
  if (action == MONITOR) {
    auto* monitor = new WifiPacketMonitorScreen();
    monitor->configureFromChannel(channel, nullptr, nullptr, nullptr);
    Screen.push(monitor);
  } else if (action == SNIFFER) {
    auto* sniffer = new WifiPacketSnifferScreen();
    sniffer->configureFromChannel(channel, nullptr, nullptr);
    Screen.push(sniffer);
  }
}

}  // namespace WifiTargetActions
