#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/TextScrollView.h"
#include <NimBLEDevice.h>

class BLEAnalyzerScreen : public ListScreen {
public:
  const char* title() override
  {
    return _state == STATE_DETAIL ? _detailTitle.c_str() : "Scanner";
  }

  ~BLEAnalyzerScreen() override;
  void configureTarget(NimBLEAdvertisedDevice& device);
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
  static constexpr uint32_t kScanSeconds = 5;

  bool _externalTarget = false;
  NimBLEScan*       _bleScan           = nullptr;
  NimBLEScanResults _scanResults;
  NimBLEAdvertisedDevice _devices[kMaxDevices];
  int               _selectedDeviceIdx = -1;

  String   _devLabel[kMaxDevices];
  String   _devSub[kMaxDevices];
  ListItem _devItems[kMaxDevices + 1];
  uint8_t  _devCount = 0;

  void _doScan();
  void _rebuildDevItems();

  NimBLEAdvertisedDevice _selDev;
  String   _infoVal[kInfoRows];
  ListItem _infoItems[kInfoRows];

  int           _lastRssiShown  = 1;
  bool          _rssiWatching   = false;
  unsigned long _lastRssiRefresh = 0;

  void _startRssiWatch();
  void _stopRssiWatch();
  void _refreshRssiRow();

  TextScrollView _textView;
  String         _detailTitle;

  void _showOptions();
  void _showList(uint8_t selectedIndex = 0);
  void _showInfo();
  void _showDetail(const char* titleText, const String& content);
};