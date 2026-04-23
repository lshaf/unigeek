#include "PortScannerScreen.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/network/NetworkMenuScreen.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"

void PortScannerScreen::onInit() {
  memset(_results,     0, sizeof(_results));
  memset(_resultItems, 0, sizeof(_resultItems));
  memset(_configItems, 0, sizeof(_configItems));
  _showInput();
}

void PortScannerScreen::onBack() {
  if (_state == STATE_RESULTS) {
    _showInput();
  } else {
    Screen.goBack();
  }
}

void PortScannerScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_INPUT) {
    switch (index) {
      case 0: {
        String ip = InputTextAction::popup("Target IP", _targetIp.c_str(), InputTextAction::INPUT_IP_ADDRESS);
        if (!InputTextAction::wasCancelled()) _targetIp = ip;
        _showInput();
        break;
      }
      case 1:
        _scan();
        break;
    }
  }
}

// ── private ────────────────────────────────────────────

void PortScannerScreen::_showInput() {
  _state = STATE_INPUT;
  _targetIpSub = _targetIp.length() > 0 ? _targetIp : "-";
  _configItems[0] = {"Target IP", _targetIpSub.c_str()};
  _configItems[1] = {"Start Scan"};
  setItems(_configItems, 2);
}

void PortScannerScreen::_scan() {
  if (_targetIp.length() == 0) {
    ShowStatusAction::show("Enter target IP first");
    return;
  }

  int a, b, c, d;
  if (sscanf(_targetIp.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d) != 4 ||
      a < 0 || a > 255 || b < 0 || b > 255 ||
      c < 0 || c > 255 || d < 0 || d > 255) {
    ShowStatusAction::show("Invalid IP address");
    return;
  }

  memset(_results,     0, sizeof(_results));
  memset(_resultItems, 0, sizeof(_resultItems));

  int nps = Achievement.inc("wifi_port_scan_started");
  if (nps == 1) Achievement.unlock("wifi_port_scan_started");

  ProgressView::init();
  _resultCount = PortScanUtil::scan(_targetIp.c_str(), _results, PortScanUtil::MAX_RESULTS);

  if (_resultCount == 0) {
    _resultItems[0] = {"No ports open"};
    _state = STATE_RESULTS;
    setItems(_resultItems, 1);
    return;
  }

  int npo = Achievement.inc("wifi_port_open_found");
  if (npo == 1) Achievement.unlock("wifi_port_open_found");

  for (uint8_t i = 0; i < _resultCount; i++) {
    _resultItems[i] = {_results[i].label, _results[i].service};
  }
  _state = STATE_RESULTS;
  setItems(_resultItems, _resultCount);
}
