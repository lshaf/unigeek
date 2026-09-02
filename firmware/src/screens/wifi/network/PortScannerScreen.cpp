#include "PortScannerScreen.h"
#include "utils/network/TargetResolveUtil.h"
#include <WiFi.h>
#include <ctype.h>
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/network/NetworkMenuScreen.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"
#include "utils/network/ScanCancelUtil.h"

void PortScannerScreen::onInit() {
  memset(_hosts,       0, sizeof(_hosts));
  memset(_results,     0, sizeof(_results));
  memset(_resultItems, 0, sizeof(_resultItems));
  memset(_configItems, 0, sizeof(_configItems));
  _showInput();
}

void PortScannerScreen::onBack() {
  if (_state == STATE_RESULTS) {
    _showInput();
  } else if (_state != STATE_SCANNING) {
    Screen.goBack();
  }
}

void PortScannerScreen::onItemSelected(uint8_t index) {
  if (_state != STATE_INPUT || index >= _configCount) return;

  switch (_configActions[index]) {
    case CFG_MODE:
      _scanMode = (_scanMode == MODE_RANGE) ? MODE_TARGETS : MODE_RANGE;
      _showInput(index);
      break;

    case CFG_TARGET_1:
      _editTarget(0, index);
      break;
    case CFG_TARGET_2:
      _editTarget(1, index);
      break;
    case CFG_TARGET_3:
      _editTarget(2, index);
      break;
    case CFG_TARGET_4:
      _editTarget(3, index);
      break;

    case CFG_START_IP: {
      int value = InputNumberAction::popup("Start IP", 1, _endIp, _startIp);
      if (!InputNumberAction::wasCancelled()) _startIp = value;
      _showInput(index);
      break;
    }

    case CFG_END_IP: {
      int value = InputNumberAction::popup("End IP", _startIp, 254, _endIp);
      if (!InputNumberAction::wasCancelled()) _endIp = value;
      _showInput(index);
      break;
    }

    case CFG_PORTS: {
      static const InputSelectAction::Option options[] = {
        {"Common", "common"},
        {"Custom", "custom"},
        {"All",    "all"},
      };
      const char* current = _portMode == PORTS_COMMON ? "common" :
                            _portMode == PORTS_CUSTOM ? "custom" : "all";
      const char* choice = InputSelectAction::popup("Ports", options, 3, current);
      if (choice) {
        if (strcmp(choice, "custom") == 0) _portMode = PORTS_CUSTOM;
        else if (strcmp(choice, "all") == 0) _portMode = PORTS_ALL;
        else _portMode = PORTS_COMMON;
      }
      _showInput(index);
      break;
    }

    case CFG_CUSTOM_PORTS: {
      String ports = InputNumberAction::popupList("Custom Ports", _customPorts.c_str());
      if (!InputNumberAction::wasCancelled()) _customPorts = ports;
      _showInput(index);
      break;
    }


    case CFG_START_SCAN:
      _scan();
      break;
  }
}

// ── private ──────────────────────────────────────────────────────────────────

void PortScannerScreen::_showInput(uint8_t selectedIndex) {
  _state = STATE_INPUT;
  _configCount = 0;

  auto add = [&](const char* label, const char* sub, ConfigAction action) {
    _configItems[_configCount] = {label, sub};
    _configActions[_configCount] = action;
    _configCount++;
  };

  add("Mode", _scanMode == MODE_RANGE ? "Range" : "Targets", CFG_MODE);

  if (_scanMode == MODE_TARGETS) {
    static const ConfigAction targetActions[MAX_TARGETS] = {
      CFG_TARGET_1, CFG_TARGET_2, CFG_TARGET_3, CFG_TARGET_4
    };
    static const char* targetLabels[MAX_TARGETS] = {
      "Target 1", "Target 2", "Target 3", "Target 4"
    };

    for (uint8_t i = 0; i < MAX_TARGETS; ++i) {
      _targetSubs[i] = _targets[i].length() > 0 ? _targets[i] : "-";
      add(targetLabels[i], _targetSubs[i].c_str(), targetActions[i]);
    }
  } else {
    _startIpSub = String(_startIp);
    _endIpSub   = String(_endIp);
    add("Start IP", _startIpSub.c_str(), CFG_START_IP);
    add("End IP",   _endIpSub.c_str(),   CFG_END_IP);
  }

  const char* portModeLabel = _portMode == PORTS_COMMON ? "Common" :
                              _portMode == PORTS_CUSTOM ? "Custom" : "All";
  add("Ports", portModeLabel, CFG_PORTS);

  if (_portMode == PORTS_CUSTOM) {
    _customPortsSub = _customPorts.length() > 0 ? _customPorts : "-";
    add("Custom Ports", _customPortsSub.c_str(), CFG_CUSTOM_PORTS);
  }

  add("Start Scan", nullptr, CFG_START_SCAN);
  setItems(_configItems, _configCount, selectedIndex);
}

void PortScannerScreen::_scan() {
  if (WiFi.localIP()[0] == 0 && WiFi.localIP()[1] == 0 &&
      WiFi.localIP()[2] == 0 && WiFi.localIP()[3] == 0) {
    ShowStatusAction::show("WiFi not connected");
    return;
  }

  if (_scanMode == MODE_TARGETS) {
    if (!_hasTargets()) {
      ShowStatusAction::show("Enter at least one target");
      return;
    }

    for (uint8_t i = 0; i < MAX_TARGETS; ++i) {
      if (_targets[i].length() > 0 && !TargetResolveUtil::isValidTarget(_targets[i])) {
        ShowStatusAction::show("Invalid target");
        return;
      }
    }
  }

  if (_portMode == PORTS_CUSTOM) {
    uint16_t ports[PortScanUtil::MAX_CUSTOM_PORTS];
    uint8_t count = 0;
    if (!_parseCustomPorts(ports, count)) {
      ShowStatusAction::show("Invalid custom ports");
      return;
    }
  }

  _state = STATE_SCANNING;
  memset(_hosts,       0, sizeof(_hosts));
  memset(_results,     0, sizeof(_results));
  memset(_resultItems, 0, sizeof(_resultItems));
  _resultCount = 0;

  int nps = Achievement.inc("wifi_port_scan_started");
  if (nps == 1) Achievement.unlock("wifi_port_scan_started");

  ScanCancelUtil::begin();
  ProgressView::init();

  if (_scanMode == MODE_TARGETS) {
    uint8_t targetCount = 0;
    for (uint8_t i = 0; i < MAX_TARGETS; ++i) {
      if (_targets[i].length() > 0) targetCount++;
    }

    uint8_t done = 0;
    for (uint8_t i = 0;
         i < MAX_TARGETS && _resultCount < PortScanUtil::MAX_RESULTS;
         ++i) {
      if (_targets[i].length() == 0) continue;
      if (ScanCancelUtil::poll()) break;

      char label[48];
      snprintf(
        label, sizeof(label),
        "Scanning %s (%u/%u)...",
        _targets[i].c_str(),
        (unsigned)(done + 1),
        (unsigned)targetCount
      );
      ProgressView::progress(label, 0);
      String resolved;
      if (!TargetResolveUtil::resolve(_targets[i], resolved)) {
        ShowStatusAction::show("Could not resolve target", 900);
        done++;
        continue;
      }
      _scanTarget(resolved.c_str(), _resultCount, label);
      done++;
      ProgressView::progress(
        label,
        targetCount ? (uint8_t)((uint16_t)done * 100 / targetCount) : 100
      );
    }
  } else {
    uint8_t hostCount = IpScanUtil::scan(
      (uint8_t)_startIp, (uint8_t)_endIp,
      _hosts, MAX_FOUND_HOSTS, false,
      [](uint8_t pct) { ProgressView::progress("Scanning hosts...", pct); },
    []() { return ScanCancelUtil::poll(); }
  );

    for (uint8_t i = 0; i < hostCount && _resultCount < PortScanUtil::MAX_RESULTS; i++) {
      if (ScanCancelUtil::poll()) break;
      char label[48];
      snprintf(
        label, sizeof(label),
        "Scanning %s (%u/%u)...",
        _hosts[i].ip,
        (unsigned)(i + 1),
        (unsigned)hostCount
      );
      ProgressView::progress(label, 0);
      _scanTarget(_hosts[i].ip, _resultCount, label);
    }
  }

  ProgressView::finish();

  if (ScanCancelUtil::wasCancelled()) {
    _showInput();
    return;
  }

  if (_resultCount == 0) {
    _resultItems[0] = {"No ports open"};
    _state = STATE_RESULTS;
    setItems(_resultItems, 1);
    return;
  }

  int npo = Achievement.inc("wifi_port_open_found");
  if (npo == 1) Achievement.unlock("wifi_port_open_found");

  for (uint8_t i = 0; i < _resultCount; i++)
    _resultItems[i] = {_results[i].label, _results[i].service};

  _state = STATE_RESULTS;
  setItems(_resultItems, _resultCount);
}

bool PortScannerScreen::_validIp(const String& ip) const {
  int a, b, c, d;
  char tail;

  return sscanf(ip.c_str(), "%d.%d.%d.%d%c", &a, &b, &c, &d, &tail) == 4 &&
         a >= 0 && a <= 255 &&
         b >= 0 && b <= 255 &&
         c >= 0 && c <= 255 &&
         d >= 0 && d <= 255;
}

bool PortScannerScreen::_hasTargets() const {
  for (uint8_t i = 0; i < MAX_TARGETS; ++i) {
    if (_targets[i].length() > 0) return true;
  }
  return false;
}

void PortScannerScreen::_editTarget(uint8_t targetIndex, uint8_t selectedIndex) {
  if (targetIndex >= MAX_TARGETS) return;

  String initial = _targets[targetIndex].length() > 0
    ? _targets[targetIndex]
    : _networkPrefix();

  char title[16];
  snprintf(title, sizeof(title), "Target %u", (unsigned)(targetIndex + 1));

  String ip = InputTextAction::popup(
    title,
    initial.c_str(),
    InputTextAction::INPUT_IP_ADDRESS
  );

  if (!InputTextAction::wasCancelled()) {
    _targets[targetIndex] = ip;
  }

  _showInput(selectedIndex);
}

bool PortScannerScreen::_scanTarget(const char* ip, uint8_t& count, const char* progressLabel) {
  if (!ip || count >= PortScanUtil::MAX_RESULTS) return false;

  uint8_t remaining = PortScanUtil::MAX_RESULTS - count;
  uint8_t found = 0;
  const bool patient = _scanMode == MODE_TARGETS;

  if (_portMode == PORTS_COMMON) {
    found = PortScanUtil::scan(ip, &_results[count], remaining, progressLabel, false, patient);
  } else if (_portMode == PORTS_ALL) {
    found = PortScanUtil::scanRange(
      ip, 1, 65535,
      &_results[count], remaining, progressLabel, false, patient
    );
  } else {
    uint16_t ports[PortScanUtil::MAX_CUSTOM_PORTS];
    uint8_t portCount = 0;
    if (!_parseCustomPorts(ports, portCount)) return false;
    found = PortScanUtil::scanPorts(
      ip, ports, portCount,
      &_results[count], remaining, progressLabel, false, patient
    );
  }

  count += found;
  return true;
}

bool PortScannerScreen::_parseCustomPorts(uint16_t out[], uint8_t& count) {
  count = 0;
  if (!out) return false;

  String src = _customPorts;
  src.trim();
  if (src.length() == 0) return false;

  int pos = 0;
  while (pos < (int)src.length()) {
    while (pos < (int)src.length() && isspace((unsigned char)src[pos])) pos++;
    if (pos >= (int)src.length()) break;

    int start = pos;
    while (pos < (int)src.length() && !isspace((unsigned char)src[pos])) pos++;
    String token = src.substring(start, pos);
    for (size_t i = 0; i < token.length(); i++) {
      if (!isdigit((unsigned char)token[i])) return false;
    }

    long value = token.toInt();
    if (value < 1 || value > 65535) return false;

    bool duplicate = false;
    for (uint8_t i = 0; i < count; i++) {
      if (out[i] == (uint16_t)value) { duplicate = true; break; }
    }
    if (!duplicate) {
      if (count >= PortScanUtil::MAX_CUSTOM_PORTS) return false;
      out[count++] = (uint16_t)value;
    }
  }

  return count > 0;
}

String PortScannerScreen::_networkPrefix() const {
  IPAddress ip = WiFi.localIP();
  if (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0) return "";
  char prefix[16];
  snprintf(prefix, sizeof(prefix), "%u.%u.%u.", ip[0], ip[1], ip[2]);
  return String(prefix);
}
