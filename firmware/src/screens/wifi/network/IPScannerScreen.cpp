#include "IPScannerScreen.h"
#include <WiFi.h>
#include <lwip/etharp.h>
#include <lwip/netif.h>
#include "utils/network/DnsUtil.h"
#include "core/ScreenManager.h"
#include "screens/wifi/network/NetworkMenuScreen.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/ShowProgressAction.h"

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
      _state = STATE_RESULT_IP;
      setItems(_foundItems, (_foundCount == 0) ? 1 : _foundCount);
      break;
    case STATE_RESULT_IP:
      _showConfiguration();
      break;
    default:
      Screen.setScreen(new NetworkMenuScreen());
      break;
  }
}

void IPScannerScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_CONFIGURATION) {
    switch (index) {
      case 0:
        _startIp = InputNumberAction::popup("Start IP", 1, _endIp, _startIp);
        _showConfiguration();
        break;
      case 1:
        _endIp = InputNumberAction::popup("End IP", _startIp, 254, _endIp);
        _showConfiguration();
        break;
      case 2:
        _scanIP();
        break;
    }
  } else if (_state == STATE_RESULT_IP) {
    if (_foundCount == 0 || _foundIPs[index].ip[0] == '\0') {
      _showConfiguration();
    } else {
      _scanPort(_foundIPs[index].ip);
    }
  }
}

// ── private ────────────────────────────────────────────

void IPScannerScreen::_showConfiguration() {
  _state = STATE_CONFIGURATION;
  _startIpSub = String(_startIp);
  _endIpSub   = String(_endIp);
  _configItems[0] = {"Start IP", _startIpSub.c_str()};
  _configItems[1] = {"End IP",   _endIpSub.c_str()};
  _configItems[2] = {"Start Scan"};
  setItems(_configItems, 3);
}

void IPScannerScreen::_scanIP() {
  _state = STATE_SCANNING_IP;

  memset(_foundIPs,   0, sizeof(_foundIPs));
  memset(_foundItems, 0, sizeof(_foundItems));
  _foundCount = 0;

  IPAddress localIP = WiFi.localIP();
  if (localIP[0] == 0 && localIP[1] == 0 && localIP[2] == 0 && localIP[3] == 0) {
    _foundItems[0] = {"No devices found"};
    _state = STATE_RESULT_IP;
    setItems(_foundItems, 1);
    return;
  }

  // Find the WiFi STA netif (first ETHARP-capable, UP interface)
  struct netif* netif = nullptr;
  for (struct netif* ni = netif_list; ni != nullptr; ni = ni->next) {
    if ((ni->flags & NETIF_FLAG_UP) && (ni->flags & NETIF_FLAG_ETHARP)) {
      netif = ni;
      break;
    }
  }
  if (!netif) {
    _foundItems[0] = {"ARP unavailable"};
    _state = STATE_RESULT_IP;
    setItems(_foundItems, 1);
    return;
  }

  char baseIp[16];
  snprintf(baseIp, sizeof(baseIp), "%d.%d.%d.", localIP[0], localIP[1], localIP[2]);

  int total    = _endIp - _startIp + 1;
  uint8_t lastPct = 255;

  // Phase 1: blast ARP requests (0–60%)
  for (int i = _startIp; i <= _endIp; i++) {
    uint8_t pct = (uint8_t)((i - _startIp) * 60 / total);
    if (pct != lastPct) {
      ShowProgressAction::show("ARP scanning...", pct);
      lastPct = pct;
    }

    char ipStr[16];
    snprintf(ipStr, sizeof(ipStr), "%s%d", baseIp, i);
    ip4_addr_t target;
    ipaddr_aton(ipStr, (ip_addr_t*)&target);
    etharp_request(netif, &target);
    delay(10);
  }

  // Brief wait for late ARP replies
  ShowProgressAction::show("ARP scanning...", 62);
  delay(300);

  // Phase 2: read ARP table + resolve hostnames (65–100%)
  lastPct = 255;
  for (int i = _startIp; i <= _endIp && _foundCount < MAX_FOUND; i++) {
    uint8_t pct = 65 + (uint8_t)((i - _startIp) * 35 / total);
    if (pct != lastPct) {
      ShowProgressAction::show("Resolving...", pct);
      lastPct = pct;
    }

    if (i == localIP[3]) continue;

    char ipStr[16];
    snprintf(ipStr, sizeof(ipStr), "%s%d", baseIp, i);
    ip4_addr_t target;
    ipaddr_aton(ipStr, (ip_addr_t*)&target);

    struct eth_addr*     eth_ret  = nullptr;
    const ip4_addr_t*    ip_ret   = nullptr;
    if (etharp_find_addr(nullptr, &target, &eth_ret, &ip_ret) >= 0) {
      strncpy(_foundIPs[_foundCount].ip, ipStr, sizeof(_foundIPs[0].ip) - 1);

      if (!DnsUtil::resolveHostname(ipStr, _foundIPs[_foundCount].hostname,
                                    sizeof(_foundIPs[0].hostname)) && eth_ret) {
        // Fallback: show MAC address
        snprintf(_foundIPs[_foundCount].hostname, sizeof(_foundIPs[0].hostname),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 eth_ret->addr[0], eth_ret->addr[1], eth_ret->addr[2],
                 eth_ret->addr[3], eth_ret->addr[4], eth_ret->addr[5]);
      }

      _foundItems[_foundCount] = {_foundIPs[_foundCount].ip, _foundIPs[_foundCount].hostname};
      _foundCount++;
    }
  }

  ShowProgressAction::show("ARP scanning...", 100);

  if (_foundCount == 0) {
    _foundItems[0] = {"No devices found"};
    _state = STATE_RESULT_IP;
    setItems(_foundItems, 1);
    return;
  }

  _state = STATE_RESULT_IP;
  setItems(_foundItems, _foundCount);
}

void IPScannerScreen::_scanPort(const char* ip) {
  _state = STATE_SCANNING_PORT;

  memset(_openPorts, 0, sizeof(_openPorts));
  memset(_openItems, 0, sizeof(_openItems));

  _openCount = PortScanUtil::scan(ip, _openPorts, PortScanUtil::MAX_RESULTS);

  if (_openCount == 0) {
    _openItems[0] = {"No ports open"};
    _state = STATE_RESULT_PORT;
    setItems(_openItems, 1);
    return;
  }

  for (uint8_t i = 0; i < _openCount; i++) {
    _openItems[i] = {_openPorts[i].label, _openPorts[i].service};
  }

  _state = STATE_RESULT_PORT;
  setItems(_openItems, _openCount);
}
