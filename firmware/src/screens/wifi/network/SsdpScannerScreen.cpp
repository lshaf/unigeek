#include "SsdpScannerScreen.h"
#include <WiFi.h>
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"
#include "utils/network/ScanCancelUtil.h"

void SsdpScannerScreen::onInit()
{
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Not connected to WiFi", 1500);
    Screen.goBack();
    return;
  }

  memset(_devices, 0, sizeof(_devices));
  _scan();
}

void SsdpScannerScreen::onBack()
{
  if (_state == STATE_DETAILS) {
    _showResults();
    return;
  }

  Screen.goBack();
}

void SsdpScannerScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_RESULTS && index < _deviceCount) {
    _showDetails(index);
  }
}

void SsdpScannerScreen::_scan()
{
  _state = STATE_SCANNING;
  _deviceCount = 0;
  memset(_devices, 0, sizeof(_devices));

  render();
  ScanCancelUtil::begin();
  ProgressView::init();
  _deviceCount = SsdpScanUtil::discover(
    "ssdp:all",
    _devices,
    SsdpScanUtil::MAX_DEVICES,
    [](uint8_t pct) {
      ProgressView::progress("Scanning...", pct);
      ScanCancelUtil::poll();
    }
  );
  ProgressView::finish();

  if (ScanCancelUtil::wasCancelled()) {
    Screen.goBack();
    return;
  }

  _showResults();
}

void SsdpScannerScreen::_showResults()
{
  _state = STATE_RESULTS;
  setStackedSublabels(false);

  if (_deviceCount == 0) {
    _resultItems[0] = {"No SSDP devices found"};
    setItems(_resultItems, 1);
    return;
  }

  for (uint8_t i = 0; i < _deviceCount; ++i) {
    _resultSubs[i] = _devices[i].ip;
    _resultItems[i] = {
      _devices[i].name[0] ? _devices[i].name : "SSDP Device",
      _resultSubs[i].c_str()
    };
  }

  setItems(_resultItems, _deviceCount);
}

void SsdpScannerScreen::_showDetails(uint8_t index)
{
  if (index >= _deviceCount) return;
  _state = STATE_DETAILS;
  setStackedSublabels(true);

  const auto& dev = _devices[index];

  _detailSubs[0] = dev.name[0]     ? dev.name     : "-";
  _detailSubs[1] = dev.ip[0]       ? dev.ip       : "-";
  _detailSubs[2] = dev.st[0]       ? dev.st       : "-";
  _detailSubs[3] = dev.server[0]   ? dev.server   : "-";
  _detailSubs[4] = dev.usn[0]      ? dev.usn      : "-";
  _detailSubs[5] = dev.location[0] ? dev.location : "-";

  _detailItems[0] = {"Name", _detailSubs[0].c_str()};
  _detailItems[1] = {"IP",   _detailSubs[1].c_str()};
  _detailItems[2] = {"ST",       _detailSubs[2].c_str()};
  _detailItems[3] = {"Server",   _detailSubs[3].c_str()};
  _detailItems[4] = {"USN",      _detailSubs[4].c_str()};
  _detailItems[5] = {"Location", _detailSubs[5].c_str()};

  setItems(_detailItems, DETAIL_ROWS);
}
