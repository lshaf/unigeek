#pragma once

#include "ui/templates/ListScreen.h"
#include "utils/FoxHuntFeedback.h"

class WifiFoxHuntScreen : public ListScreen {
public:
  const char* title() override { return "Fox Hunt"; }
  bool inhibitPowerSave() override { return _state == STATE_TRACK; }

  ~WifiFoxHuntScreen() override;
  void configureTarget(bool client, const uint8_t mac[6], uint8_t channel, const char* ssid = nullptr);
  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  static constexpr int MAX_SCAN = 20;

  enum State { STATE_MODE, STATE_SCAN, STATE_TRACK };
  enum TargetMode { MODE_AP, MODE_CLIENT };
  TargetMode _mode = MODE_AP;
  bool _directTarget = false;
  State _state = STATE_SCAN;

  struct WifiEntry {
    String ssid;
    String bssid;
    int rssi = 0;
    int channel = 1;
  };

  WifiEntry _entries[MAX_SCAN];
  String    _labels[MAX_SCAN];
  String    _subs[MAX_SCAN];
  ListItem  _items[MAX_SCAN + 1];
  int       _entryCount = 0;
  int       _selected = -1;

  FoxHuntFeedback _feedback;
  int             _lastRawRssi = 0;
  uint32_t        _lastLiveSeen = 0;
  uint32_t        _lastDraw = 0;
  bool            _wasLive = false;
  bool            _uiInitialized = false;
  int             _displayedRssi = 127;
  bool            _displayedLive = false;
  bool            _continuousTone = false;
  Sprite*         _pulseSprite = nullptr;
  int16_t         _pulseSpriteW = 0;
  int16_t         _pulseSpriteH = 0;

  void _showModes();
  void _doScan();
  void _scanClients();
  void _showScan();
  void _startTracking(int index);
  void _stopTracking();
  void _updateTracking();
  void _renderTracking(bool force = false);
  void _initTrackingUi();
  void _destroyTrackingUi();
};
