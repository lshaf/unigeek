#pragma once

#include "ui/templates/BaseScreen.h"
#include <NimBLEDevice.h>
#include <vector>
#include "utils/ble/BleSpamSignature.h"

class BLEDetectorScreen : public BaseScreen {
public:
  const char* title() override { return "Detector"; }
  bool inhibitPowerOff() override { return _scanning; }

  ~BLEDetectorScreen() override;
  void onInit()   override;
  void onUpdate() override;
  void onRender() override;

private:
  struct DetectedDevice {
    char name[20];
    char mac[18];
    char type[16];
    int8_t rssi;
    bool spoofed;
    float distance;
    int8_t prevRssi;
    unsigned long lastSeen;
  };

  struct SpamAlert {
    char type[24];
    unsigned long timestamp;
  };

  static constexpr int kMaxDevices = 30;
  static constexpr int kMaxAlerts  = 20;

  bool _scanning = false;
  bool _paused = false;
  bool _returningFromAction = false;
  int _selected = 0;
  // Full advertisement snapshots are retained for Details and Fox Hunt.
  NimBLEAdvertisedDevice _snapshots[kMaxDevices];
  NimBLEScan* _bleScan = nullptr;

  DetectedDevice _devices[kMaxDevices];
  int _deviceCount = 0;
  SpamAlert _alerts[kMaxAlerts];
  int _alertCount = 0;
  int _alertHead  = 0;
  BleSpam::SpamTracker _spamTracker;  // spam = one type from several addresses
  // Last alert per spam type: a running attack alerts at most every 30 s.
  struct AlertCooldown { const char* type; unsigned long at; };
  AlertCooldown _cooldown[12] = {};

  int _scrollOffset = 0;
  uint32_t _lastDrawMs = 0;
  bool _chromeDrawn = false;

  void _startScan();
  void _stopScan();
  void _draw();
  void _purgeOld();
  void _pause();
  void _resume();
  void _showOptions();
  void _ensureSelectedVisible();

  void _onDevice(NimBLEAdvertisedDevice* dev);
  int  _findByMac(const char* mac);
  void _pushAlert(const char* type);

  class ScanCallbacks;
  friend class ScanCallbacks;

  static BLEDetectorScreen* _instance;
};
