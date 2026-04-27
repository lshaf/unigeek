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

  static constexpr uint8_t kMaxDevices = 40;
  static constexpr uint8_t kInfoRows   = 16;

  NimBLEScan*       _bleScan           = nullptr;
  NimBLEScanResults _scanResults;
  int               _selectedDeviceIdx = -1;

  // Device list storage
  String   _devLabel[kMaxDevices];
  String   _devSub[kMaxDevices];
  ListItem _devItems[kMaxDevices];
  uint8_t  _devCount = 0;

  // Info view (16 fixed rows)
  String   _infoVal[kInfoRows];
  ListItem _infoItems[kInfoRows];

  // Detail view — wrapped scrolling text for clickable info-row drill-downs.
  TextScrollView _textView;
  String         _detailTitle;

  void _startScan();
  void _showList();
  void _showInfo();
  void _showDetail(const char* titleText, const String& content);
};