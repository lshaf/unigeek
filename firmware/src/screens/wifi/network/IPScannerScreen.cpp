#include "IPScannerScreen.h"
#include <WiFi.h>
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/network/NetworkMenuScreen.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/views/ProgressView.h"

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
      Screen.goBack();
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

  if (WiFi.localIP()[0] == 0) {
    _foundItems[0] = {"No devices found"};
    _state = STATE_RESULT_IP;
    setItems(_foundItems, 1);
    return;
  }

  int nip = Achievement.inc("wifi_ip_scan_started");
  if (nip == 1) Achievement.unlock("wifi_ip_scan_started");

  ProgressView::init();
  _foundCount = IpScanUtil::scan(
    (uint8_t)_startIp, (uint8_t)_endIp,
    _foundIPs, MAX_FOUND, true,
    [](uint8_t pct) { ProgressView::progress("IP scanning...", pct); }
  );

  if (_foundCount == 0) {
    _foundItems[0] = {"No devices found"};
    _state = STATE_RESULT_IP;
    setItems(_foundItems, 1);
    return;
  }

  int nh = Achievement.inc("wifi_ip_host_found");
  if (nh == 1) Achievement.unlock("wifi_ip_host_found");

  for (uint8_t i = 0; i < _foundCount; i++)
    _foundItems[i] = {_foundIPs[i].ip, _foundIPs[i].hostname};

  _state = STATE_RESULT_IP;
  setItems(_foundItems, _foundCount);
}

void IPScannerScreen::_scanPort(const char* ip) {
  _state = STATE_SCANNING_PORT;

  memset(_openPorts, 0, sizeof(_openPorts));
  memset(_openItems, 0, sizeof(_openItems));

  int nps = Achievement.inc("wifi_port_scan_started");
  if (nps == 1) Achievement.unlock("wifi_port_scan_started");

  ProgressView::init();
  _openCount = PortScanUtil::scan(ip, _openPorts, PortScanUtil::MAX_RESULTS);

  if (_openCount == 0) {
    _openItems[0] = {"No ports open"};
    _state = STATE_RESULT_PORT;
    setItems(_openItems, 1);
    return;
  }

  int npo = Achievement.inc("wifi_port_open_found");
  if (npo == 1) Achievement.unlock("wifi_port_open_found");

  for (uint8_t i = 0; i < _openCount; i++) {
    _openItems[i] = {_openPorts[i].label, _openPorts[i].service};
  }

  _state = STATE_RESULT_PORT;
  setItems(_openItems, _openCount);
}
