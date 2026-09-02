#include "IoTDeviceScannerScreen.h"
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
#include "utils/network/MdnsScanUtil.h"
#include "utils/network/SsdpScanUtil.h"
#include "utils/network/WebScanUtil.h"
#include <stdlib.h>

namespace {

static bool quickTcpOpen(const char* ip, uint16_t port, uint32_t timeoutMs)
{
  WiFiClient client;
  const bool open = client.connect(ip, port, timeoutMs);
  client.stop();
  return open;
}

static constexpr const char* MDNS_TYPES[] = {
  "_googlecast._tcp.local",
  "_airplay._tcp.local",
  "_hap._tcp.local",
  "_matter._tcp.local",
  "_home-assistant._tcp.local",
  "_esphomelib._tcp.local",
  "_ipp._tcp.local",
};

static constexpr uint16_t IOT_PORTS[] = {
  1883,
  8883,
  554,
  1400,
  8008,
  8009,
  8123,
};

}

void IoTDeviceScannerScreen::onInit()
{
  memset(_hosts, 0, sizeof(_hosts));
  memset(_devices, 0, sizeof(_devices));
  memset(_resultItems, 0, sizeof(_resultItems));
  memset(_configItems, 0, sizeof(_configItems));
  _showConfig();
}

void IoTDeviceScannerScreen::onBack()
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

void IoTDeviceScannerScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_CONFIG) {
    if (index >= _configCount) return;

    switch (_configActions[index]) {
      case CFG_MODE:
        _scanMode = (_scanMode == MODE_RANGE)
          ? MODE_TARGETS
          : MODE_RANGE;
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

  if (_state == STATE_RESULTS && index < _deviceCount) {
    _showDetails(index);
  }
}

void IoTDeviceScannerScreen::_showConfig(uint8_t selectedIndex)
{
  _state = STATE_CONFIG;
  _configCount = 0;

  auto add = [&](const char* label, const char* sub, ConfigAction action) {
    _configItems[_configCount] = {label, sub};
    _configActions[_configCount] = action;
    _configCount++;
  };

  add(
    "Mode",
    _scanMode == MODE_RANGE ? "Range" : "Targets",
    CFG_MODE
  );

  if (_scanMode == MODE_TARGETS) {
    static const ConfigAction actions[MAX_TARGETS] = {
      CFG_TARGET_1,
      CFG_TARGET_2,
      CFG_TARGET_3,
      CFG_TARGET_4,
    };

    static const char* labels[MAX_TARGETS] = {
      "Target 1",
      "Target 2",
      "Target 3",
      "Target 4",
    };

    for (uint8_t i = 0; i < MAX_TARGETS; ++i) {
      _targetSubs[i] = _targets[i].length()
        ? _targets[i]
        : "-";

      add(labels[i], _targetSubs[i].c_str(), actions[i]);
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

void IoTDeviceScannerScreen::_scan()
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
      if (_targets[i].length() &&
          !TargetResolveUtil::isValidTarget(_targets[i])) {
        ShowStatusAction::show("Invalid target");
        return;
      }
    }
  }

  _state = STATE_SCANNING;
  _deviceCount = 0;
  memset(_devices, 0, sizeof(_devices));

  render();
  ScanCancelUtil::begin();
  ProgressView::init();
  ProgressView::progress(
    _scanMode == MODE_RANGE ? "Scanning hosts..." : "Scanning...",
    1
  );

  if (!_discoverMulticast()) {
    ProgressView::finish();
    _showConfig();
    return;
  }

  if (ScanCancelUtil::wasCancelled()) {
    ProgressView::finish();
    _showConfig();
    return;
  }

  if (_scanMode == MODE_RANGE) {
    _scanRange();
  } else {
    _scanTargets();
  }

  ProgressView::finish();

  if (ScanCancelUtil::wasCancelled()) {
    _showConfig();
    return;
  }
  _showResults();
}

bool IoTDeviceScannerScreen::_discoverMulticast()
{
  const char* progressLabel =
    _scanMode == MODE_RANGE ? "Scanning hosts..." : "Scanning...";

  SsdpScanUtil::Device* ssdpBuffer = static_cast<SsdpScanUtil::Device*>(
    calloc(SsdpScanUtil::MAX_DEVICES, sizeof(SsdpScanUtil::Device))
  );
  if (!ssdpBuffer) {
    ShowStatusAction::show("Not enough memory", 1200);
    return false;
  }

  uint8_t ssdpCount = SsdpScanUtil::discover(
    "ssdp:all",
    ssdpBuffer,
    SsdpScanUtil::MAX_DEVICES,
    nullptr
  );

  for (uint8_t i = 0; i < ssdpCount; ++i) {
    if (!_acceptIp(ssdpBuffer[i].ip)) continue;

    IoTScanUtil::Device evidence;
    if (IoTScanUtil::addSsdpEvidence(
          ssdpBuffer[i],
          evidence
        )) {
      _mergeEvidence(evidence);
    }
  }
  free(ssdpBuffer);

  if (ScanCancelUtil::wasCancelled()) return true;

  ProgressView::progress(progressLabel, 12);

  constexpr uint8_t typeCount =
    sizeof(MDNS_TYPES) / sizeof(MDNS_TYPES[0]);

  MdnsScanUtil::Service* mdnsBuffer = static_cast<MdnsScanUtil::Service*>(
    calloc(16, sizeof(MdnsScanUtil::Service))
  );
  if (!mdnsBuffer) {
    ShowStatusAction::show("Not enough memory", 1200);
    return false;
  }

  for (uint8_t type = 0; type < typeCount; ++type) {
    memset(mdnsBuffer, 0, 16 * sizeof(MdnsScanUtil::Service));

    uint8_t count = MdnsScanUtil::discover(
      MDNS_TYPES[type],
      mdnsBuffer,
      16,
      nullptr
    );

    for (uint8_t i = 0; i < count; ++i) {
      if (!mdnsBuffer[i].ip[0] ||
          !_acceptIp(mdnsBuffer[i].ip)) {
        continue;
      }

      IoTScanUtil::Device evidence;
      if (IoTScanUtil::addMdnsEvidence(
            mdnsBuffer[i],
            evidence
          )) {
        _mergeEvidence(evidence);
      }
    }

    if (ScanCancelUtil::wasCancelled()) break;

    ProgressView::progress(
      progressLabel,
      (uint8_t)(12 + ((uint16_t)(type + 1) * 23 / typeCount))
    );
  }

  free(mdnsBuffer);
  return true;
}

void IoTDeviceScannerScreen::_scanRange()
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

  for (uint8_t i = 0; i < hostCount; ++i) {
    char label[48];
    snprintf(
      label, sizeof(label),
      "Scanning %s (%u/%u)...",
      _hosts[i].ip,
      (unsigned)(i + 1),
      (unsigned)hostCount
    );
    ProgressView::progress(label, 0);
    _probeTarget(_hosts[i].ip, label);
  }
}

void IoTDeviceScannerScreen::_scanTargets()
{
  uint8_t count = 0;

  for (uint8_t i = 0; i < MAX_TARGETS; ++i) {
    if (_targets[i].length()) count++;
  }

  uint8_t done = 0;

  for (uint8_t i = 0; i < MAX_TARGETS; ++i) {
    if (!_targets[i].length()) continue;

    char label[48];
    snprintf(
      label, sizeof(label),
      "Scanning %s (%u/%u)...",
      _targets[i].c_str(),
      (unsigned)(done + 1),
      (unsigned)count
    );
    ProgressView::progress(label, 0);
    String resolved;
    if (!TargetResolveUtil::resolve(_targets[i], resolved)) {
      ShowStatusAction::show("Could not resolve target", 900);
      done++;
      continue;
    }
    _probeTarget(resolved.c_str(), label);
    done++;
  }
}

void IoTDeviceScannerScreen::_probeTarget(const char* ip, const char* label)
{
  if (!ip || !ip[0]) return;

  const bool patient = _scanMode == MODE_TARGETS;
  const uint32_t quickTimeout = patient ? 450 : 300;

  static constexpr struct {
    uint16_t port;
    bool https;
  } WEB_PROBES[] = {
    {80, false},
    {443, true},
    {8008, false},
    {8123, false},
  };

  static constexpr uint8_t WEB_COUNT =
    sizeof(WEB_PROBES) / sizeof(WEB_PROBES[0]);
  static constexpr uint8_t IOT_COUNT =
    sizeof(IOT_PORTS) / sizeof(IOT_PORTS[0]);
  static constexpr uint8_t TOTAL_STEPS = WEB_COUNT + IOT_COUNT;

  uint8_t step = 0;
  auto advance = [&]() {
    ++step;
    ProgressView::progress(label, (uint8_t)((uint16_t)step * 100 / TOTAL_STEPS));
  };

  for (const auto& probe : WEB_PROBES) {
    if (quickTcpOpen(ip, probe.port, quickTimeout)) {
      WebScanUtil::Result web;

      if (WebScanUtil::probe(ip, probe.port, probe.https, web, patient)) {
        IoTScanUtil::Device evidence;
        if (IoTScanUtil::addWebEvidence(web, evidence)) {
          _mergeEvidence(evidence);
        }
      }
    }
    advance();
  }

  for (uint16_t port : IOT_PORTS) {
    IoTScanUtil::Device evidence;

    if (quickTcpOpen(ip, port, quickTimeout) &&
        IoTScanUtil::addPortEvidence(ip, port, evidence, patient)) {
      _mergeEvidence(evidence);
    }
    advance();
  }
}

IoTScanUtil::Device* IoTDeviceScannerScreen::_findDevice(
  const char* ip
)
{
  if (!ip || !ip[0]) return nullptr;

  for (uint8_t i = 0; i < _deviceCount; ++i) {
    if (strcmp(_devices[i].ip, ip) == 0) {
      return &_devices[i];
    }
  }

  return nullptr;
}

IoTScanUtil::Device* IoTDeviceScannerScreen::_addDevice(
  const char* ip
)
{
  if (!ip || !ip[0] ||
      _deviceCount >= IoTScanUtil::MAX_DEVICES) {
    return nullptr;
  }

  IoTScanUtil::Device* dev = &_devices[_deviceCount++];
  memset(dev, 0, sizeof(*dev));
  strlcpy(dev->ip, ip, sizeof(dev->ip));

  return dev;
}

void IoTDeviceScannerScreen::_mergeEvidence(
  const IoTScanUtil::Device& evidence
)
{
  if (!evidence.ip[0]) return;

  IoTScanUtil::Device* dev = _findDevice(evidence.ip);

  if (!dev) {
    dev = _addDevice(evidence.ip);
  }

  if (!dev) return;

  IoTScanUtil::merge(*dev, evidence);
}

void IoTDeviceScannerScreen::_showResults()
{
  _state = STATE_RESULTS;
  setStackedSublabels(false);

  if (_deviceCount == 0) {
    _resultItems[0] = {"No IoT devices found"};
    setItems(_resultItems, 1);
    return;
  }

  for (uint8_t i = 0; i < _deviceCount; ++i) {
    const auto& dev = _devices[i];

    _resultSubs[i] =
      String(dev.category) +
      " / " +
      String(dev.device);

    _resultItems[i] = {
      dev.name[0] ? dev.name : dev.ip,
      _resultSubs[i].c_str(),
    };
  }

  setItems(_resultItems, _deviceCount);
}

void IoTDeviceScannerScreen::_showDetails(uint8_t index)
{
  if (index >= _deviceCount) return;

  _state = STATE_DETAILS;
  setStackedSublabels(true);
  const auto& dev = _devices[index];

  _detailSubs[0] = dev.ip;
  _detailItems[0] = {"IP", _detailSubs[0].c_str()};

  _detailSubs[1] = dev.name[0] ? dev.name : "-";
  _detailItems[1] = {"Name", _detailSubs[1].c_str()};

  _detailSubs[2] = dev.category;
  _detailItems[2] = {"Category", _detailSubs[2].c_str()};

  _detailSubs[3] = dev.device;
  _detailItems[3] = {"Device", _detailSubs[3].c_str()};

  _detailSubs[4] =
    IoTScanUtil::confidenceName(dev.confidence);
  _detailItems[4] = {
    "Confidence",
    _detailSubs[4].c_str(),
  };

  _detailSubs[5] = dev.evidence[0]
    ? dev.evidence
    : "-";
  _detailItems[5] = {
    "Evidence",
    _detailSubs[5].c_str(),
  };

  setItems(_detailItems, DETAIL_ROWS);
}

void IoTDeviceScannerScreen::_editTarget(
  uint8_t targetIndex,
  uint8_t selectedIndex
)
{
  if (targetIndex >= MAX_TARGETS) return;

  String initial = _targets[targetIndex].length()
    ? _targets[targetIndex]
    : _networkPrefix();

  char title[16];
  snprintf(
    title,
    sizeof(title),
    "Target %u",
    (unsigned)(targetIndex + 1)
  );

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

bool IoTDeviceScannerScreen::_acceptIp(const char* ip) const
{
  if (!ip || !ip[0]) return false;

  if (_scanMode == MODE_TARGETS) {
    for (uint8_t i = 0; i < MAX_TARGETS; ++i) {
      if (!_targets[i].length()) continue;
      String resolved;
      if (TargetResolveUtil::resolve(_targets[i], resolved) && resolved == ip) {
        return true;
      }
    }

    return false;
  }

  int a, b, c, d;
  char tail;

  if (sscanf(
        ip,
        "%d.%d.%d.%d%c",
        &a,
        &b,
        &c,
        &d,
        &tail
      ) != 4) {
    return false;
  }

  IPAddress local = WiFi.localIP();

  return a == local[0] &&
         b == local[1] &&
         c == local[2] &&
         d >= _startIp &&
         d <= _endIp;
}

bool IoTDeviceScannerScreen::_validIp(const String& ip) const
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

bool IoTDeviceScannerScreen::_hasTargets() const
{
  for (uint8_t i = 0; i < MAX_TARGETS; ++i) {
    if (_targets[i].length()) return true;
  }

  return false;
}

String IoTDeviceScannerScreen::_networkPrefix() const
{
  IPAddress ip = WiFi.localIP();

  if (ip[0] == 0 &&
      ip[1] == 0 &&
      ip[2] == 0 &&
      ip[3] == 0) {
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
