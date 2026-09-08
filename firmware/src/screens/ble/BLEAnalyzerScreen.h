#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/TextScrollView.h"
#include <NimBLEDevice.h>

class BLEAnalyzerScreen : public ListScreen {
public:
  const char* title() override
  {
    return _state == STATE_DETAIL ? _detailTitle.c_str() : "BLE Analyzer";
  }

  ~BLEAnalyzerScreen() override;
  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  enum State {
    STATE_SCAN,
    STATE_LIST,
    STATE_INFO,
    STATE_DETAIL,
  } _state = STATE_SCAN;

  static constexpr uint8_t  kMaxDevices = 40;
  static constexpr uint8_t  kInfoRows   = 16;
  static constexpr uint32_t kScanSeconds = 5;   // one-shot blocking sweep

  NimBLEScan*       _bleScan           = nullptr;
  NimBLEScanResults _scanResults;
  NimBLEAdvertisedDevice _devices[kMaxDevices];
  int               _selectedDeviceIdx = -1;

  // Device list storage
  String   _devLabel[kMaxDevices];
  String   _devSub[kMaxDevices];
  ListItem _devItems[kMaxDevices + 1];
  uint8_t  _devCount = 0;

  void _doScan();
  void _rebuildDevItems();

  // Info view (16 fixed rows)
  //
  // _selDev is a by-value snapshot of the picked device (NimBLEAdvertisedDevice
  // owns its payload in a std::vector, so the copy is self-contained). The live
  // RSSI watcher below erases entries from the scan's own results vector, which
  // would otherwise dangle the pointers _scanResults holds.
  NimBLEAdvertisedDevice _selDev;
  String   _infoVal[kInfoRows];
  ListItem _infoItems[kInfoRows];

  // Live RSSI of the selected device: an indefinite duplicate-passing scan
  // whose callback only records the RSSI of the matching address.
  int           _lastRssiShown  = 1;   // sentinel: nothing rendered yet
  bool          _rssiWatching   = false;
  unsigned long _lastRssiRefresh = 0;

  void _startRssiWatch();
  void _stopRssiWatch();
  void _refreshRssiRow();

  // Detail view — wrapped scrolling text for clickable info-row drill-downs.
  TextScrollView _textView;
  String         _detailTitle;

  void _showList();
  void _showInfo();
  void _showDetail(const char* titleText, const String& content);
};