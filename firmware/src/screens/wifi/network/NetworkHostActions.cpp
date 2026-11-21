//
// See NetworkHostActions.h.
//
#include "NetworkHostActions.h"

#include "IPScannerDetailsScreen.h"
#include "PortScannerScreen.h"
#include "screens/wifi/WifiFoxHuntScreen.h"
#include "screens/wifi/network/NetworkMitmScreen.h"
#include "screens/wifi/network/toolbox/PingScreen.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowStatusAction.h"

#include <WiFi.h>
#include <lwip/etharp.h>
#include <lwip/ip4_addr.h>
#include <lwip/netif.h>

namespace {

void fmtIp(uint32_t ip, char* out, size_t n) {
  snprintf(out, n, "%u.%u.%u.%u",
           (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
           (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
}

// True when `ipNet` shares the local subnet (so ARP/MITM/NetCut/Fox Hunt can
// reach it).
bool onSubnet(uint32_t ipNet) {
  IPAddress local = WiFi.localIP();
  IPAddress mask  = WiFi.subnetMask();
  // No usable IPv4 (e.g. IPv6-only network, or before a DHCPv4 lease): a zero
  // mask would make every address compare equal and look on-subnet, so treat
  // "no IPv4" as nothing being reachable on the local subnet.
  if ((uint32_t)local == 0 || (uint32_t)mask == 0) return false;
  IPAddress remote(ipNet);
  for (int i = 0; i < 4; ++i)
    if ((local[i] & mask[i]) != (remote[i] & mask[i])) return false;
  return true;
}

}  // namespace

bool NetworkHostActions::lookupHostMac(const char* ip, uint8_t mac[6], bool probe) {
  ip4_addr_t target;
  if (!ip4addr_aton(ip, &target)) return false;

  auto readCache = [&]() -> bool {
    for (int i = 0; i < ARP_TABLE_SIZE; ++i) {
      ip4_addr_t*      cachedIp  = nullptr;
      struct netif*    netif     = nullptr;
      struct eth_addr* cachedMac = nullptr;
      if (!etharp_get_entry(i, &cachedIp, &netif, &cachedMac)) continue;
      if (!cachedIp || !cachedMac || !ip4_addr_cmp(cachedIp, &target)) continue;
      memcpy(mac, cachedMac->addr, 6);
      return true;
    }
    return false;
  };

  if (readCache()) return true;
  if (!probe) return false;

  // Only same-subnet peers resolve to their own MAC; the gateway MAC would be
  // misleading for off-subnet destinations.
  IPAddress local = WiFi.localIP();
  IPAddress mask  = WiFi.subnetMask();
  if ((uint32_t)local == 0 || (uint32_t)mask == 0) return false;  // no IPv4
  IPAddress remote;
  if (!remote.fromString(ip)) return false;
  for (int i = 0; i < 4; ++i)
    if ((local[i] & mask[i]) != (remote[i] & mask[i])) return false;

  struct netif* iface = netif_default;
  if (!iface) return false;
  if (etharp_request(iface, &target) != ERR_OK) return false;
  for (int attempt = 0; attempt < 40; ++attempt) {
    delay(25);
    if (readCache()) return true;
  }
  return false;
}

bool NetworkHostActions::openOptions(const HostTarget& t,
                                     const std::function<void()>& onLeave) {
  char ip[16];
  fmtIp(t.ip, ip, sizeof(ip));

  const uint32_t gw      = (uint32_t)WiFi.gatewayIP();
  const bool     sub     = onSubnet(t.ip);
  const uint8_t  channel = WiFi.channel();
  const bool     chOk    = (channel >= 1 && channel <= 14);
  const bool     poison  = sub && gw && t.ip != gw;   // MITM/NetCut target valid

  // MAC known without blocking (stored, or already in the ARP cache). Used for
  // Details; Fox Hunt resolves it with a probe when picked.
  uint8_t mac[6] = {};
  bool macKnown = false;
  if (t.hasMac) { memcpy(mac, t.mac, 6); macKnown = true; }
  else          { macKnown = lookupHostMac(ip, mac, /*probe=*/false); }

  // Curate: only the actions that make sense for this target.
  InputSelectAction::Option options[6];
  uint8_t n = 0;
  options[n++] = {"Details", "details"};        // always
  options[n++] = {"Ping", "ping"};              // always; starts immediately
  if (sub)         options[n++] = {"Port Scan", "ports"};
  if (poison)      options[n++] = {"Sniff", "sniff"};
  if (poison)      options[n++] = {"NetCut", "netcut"};
  if (sub && chOk) options[n++] = {"Fox Hunt", "fox"};

  const char* choice = InputSelectAction::popup("Options", options, n);
  if (!choice) return false;

  auto leave = [&] { if (onLeave) onLeave(); };

  if (strcmp(choice, "details") == 0) {
    leave();
    auto* d = new IPScannerDetailsScreen();
    d->configure(ip, t.name, macKnown ? mac : nullptr);
    Screen.push(d);
    return true;
  }

  if (strcmp(choice, "ping") == 0) {
    leave();
    auto* p = new PingScreen();
    p->configure(ip);
    Screen.push(p);
    return true;
  }

  if (strcmp(choice, "ports") == 0) {
    leave();
    auto* s = new PortScannerScreen();
    s->configureTarget(ip);
    Screen.push(s);
    return true;
  }

  if (strcmp(choice, "sniff") == 0) {
    if (!gw)        { ShowStatusAction::show("No gateway"); return false; }
    if (t.ip == gw) { ShowStatusAction::show("Host is the gateway"); return false; }
    leave();
    auto* m = new NetworkMitmScreen();
    m->configureSniff(t.ip, gw);
    Screen.push(m);
    return true;
  }

  if (strcmp(choice, "netcut") == 0) {
    if (!gw)        { ShowStatusAction::show("No gateway"); return false; }
    if (t.ip == gw) { ShowStatusAction::show("Host is the gateway"); return false; }
    leave();
    auto* c = new NetworkMitmScreen();
    c->configureCut(t.ip);
    Screen.push(c);
    return true;
  }

  if (strcmp(choice, "fox") == 0) {
    // Resolve the MAC now (probe if we don't already have it).
    if (!macKnown) macKnown = lookupHostMac(ip, mac, /*probe=*/true);
    if (!macKnown)  { ShowStatusAction::show("MAC unavailable"); return false; }
    if (!chOk)      { ShowStatusAction::show("Channel unavailable"); return false; }
    leave();
    auto* hunt = new WifiFoxHuntScreen();
    const uint8_t* ap = WiFi.BSSID();
    const bool validAp = ap && memcmp(ap, "\0\0\0\0\0\0", 6) != 0;
    // IP discovery cannot tell an AP from a client. The gateway's ARP MAC can be
    // the router's LAN interface MAC, which differs from its BSSID; for the
    // gateway (or a host whose MAC matches the associated AP) prefer the BSSID.
    const bool isGateway = (gw && t.ip == gw);
    if (validAp && (isGateway || memcmp(mac, ap, 6) == 0))
      hunt->configureTarget(false, ap, channel, t.name);
    else
      hunt->configureTransmitterTarget(mac, channel, t.name);
    Screen.push(hunt);
    return true;
  }

  return false;
}
