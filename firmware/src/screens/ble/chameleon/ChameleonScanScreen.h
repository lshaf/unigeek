#pragma once
#include "ui/templates/ListScreen.h"
#include <NimBLEDevice.h>

class ChameleonScanScreen : public ListScreen {
public:
  const char* title() override { return "Scan Chameleon"; }
  bool inhibitPowerOff()  override { return true; }
  bool inhibitPowerSave() override { return true; }

  ~ChameleonScanScreen() override;
  void onInit()                override;
  void onUpdate()              override;
  void onRender()              override;
  void onItemSelected(uint8_t) override;
  void onBack()                override;

private:
  struct DeviceEntry {
    char name[24];
    char addr[18];
    int8_t rssi;
    NimBLEAddress bleAddr;
  };

  static constexpr int kMaxDevices = 8;

  enum State { STATE_EMPTY, STATE_LISTED };
  State _state    = STATE_EMPTY;
  bool  _scanning = false;

  NimBLEScan*    _bleScan    = nullptr;
  DeviceEntry    _devices[kMaxDevices];
  volatile int   _devCount   = 0;
  volatile bool  _devChanged = false;

  ListItem _items[kMaxDevices];
  char     _labels[kMaxDevices][24];
  char     _sublabels[kMaxDevices][28];

  void _startScan();
  void _stopScan();
  void _rebuildList();
  void _onDevice(NimBLEAdvertisedDevice* dev);
  bool _isChameleon(NimBLEAdvertisedDevice* dev);

  class ScanCallbacks;
  friend class ScanCallbacks;
  static ChameleonScanScreen* _instance;
};
