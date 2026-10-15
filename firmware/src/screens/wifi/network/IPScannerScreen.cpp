#include "IPScannerScreen.h"
#include <WiFi.h>
#include <lwip/etharp.h>
#include <lwip/ip4_addr.h>
#include <lwip/netif.h>
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/network/NetworkMenuScreen.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"
#include "utils/network/ScanCancelUtil.h"

// ARP cache entries are not guaranteed to survive the complete IP scan.
// Refresh the neighbor entry for the selected host before reading it.
static bool lookupHostMac(const char* ip, uint8_t mac[6]) {
  ip4_addr_t target;
  if (!ip4addr_aton(ip, &target)) return false;

  auto readCache = [&]() -> bool {
    for (int i = 0; i < ARP_TABLE_SIZE; ++i) {
      ip4_addr_t* cachedIp = nullptr;
      struct netif* netif = nullptr;
      struct eth_addr* cachedMac = nullptr;
      if (!etharp_get_entry(i, &cachedIp, &netif, &cachedMac)) continue;
      if (!cachedIp || !cachedMac || !ip4_addr_cmp(cachedIp, &target)) continue;
      memcpy(mac, cachedMac->addr, 6);
      return true;
    }
    return false;
  };

  if (readCache()) return true;

  // Only same-subnet peers can be resolved to their own MAC via ARP.
  // The gateway MAC would be misleading for off-subnet destinations.
  IPAddress local = WiFi.localIP();
  IPAddress mask = WiFi.subnetMask();
  IPAddress remote;
  if (!remote.fromString(ip)) {
    return false;
  }
  for (int i = 0; i < 4; ++i) {
    if ((local[i] & mask[i]) != (remote[i] & mask[i])) return false;
  }

  struct netif* iface = netif_default;
  if (!iface) return false;
  if (etharp_request(iface, &target) != ERR_OK) return false;
  for (int attempt = 0; attempt < 12; ++attempt) {
    delay(25);
    if (readCache()) return true;
  }
  return false;
}

// Small built-in fallback table. Unknown OUIs are deliberately not guessed.
// A complete manufacturer database can be added independently later.
static const char* vendorForMac(const uint8_t mac[6]) {
  if (mac[0] & 0x02) return "Private / randomized";
  const uint32_t oui = ((uint32_t)mac[0] << 16) | ((uint32_t)mac[1] << 8) | mac[2];
  switch (oui) {
    case 0x00163E: return "Xensource";
    case 0x001A11: return "Google";
    case 0x001B63: return "Apple";
    case 0x001C42: return "Parallels";
    case 0x002248: return "Microsoft";
    case 0x0024E8: return "Dell";
    case 0x3C5A37: return "Samsung";
    case 0x3C22FB: return "Apple";
    case 0xB827EB: return "Raspberry Pi";
    case 0xDC4A3E: return "Raspberry Pi";
    case 0xF4F5D8: return "Google";
    default: return "Unknown";
  }
}

// ── screen methods ─────────────────────────────────────

IPScannerScreen::IPScannerScreen() {
  memset(_foundIPs,    0, sizeof(_foundIPs));
  memset(_foundItems,  0, sizeof(_foundItems));
  memset(_openPorts,   0, sizeof(_openPorts));
  memset(_openItems,   0, sizeof(_openItems));
  memset(_configItems, 0, sizeof(_configItems));
}

void IPScannerScreen::onInit() {
  _showConfiguration();
}

void IPScannerScreen::onBack() {
  switch (_state) {
    case STATE_RESULT_PORT:
    case STATE_DETAILS:
      _state = STATE_RESULT_IP;
      setPreferSublabel(true);
      setItems(_foundItems, (_foundCount == 0) ? 1 : _foundCount, _selectedHost);
      break;
    case STATE_RESULT_IP:
      _showConfiguration();
      break;
    default:
      Screen.goBack();
      break;
  }
}

void IPScannerScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_CONFIGURATION) {
    switch (index) {
      case 0: {
        int value = InputNumberAction::popup("Start IP", 1, _endIp, _startIp);
        if (!InputNumberAction::wasCancelled()) _startIp = value;
        _showConfiguration(index);
        break;
      }
      case 1: {
        int value = InputNumberAction::popup("End IP", _startIp, 254, _endIp);
        if (!InputNumberAction::wasCancelled()) _endIp = value;
        _showConfiguration(index);
        break;
      }
      case 2:
        _resolveName = !_resolveName;
        _showConfiguration(index);
        break;
      case 3:
        _scanIP();
        break;
    }
  } else if (_state == STATE_RESULT_IP) {
    if (_foundCount == 0 || _foundIPs[index].ip[0] == '\0') {
      _showConfiguration();
    } else {
      _selectedHost = index;
      static const InputSelectAction::Option options[] = {
        {"Details", "details"},
        {"Port Scan", "ports"},
      };
      const char* choice = InputSelectAction::popup("Options", options, 2);
      if (choice && strcmp(choice, "details") == 0) _showDetails(index);
      else if (choice && strcmp(choice, "ports") == 0) _scanPort(_foundIPs[index].ip);
    }
  }
}

// ── private ────────────────────────────────────────────

void IPScannerScreen::_showDetails(uint8_t index) {
  if (index >= _foundCount) return;
  _selectedHost = index;
  _state = STATE_DETAILS;
  setPreferSublabel(true);
  _detailItems[0] = {"IP Address", _foundIPs[index].ip};
  _detailItems[1] = {"Hostname", _foundIPs[index].hostname[0] ? _foundIPs[index].hostname : "Unknown"};
  uint8_t mac[6];
  if (lookupHostMac(_foundIPs[index].ip, mac)) {
    char macText[18];
    snprintf(macText, sizeof(macText), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    _detailMac = macText;
    _detailVendor = vendorForMac(mac);
  } else {
    _detailMac = "Unavailable";
    _detailVendor = "Unknown";
  }
  _detailItems[2] = {"MAC Address", _detailMac.c_str()};
  _detailItems[3] = {"Vendor", _detailVendor.c_str()};
  setItems(_detailItems, 4);
}


void IPScannerScreen::_showConfiguration(uint8_t selectedIndex) {
  _state = STATE_CONFIGURATION;
  setPreferSublabel(false);
  _startIpSub = String(_startIp);
  _endIpSub   = String(_endIp);
  _configItems[0] = {"Start IP", _startIpSub.c_str()};
  _configItems[1] = {"End IP", _endIpSub.c_str()};
  _configItems[2] = {"Resolve Name", _resolveName ? "On" : "Off"};
  _configItems[3] = {"Start Scan"};
  setItems(_configItems, 4, selectedIndex);
}

void IPScannerScreen::_scanIP() {
  if (WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    ShowStatusAction::show("Not connected");
    return;
  }

  _state = STATE_SCANNING_IP;
  memset(_foundIPs,   0, sizeof(_foundIPs));
  memset(_foundItems, 0, sizeof(_foundItems));
  _foundCount = 0;

  int nip = Achievement.inc("wifi_ip_scan_started");
  if (nip == 1) Achievement.unlock("wifi_ip_scan_started");

  ScanCancelUtil::begin();
  ProgressView::init();

  _foundCount = IpScanUtil::scan(
    (uint8_t)_startIp, (uint8_t)_endIp,
    _foundIPs, MAX_FOUND, _resolveName,
    [](uint8_t pct) { ProgressView::progress("Scanning...", pct); },
    []() { return ScanCancelUtil::poll(); }
  );

  ProgressView::finish();

  if (ScanCancelUtil::wasCancelled()) {
    _showConfiguration();
    return;
  }

  if (_foundCount == 0) {
    _foundItems[0] = {"No devices found"};
    _state = STATE_RESULT_IP;
    setPreferSublabel(true);
    setItems(_foundItems, 1);
    return;
  }

  int nh = Achievement.inc("wifi_ip_host_found");
  if (nh == 1) Achievement.unlock("wifi_ip_host_found");

  for (uint8_t i = 0; i < _foundCount; i++) {
    _foundItems[i] = {_foundIPs[i].ip, _foundIPs[i].hostname};
  }

  _state = STATE_RESULT_IP;
  setPreferSublabel(true);
  setItems(_foundItems, _foundCount);
}

void IPScannerScreen::_scanPort(const char* ip) {
  _state = STATE_SCANNING_PORT;

  memset(_openPorts, 0, sizeof(_openPorts));
  memset(_openItems, 0, sizeof(_openItems));

  int nps = Achievement.inc("wifi_port_scan_started");
  if (nps == 1) Achievement.unlock("wifi_port_scan_started");

  ScanCancelUtil::begin();
  ProgressView::init();
  String scanLabel = String("Scanning ") + ip + "...";
  _openCount = PortScanUtil::scan(ip, _openPorts, PortScanUtil::MAX_RESULTS, scanLabel.c_str(), false);
  ProgressView::finish();

  if (ScanCancelUtil::wasCancelled()) {
    _state = STATE_RESULT_IP;
    setPreferSublabel(true);
    setItems(_foundItems, _foundCount, _selectedHost);
    return;
  }

  if (_openCount == 0) {
    _openItems[0] = {"No ports open"};
    _state = STATE_RESULT_PORT;
    setPreferSublabel(false);
    setItems(_openItems, 1);
    return;
  }

  int npo = Achievement.inc("wifi_port_open_found");
  if (npo == 1) Achievement.unlock("wifi_port_open_found");

  for (uint8_t i = 0; i < _openCount; i++) {
    _openItems[i] = {_openPorts[i].label, _openPorts[i].service};
  }

  _state = STATE_RESULT_PORT;
  setPreferSublabel(false);
  setItems(_openItems, _openCount);
}
