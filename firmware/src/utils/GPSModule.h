//
// Created by L Shaf on 2026-03-26.
//

#pragma once

#include <Arduino.h>
#include <TinyGPS++.h>
#include <array>
#include "core/IStorage.h"

class GPSModule
{
public:
  TinyGPSPlus gps;

  enum WardriveState {
    WARDRIVE_IDLE,
    WARDRIVE_SCANNING,
    WARDRIVE_SAVING
  };

  void begin(uint8_t uartNum, uint32_t baudRate, int8_t rxPin, int8_t txPin = -1);
  void end();
  void update();

  String getCurrentDate();
  String getCurrentTime();

  bool initWardrive(IStorage* storage);
  void doWardrive(IStorage* storage);
  void endWardrive();

  WardriveState wardriveStatus() { return _wardriveState; }
  const String& wardriveFilename() { return _filename; }
  const String& wardriveError() { return _lastError; }
  uint16_t discoveredCount() { return _totalDiscovered; }

private:
  HardwareSerial* _serial = nullptr;
  bool _ownSerial = false;
  WardriveState _wardriveState = WARDRIVE_IDLE;

  String _savePath = "/unigeek/gps/wardriver";
  String _filename;
  String _lastError;
  uint16_t _totalDiscovered = 0;
  unsigned long _lastWifiScan = 0;

  void _addWigleRecord(IStorage* storage, const String& ssid, const String& bssid,
                        const String& authMode, int32_t rssi, int32_t channel);

  static String _pad(int val, int width);

  // circular BSSID dedup buffer
  using BSSID = std::array<uint8_t, 6>;
  static const uint8_t BSSID_BUF_SIZE = 80;
  BSSID _bssidBuf[BSSID_BUF_SIZE];
  uint8_t _bssidCount = 0;
  uint8_t _bssidHead = 0;

  bool _isBssidScanned(const uint8_t* bssid);
};
