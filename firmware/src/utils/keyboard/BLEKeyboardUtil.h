#pragma once

#include "HIDKeyboardUtil.h"
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

class BLEKeyboardUtil final : public HIDKeyboardUtil,
                               public NimBLEServerCallbacks,
                               public NimBLECharacteristicCallbacks {
public:
  explicit BLEKeyboardUtil(const char* deviceName = "UniGeek",
                            const char* manufacturer = "UniGeek",
                            uint8_t     batteryLevel = 100);
  ~BLEKeyboardUtil() override;

  void begin()                        override;
  void end()                          override;
  void sendReport(KeyReport* keys)    override;
  void sendMouseReport(MouseReport* m) override;
  bool isConnected()                  override;
  void setBatteryLevel(uint8_t level) override;
  void resetPair()                    override;

  // NimBLE callbacks (1.4.x signatures)
  void onConnect(NimBLEServer* pServer)                      override;
  void onDisconnect(NimBLEServer* pServer)                   override;
  void onWrite(NimBLECharacteristic* pCharacteristic)        override;

private:
  std::string _deviceName;
  uint8_t     _batteryLevel;
  bool        _connected    = false;
  bool        _initialized  = false;

  NimBLEServer*         _server     = nullptr;
  NimBLEHIDDevice*      _hid        = nullptr;
  NimBLECharacteristic* _inputKbd   = nullptr;
  NimBLECharacteristic* _outputKbd  = nullptr;
  NimBLECharacteristic* _inputMouse = nullptr;
  NimBLEAdvertising*    _adv        = nullptr;
};