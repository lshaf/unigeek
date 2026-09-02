#include "MdnsScannerScreen.h"
#include <WiFi.h>
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"
#include "utils/network/ScanCancelUtil.h"

const char* MdnsScannerScreen::SERVICE_TYPES[SERVICE_COUNT] = {
  "_services._dns-sd._udp.local",
  "_ipp._tcp.local",
  "_googlecast._tcp.local",
  "_airplay._tcp.local",
  "_smb._tcp.local",
};

void MdnsScannerScreen::onInit()
{
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Not connected to WiFi", 1500);
    Screen.goBack();
    return;
  }

  memset(_results, 0, sizeof(_results));
  _scan();
}

void MdnsScannerScreen::onBack()
{
  if (_state == STATE_DETAILS) {
    _showResults();
    return;
  }

  Screen.goBack();
}

void MdnsScannerScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_RESULTS && index < _resultCount) {
    _showDetails(index);
  }
}

void MdnsScannerScreen::_scan()
{
  _state = STATE_SCANNING;
  _resultCount = 0;
  memset(_results, 0, sizeof(_results));

  render();
  ScanCancelUtil::begin();
  ProgressView::init();

  // Always discover all service classes currently supported by this scanner.
  for (uint8_t i = 1; i < SERVICE_COUNT; ++i) {
    if (_resultCount >= MdnsScanUtil::MAX_RESULTS) break;

    uint8_t remaining = MdnsScanUtil::MAX_RESULTS - _resultCount;
    ProgressView::progress(
      "Scanning...",
      (uint8_t)((i - 1) * 100 / (SERVICE_COUNT - 1))
    );
    uint8_t found = MdnsScanUtil::discover(
      SERVICE_TYPES[i],
      &_results[_resultCount],
      remaining,
      [](uint8_t) { ScanCancelUtil::poll(); }
    );
    _resultCount += found;
    if (ScanCancelUtil::wasCancelled()) break;
  }

  ProgressView::finish();

  if (ScanCancelUtil::wasCancelled()) {
    Screen.goBack();
    return;
  }

  _showResults();
}

void MdnsScannerScreen::_showResults()
{
  _state = STATE_RESULTS;
  setStackedSublabels(false);

  if (_resultCount == 0) {
    _resultItems[0] = {"No mDNS services found"};
    setItems(_resultItems, 1);
    return;
  }

  for (uint8_t i = 0; i < _resultCount; ++i) {
    if (_results[i].ip[0]) {
      _resultSubs[i] = _results[i].ip;
    } else if (_results[i].host[0]) {
      _resultSubs[i] = _results[i].host;
    } else {
      _resultSubs[i] = _results[i].service;
    }

    _resultItems[i] = {
      _results[i].name[0] ? _results[i].name : "mDNS Service",
      _resultSubs[i].c_str()
    };
  }

  setItems(_resultItems, _resultCount);
}

void MdnsScannerScreen::_showDetails(uint8_t index)
{
  if (index >= _resultCount) return;
  _state = STATE_DETAILS;
  setStackedSublabels(true);

  const auto& svc = _results[index];

  _detailSubs[0] = svc.name[0]    ? svc.name    : "-";
  _detailSubs[1] = svc.host[0]    ? svc.host    : "-";
  _detailSubs[2] = svc.ip[0]      ? svc.ip      : "-";
  _detailSubs[3] = svc.service[0] ? svc.service : "-";
  _detailSubs[4] = String(svc.port);
  _detailSubs[5] = svc.txt[0]     ? svc.txt     : "-";

  _detailItems[0] = {"Name",    _detailSubs[0].c_str()};
  _detailItems[1] = {"Host",    _detailSubs[1].c_str()};
  _detailItems[2] = {"IP",      _detailSubs[2].c_str()};
  _detailItems[3] = {"Service", _detailSubs[3].c_str()};
  _detailItems[4] = {"Port",    _detailSubs[4].c_str()};
  _detailItems[5] = {"TXT",     _detailSubs[5].c_str()};

  setItems(_detailItems, DETAIL_ROWS);
}
