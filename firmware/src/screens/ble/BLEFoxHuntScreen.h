#pragma once

#include "ui/templates/ListScreen.h"
#include "utils/FoxHuntFeedback.h"
#include <NimBLEDevice.h>

class BLEFoxHuntScreen : public ListScreen {
public:
  const char* title() override { return "BLE Fox Hunt"; }
  bool inhibitPowerSave() override { return _state == STATE_TRACK; }

  ~BLEFoxHuntScreen() override;
  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  static constexpr uint8_t kMaxDevices = 40;
  static constexpr uint32_t kScanSeconds = 5;

  enum State { STATE_SCAN, STATE_LIST, STATE_TRACK };
  State _state = STATE_SCAN;

  NimBLEScan*       _bleScan = nullptr;
  NimBLEScanResults _scanResults;
  NimBLEAdvertisedDevice _devices[kMaxDevices];
  NimBLEAdvertisedDevice _selDev;

  String   _labels[kMaxDevices];
  String   _subs[kMaxDevices];
  ListItem _items[kMaxDevices + 1];
  uint8_t  _devCount = 0;
  int      _selected = -1;

  FoxHuntFeedback _feedback;
  int             _lastRawRssi = 0;
  uint32_t        _lastLiveSeen = 0;
  uint32_t        _lastDraw = 0;
  bool            _watching = false;
  bool            _wasLive = false;
  bool            _uiInitialized = false;
  int             _displayedRssi = 127;
  String          _displayedLabel;
  bool            _displayedLive = false;
  Sprite*         _pulseSprite = nullptr;
  int16_t         _pulseSpriteW = 0;
  int16_t         _pulseSpriteH = 0;

  void _doScan();
  void _showList();
  void _startTracking(int index);
  void _stopTracking();
  void _updateTracking();
  void _renderTracking(bool force = false);
  void _initTrackingUi();
  void _destroyTrackingUi();
};
