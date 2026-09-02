#include "PrinterScannerScreen.h"
#include "utils/network/TargetResolveUtil.h"
#include <WiFi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "core/ScreenManager.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"
#include "utils/network/ScanCancelUtil.h"

void PrinterScannerScreen::onInit()
{
  memset(_printers, 0, sizeof(_printers));
  memset(_configItems, 0, sizeof(_configItems));
  _showConfig();
}

void PrinterScannerScreen::onBack()
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

void PrinterScannerScreen::onItemSelected(uint8_t index)
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
        int value = InputNumberAction::popup("Start IP", 1, _endIp, _startIp);
        if (!InputNumberAction::wasCancelled()) _startIp = value;
        _showConfig(index);
        break;
      }
      case CFG_END_IP: {
        int value = InputNumberAction::popup("End IP", _startIp, 254, _endIp);
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

  if (_state == STATE_RESULTS && index < _printerCount) {
    _showDetails(index);
  }
}

void PrinterScannerScreen::_showConfig(uint8_t selectedIndex)
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
    _endIpSub = String(_endIp);
    add("Start IP", _startIpSub.c_str(), CFG_START_IP);
    add("End IP", _endIpSub.c_str(), CFG_END_IP);
  }

  add("Start Scan", nullptr, CFG_START_SCAN);
  setItems(_configItems, _configCount, selectedIndex);
}

void PrinterScannerScreen::_scan()
{
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Not connected to WiFi", 1500);
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
  _printerCount = 0;
  memset(_printers, 0, sizeof(_printers));

  render();
  ScanCancelUtil::begin();
  ProgressView::init();

  SsdpScanUtil::Device* ssdp = static_cast<SsdpScanUtil::Device*>(
    calloc(SsdpScanUtil::MAX_DEVICES, sizeof(SsdpScanUtil::Device))
  );
  if (!ssdp) {
    ProgressView::finish();
    ShowStatusAction::show("Not enough memory", 1200);
    _showConfig();
    return;
  }

  uint8_t ssdpCount = SsdpScanUtil::discover(
    "urn:schemas-upnp-org:device:Printer:1",
    ssdp,
    SsdpScanUtil::MAX_DEVICES,
    [](uint8_t pct) {
      ProgressView::progress("Scanning...", pct / 2);
      ScanCancelUtil::poll();
    }
  );

  for (uint8_t i = 0; i < ssdpCount; ++i) {
    _mergeSsdp(ssdp[i]);
  }
  free(ssdp);

  if (ScanCancelUtil::wasCancelled()) {
    ProgressView::finish();
    _showConfig();
    return;
  }

  // SSDP and mDNS buffers are deliberately sequential: constrained devices
  // never need to hold both large discovery result arrays at once.
  MdnsScanUtil::Service* mdns = static_cast<MdnsScanUtil::Service*>(
    calloc(MdnsScanUtil::MAX_RESULTS, sizeof(MdnsScanUtil::Service))
  );
  if (!mdns) {
    ProgressView::finish();
    ShowStatusAction::show("Not enough memory", 1200);
    _showConfig();
    return;
  }

  uint8_t mdnsCount = MdnsScanUtil::discover(
    "_ipp._tcp.local",
    mdns,
    MdnsScanUtil::MAX_RESULTS,
    [](uint8_t pct) {
      ProgressView::progress("Scanning...", 50 + pct / 2);
      ScanCancelUtil::poll();
    }
  );

  for (uint8_t i = 0; i < mdnsCount; ++i) {
    _mergeMdns(mdns[i]);
  }
  free(mdns);

  ProgressView::finish();

  if (ScanCancelUtil::wasCancelled()) {
    _showConfig();
    return;
  }

  _showResults();
}

void PrinterScannerScreen::_editTarget(
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

bool PrinterScannerScreen::_validIp(const String& ip) const
{
  int a, b, c, d;
  char tail;

  return sscanf(ip.c_str(), "%d.%d.%d.%d%c", &a, &b, &c, &d, &tail) == 4 &&
         a >= 0 && a <= 255 &&
         b >= 0 && b <= 255 &&
         c >= 0 && c <= 255 &&
         d >= 0 && d <= 255;
}

bool PrinterScannerScreen::_hasTargets() const
{
  for (uint8_t i = 0; i < MAX_TARGETS; ++i) {
    if (_targets[i].length() > 0) return true;
  }
  return false;
}

bool PrinterScannerScreen::_acceptIp(const char* ip) const
{
  if (!ip || !*ip) return false;

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

  IPAddress local = WiFi.localIP();
  int a, b, c, d;
  char tail;

  if (sscanf(ip, "%d.%d.%d.%d%c", &a, &b, &c, &d, &tail) != 4) {
    return false;
  }

  return a == local[0] &&
         b == local[1] &&
         c == local[2] &&
         d >= _startIp &&
         d <= _endIp;
}

String PrinterScannerScreen::_networkPrefix() const
{
  IPAddress ip = WiFi.localIP();
  if (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0) {
    return "";
  }

  char prefix[16];
  snprintf(prefix, sizeof(prefix), "%u.%u.%u.", ip[0], ip[1], ip[2]);
  return String(prefix);
}

void PrinterScannerScreen::_showResults()
{
  _state = STATE_RESULTS;
  setStackedSublabels(false);

  if (_printerCount == 0) {
    _resultItems[0] = {"No printers found"};
    setItems(_resultItems, 1);
    return;
  }

  for (uint8_t i = 0; i < _printerCount; ++i) {
    if (_printers[i].ip[0]) {
      _resultSubs[i] = _printers[i].ip;
    } else if (_printers[i].host[0]) {
      _resultSubs[i] = _printers[i].host;
    } else {
      _resultSubs[i] = _printers[i].source;
    }

    _resultItems[i] = {
      _printers[i].name[0] ? _printers[i].name : "Network Printer",
      _resultSubs[i].c_str()
    };
  }

  setItems(_resultItems, _printerCount);
}

void PrinterScannerScreen::_showDetails(uint8_t index)
{
  if (index >= _printerCount) return;
  _state = STATE_DETAILS;
  setStackedSublabels(true);

  const Printer& p = _printers[index];

  _detailSubs[0] = p.name[0]     ? p.name     : "-";
  _detailSubs[1] = p.ip[0]       ? p.ip       : "-";
  _detailSubs[2] = p.host[0]     ? p.host     : "-";
  _detailSubs[3] = p.source[0]   ? p.source   : "-";
  _detailSubs[4] = p.port ? String(p.port) : "-";
  _detailSubs[5] = p.service[0]  ? p.service  : "-";
  _detailSubs[6] = p.server[0]   ? p.server   : "-";
  _detailSubs[7] = p.location[0] ? p.location : "-";

  _detailItems[0] = {"Name",      _detailSubs[0].c_str()};
  _detailItems[1] = {"IP",        _detailSubs[1].c_str()};
  _detailItems[2] = {"Host",      _detailSubs[2].c_str()};
  _detailItems[3] = {"Discovery", _detailSubs[3].c_str()};
  _detailItems[4] = {"Port",      _detailSubs[4].c_str()};
  _detailItems[5] = {"Service",   _detailSubs[5].c_str()};
  _detailItems[6] = {"Server",    _detailSubs[6].c_str()};
  _detailItems[7] = {"Location",  _detailSubs[7].c_str()};

  setItems(_detailItems, DETAIL_ROWS);
}

PrinterScannerScreen::Printer* PrinterScannerScreen::_findByIp(const char* ip)
{
  if (!ip || !*ip) return nullptr;

  for (uint8_t i = 0; i < _printerCount; ++i) {
    if (_printers[i].ip[0] && strcmp(_printers[i].ip, ip) == 0) {
      return &_printers[i];
    }
  }

  return nullptr;
}

PrinterScannerScreen::Printer* PrinterScannerScreen::_findByHost(const char* host)
{
  if (!host || !*host) return nullptr;

  for (uint8_t i = 0; i < _printerCount; ++i) {
    if (_printers[i].host[0] && strcasecmp(_printers[i].host, host) == 0) {
      return &_printers[i];
    }
  }

  return nullptr;
}

PrinterScannerScreen::Printer* PrinterScannerScreen::_addPrinter()
{
  if (_printerCount >= MAX_PRINTERS) return nullptr;

  Printer& p = _printers[_printerCount++];
  memset(&p, 0, sizeof(p));
  strncpy(p.name, "Network Printer", sizeof(p.name) - 1);
  return &p;
}

void PrinterScannerScreen::_mergeSsdp(const SsdpScanUtil::Device& dev)
{
  if (!_acceptIp(dev.ip)) return;

  Printer* p = _findByIp(dev.ip);
  if (!p) p = _addPrinter();
  if (!p) return;

  if (dev.name[0] && strcmp(dev.name, "SSDP Device") != 0) {
    strncpy(p->name, dev.name, sizeof(p->name) - 1);
  }

  if (dev.ip[0]) {
    strncpy(p->ip, dev.ip, sizeof(p->ip) - 1);
  }

  strncpy(p->source, "SSDP", sizeof(p->source) - 1);
  strncpy(p->service, "UPnP Printer", sizeof(p->service) - 1);

  if (dev.server[0]) {
    strncpy(p->server, dev.server, sizeof(p->server) - 1);
  }

  if (dev.location[0]) {
    strncpy(p->location, dev.location, sizeof(p->location) - 1);
  }
}

void PrinterScannerScreen::_mergeMdns(const MdnsScanUtil::Service& svc)
{
  if (!_acceptIp(svc.ip)) return;

  Printer* p = nullptr;

  if (svc.ip[0]) p = _findByIp(svc.ip);
  if (!p && svc.host[0]) p = _findByHost(svc.host);
  if (!p) p = _addPrinter();
  if (!p) return;

  if (svc.name[0]) {
    strncpy(p->name, svc.name, sizeof(p->name) - 1);
  }

  if (svc.ip[0]) {
    strncpy(p->ip, svc.ip, sizeof(p->ip) - 1);
  }

  if (svc.host[0]) {
    strncpy(p->host, svc.host, sizeof(p->host) - 1);
  }

  if (strcmp(p->source, "SSDP") == 0) {
    strncpy(p->source, "SSDP+mDNS", sizeof(p->source) - 1);
  } else {
    strncpy(p->source, "mDNS", sizeof(p->source) - 1);
  }

  if (svc.port) p->port = svc.port;

  if (svc.service[0]) {
    strncpy(p->service, svc.service, sizeof(p->service) - 1);
  }
}
