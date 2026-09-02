#include "WebServerScannerScreen.h"
#include "utils/network/TargetResolveUtil.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <stdio.h>
#include <string.h>
#include "core/ScreenManager.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"
#include "utils/network/ScanCancelUtil.h"

namespace {
struct WebPort {
  uint16_t port;
  bool https;
};

static constexpr WebPort WEB_PORTS[] = {
  {80, false},
  {443, true},
  {8000, false},
  {8080, false},
  {8081, false},
  {8443, true},
  {8888, false},
};

static constexpr uint8_t WEB_PORT_COUNT =
  sizeof(WEB_PORTS) / sizeof(WEB_PORTS[0]);

static bool quickTcpOpen(const char* ip, uint16_t port, uint32_t timeoutMs)
{
  WiFiClient client;
  const bool open = client.connect(ip, port, timeoutMs);
  client.stop();
  return open;
}
}

void WebServerScannerScreen::onInit()
{
  memset(_hosts, 0, sizeof(_hosts));
  memset(_results, 0, sizeof(_results));
  memset(_resultItems, 0, sizeof(_resultItems));
  memset(_configItems, 0, sizeof(_configItems));
  _showConfig();
}

void WebServerScannerScreen::onBack()
{
  if (_state == STATE_DETAILS) {
    _showResults();
    return;
  }

  if (_state == STATE_RESULTS) {
    _showConfig();
    return;
  }

  if (_state != STATE_SCANNING) {
    Screen.goBack();
  }
}

void WebServerScannerScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_CONFIG) {
    if (index >= _configCount) return;

    switch (_configActions[index]) {
      case CFG_MODE:
        _scanMode = (_scanMode == MODE_RANGE) ? MODE_TARGETS : MODE_RANGE;
        _showConfig(index);
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
        int value = InputNumberAction::popup(
          "Start IP",
          1,
          _endIp,
          _startIp
        );
        if (!InputNumberAction::wasCancelled()) _startIp = value;
        _showConfig(index);
        break;
      }

      case CFG_END_IP: {
        int value = InputNumberAction::popup(
          "End IP",
          _startIp,
          254,
          _endIp
        );
        if (!InputNumberAction::wasCancelled()) _endIp = value;
        _showConfig(index);
        break;
      }

      case CFG_START_SCAN:
        _scan();
        break;
    }
    return;
  }

  if (_state == STATE_RESULTS && index < _resultCount) {
    _showDetails(index);
  }
}

void WebServerScannerScreen::_showConfig(uint8_t selectedIndex)
{
  _state = STATE_CONFIG;
  _configCount = 0;

  auto add = [&](const char* label, const char* sub, ConfigAction action) {
    _configItems[_configCount] = {label, sub};
    _configActions[_configCount] = action;
    _configCount++;
  };

  add("Mode", _scanMode == MODE_RANGE ? "Range" : "Targets", CFG_MODE);

  if (_scanMode == MODE_TARGETS) {
    static const ConfigAction targetActions[MAX_TARGETS] = {
      CFG_TARGET_1,
      CFG_TARGET_2,
      CFG_TARGET_3,
      CFG_TARGET_4,
    };

    static const char* targetLabels[MAX_TARGETS] = {
      "Target 1",
      "Target 2",
      "Target 3",
      "Target 4",
    };

    for (uint8_t i = 0; i < MAX_TARGETS; ++i) {
      _targetSubs[i] = _targets[i].length() > 0 ? _targets[i] : "-";
      add(targetLabels[i], _targetSubs[i].c_str(), targetActions[i]);
    }
  } else {
    _startIpSub = String(_startIp);
    _endIpSub = String(_endIp);
    add("Start IP", _startIpSub.c_str(), CFG_START_IP);
    add("End IP", _endIpSub.c_str(), CFG_END_IP);
  }

  add("Start Scan", nullptr, CFG_START_SCAN);
  setItems(_configItems, _configCount, selectedIndex);
}

void WebServerScannerScreen::_scan()
{
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Not connected to WiFi");
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

  _state = STATE_SCANNING;
  _resultCount = 0;
  memset(_results, 0, sizeof(_results));

  render();
  ScanCancelUtil::begin();
  ProgressView::init();
  ProgressView::progress("Scanning...", 0);

  if (_scanMode == MODE_TARGETS) {
    uint8_t targetCount = 0;
    for (uint8_t i = 0; i < MAX_TARGETS; ++i) {
      if (_targets[i].length() > 0) targetCount++;
    }

    uint8_t done = 0;
    for (uint8_t i = 0;
         i < MAX_TARGETS && _resultCount < WebScanUtil::MAX_RESULTS;
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
      _scanTarget(resolved.c_str(), label);
      done++;
    }
  } else {
    _scanRange();
  }

  ProgressView::finish();

  if (ScanCancelUtil::wasCancelled()) {
    _showConfig();
    return;
  }
  _showResults();
}

void WebServerScannerScreen::_scanRange()
{
  uint8_t hostCount = IpScanUtil::scan(
    (uint8_t)_startIp,
    (uint8_t)_endIp,
    _hosts,
    MAX_FOUND_HOSTS,
    false,
    [](uint8_t pct) {
      ProgressView::progress("Scanning hosts...", pct);
    },
    []() { return ScanCancelUtil::poll(); }
  );

  if (hostCount == 0) return;

  for (uint8_t i = 0;
       i < hostCount && _resultCount < WebScanUtil::MAX_RESULTS;
       ++i) {
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
    _scanTarget(_hosts[i].ip, label);
  }
}

void WebServerScannerScreen::_scanTarget(const char* ip, const char* label)
{
  const bool patient = _scanMode == MODE_TARGETS;
  const uint32_t quickTimeout = patient ? 450 : 300;

  for (uint8_t i = 0;
       i < WEB_PORT_COUNT && _resultCount < WebScanUtil::MAX_RESULTS;
       ++i) {
    if (quickTcpOpen(ip, WEB_PORTS[i].port, quickTimeout)) {
      WebScanUtil::Result result;

      if (WebScanUtil::probe(
            ip,
            WEB_PORTS[i].port,
            WEB_PORTS[i].https,
            result,
            patient
          )) {
        _results[_resultCount++] = result;
      }
    }

    ProgressView::progress(
      label,
      (uint8_t)((uint16_t)(i + 1) * 100 / WEB_PORT_COUNT)
    );
  }
}

void WebServerScannerScreen::_showResults()
{
  _state = STATE_RESULTS;
  setStackedSublabels(false);

  if (_resultCount == 0) {
    _resultItems[0] = {"No web servers found"};
    setItems(_resultItems, 1);
    return;
  }

  for (uint8_t i = 0; i < _resultCount; ++i) {
    const auto& result = _results[i];

    _resultSubs[i] =
      String(result.https ? "HTTPS" : "HTTP") +
      " :" +
      String(result.port);

    _resultItems[i] = {
      result.ip,
      _resultSubs[i].c_str(),
    };
  }

  setItems(_resultItems, _resultCount);
}

void WebServerScannerScreen::_showDetails(uint8_t index)
{
  if (index >= _resultCount) return;

  _state = STATE_DETAILS;
  setStackedSublabels(true);
  const auto& result = _results[index];

  _detailSubs[0] = result.ip;
  _detailItems[0] = {"IP", _detailSubs[0].c_str()};

  _detailSubs[1] = String(result.port);
  _detailItems[1] = {"Port", _detailSubs[1].c_str()};

  _detailSubs[2] = result.https ? "HTTPS" : "HTTP";
  _detailItems[2] = {"Protocol", _detailSubs[2].c_str()};

  _detailSubs[3] = result.status > 0
    ? String(result.status)
    : "-";
  _detailItems[3] = {"Status", _detailSubs[3].c_str()};

  _detailSubs[4] = result.server[0] ? result.server : "-";
  _detailItems[4] = {"Server", _detailSubs[4].c_str()};

  _detailSubs[5] = result.title[0] ? result.title : "-";
  _detailItems[5] = {"Title", _detailSubs[5].c_str()};

  setItems(_detailItems, DETAIL_ROWS);
}

void WebServerScannerScreen::_editTarget(
  uint8_t targetIndex,
  uint8_t selectedIndex
)
{
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

  _showConfig(selectedIndex);
}

bool WebServerScannerScreen::_validIp(const String& ip) const
{
  int a, b, c, d;
  char tail;

  return sscanf(
           ip.c_str(),
           "%d.%d.%d.%d%c",
           &a,
           &b,
           &c,
           &d,
           &tail
         ) == 4 &&
         a >= 0 && a <= 255 &&
         b >= 0 && b <= 255 &&
         c >= 0 && c <= 255 &&
         d >= 0 && d <= 255;
}

bool WebServerScannerScreen::_hasTargets() const
{
  for (uint8_t i = 0; i < MAX_TARGETS; ++i) {
    if (_targets[i].length() > 0) return true;
  }

  return false;
}

String WebServerScannerScreen::_networkPrefix() const
{
  IPAddress ip = WiFi.localIP();

  if (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0) {
    return "";
  }

  char prefix[16];
  snprintf(
    prefix,
    sizeof(prefix),
    "%u.%u.%u.",
    ip[0],
    ip[1],
    ip[2]
  );

  return String(prefix);
}
