//
// Created by L Shaf on 2026-03-26.
//

#include "GPSModule.h"
#include <WiFi.h>
#include "core/ConfigManager.h"

void GPSModule::begin(uint8_t uartNum, uint32_t baudRate, int8_t rxPin, int8_t txPin) {
  // Release pins from any prior SPI/peripheral use
  if (rxPin >= 0) pinMode(rxPin, INPUT);
  if (txPin >= 0) pinMode(txPin, OUTPUT);

  _serial = new HardwareSerial(uartNum);
  _ownSerial = true;
  _serial->begin(baudRate, SERIAL_8N1, rxPin, txPin);
}

void GPSModule::end() {
  if (_serial) {
    _serial->flush();
    while (_serial->available()) _serial->read();
    _serial->end();
    if (_ownSerial) delete _serial;
    _serial = nullptr;
    _ownSerial = false;
  }
}

void GPSModule::update() {
  if (!_serial) return;
  while (_serial->available()) gps.encode(_serial->read());
}

String GPSModule::_pad(int val, int width) {
  String s = String(val);
  while ((int)s.length() < width) s = "0" + s;
  return s;
}

String GPSModule::getCurrentDate() {
  return String(gps.date.year()) + "-" + _pad(gps.date.month(), 2) + "-" + _pad(gps.date.day(), 2);
}

String GPSModule::getCurrentTime() {
  return _pad(gps.time.hour(), 2) + ":" + _pad(gps.time.minute(), 2) + ":" + _pad(gps.time.second(), 2);
}

bool GPSModule::_isBssidScanned(const uint8_t* bssid) {
  BSSID b;
  memcpy(b.data(), bssid, 6);
  for (uint8_t i = 0; i < _bssidCount; i++) {
    if (_bssidBuf[i] == b) return true;
  }
  // add to circular buffer
  _bssidBuf[_bssidHead] = b;
  _bssidHead = (_bssidHead + 1) % BSSID_BUF_SIZE;
  if (_bssidCount < BSSID_BUF_SIZE) _bssidCount++;
  return false;
}

void GPSModule::_addWigleRecord(IStorage* storage, const String& ssid, const String& bssid,
                                 const String& authMode, int32_t rssi, int32_t channel) {
  String fullPath = _savePath + "/" + _filename;
  fs::File f = storage->open(fullPath.c_str(), FILE_APPEND);
  if (!f) return;

  int frequency = channel != 14 ? 2407 + (channel * 5) : 2484;
  f.println(
    bssid + "," +
    "\"" + ssid + "\"," +
    authMode + "," +
    getCurrentDate() + " " + getCurrentTime() + "," +
    String(channel) + "," +
    String(frequency) + "," +
    String(rssi) + "," +
    String(gps.location.lat(), 6) + "," +
    String(gps.location.lng(), 6) + "," +
    String(gps.altitude.meters(), 2) + "," +
    String(gps.hdop.hdop() * 1.0) + "," +
    ",,WIFI"
  );
  f.close();
}

bool GPSModule::initWardrive(IStorage* storage) {
  endWardrive();

  if (!storage || !storage->isAvailable()) {
    _lastError = "Storage not available";
    return false;
  }

  String cleanDate = getCurrentDate();
  String cleanTime = getCurrentTime();
  cleanDate.replace("-", "");
  cleanTime.replace(":", "");
  _filename = cleanDate + "_" + cleanTime + ".csv";

  storage->makeDir(_savePath.c_str());
  String fullPath = _savePath + "/" + _filename;

  if (storage->exists(fullPath.c_str())) {
    _lastError = "File already exists";
    _filename = "";
    return false;
  }

  fs::File f = storage->open(fullPath.c_str(), FILE_WRITE);
  if (!f) {
    _lastError = "Failed to create file";
    _filename = "";
    return false;
  }

  String deviceName = Config.get(APP_CONFIG_DEVICE_NAME, APP_CONFIG_DEVICE_NAME_DEFAULT);
  f.println(
    "WigleWifi-1.6,"
    "appRelease=1.0,"
    "model=" + deviceName + ",release=1.0,"
    "device=UniGeek,display=" + deviceName + ",board=" + deviceName + ",brand=UniGeek,star=Sol,body=4,subBody=1"
  );
  f.println(
    "MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,CurrentLongitude,"
    "AltitudeMeters,AccuracyMeters,RCOIs,MfgrId,Type"
  );
  f.close();

  WiFi.mode(WIFI_STA);
  return true;
}

void GPSModule::doWardrive(IStorage* storage) {
  if (_wardriveState == WARDRIVE_IDLE && (millis() - _lastWifiScan > 5000)) {
    WiFi.scanNetworks(true, true);
    _wardriveState = WARDRIVE_SCANNING;
  }

  int n = WiFi.scanComplete();
  if (n > -1) {
    _wardriveState = WARDRIVE_SAVING;
    for (int i = 0; i < n; i++) {
      if (_isBssidScanned(WiFi.BSSID(i))) continue;

      String authMode;
      switch (WiFi.encryptionType(i)) {
        case WIFI_AUTH_OPEN:            authMode = "[OPEN][ESS]"; break;
        case WIFI_AUTH_WEP:             authMode = "[WEP][ESS]"; break;
        case WIFI_AUTH_WPA_PSK:         authMode = "[WPA-PSK][ESS]"; break;
        case WIFI_AUTH_WPA2_PSK:        authMode = "[WPA2-PSK][ESS]"; break;
        case WIFI_AUTH_WPA_WPA2_PSK:    authMode = "[WPA-WPA2-PSK][ESS]"; break;
        case WIFI_AUTH_WPA2_ENTERPRISE: authMode = "[WPA2-ENTERPRISE][ESS]"; break;
        default:                        authMode = "[UNKNOWN]";
      }

      _addWigleRecord(storage, WiFi.SSID(i), WiFi.BSSIDstr(i), authMode,
                       WiFi.RSSI(i), WiFi.channel(i));
      _totalDiscovered++;
    }

    WiFi.scanDelete();
    _wardriveState = WARDRIVE_IDLE;
    _lastWifiScan = millis();
  }
}

void GPSModule::endWardrive() {
  _wardriveState = WARDRIVE_IDLE;
  _lastError = "";
  _filename = "";
  _totalDiscovered = 0;
  _bssidCount = 0;
  _bssidHead = 0;
  WiFi.scanDelete();
  WiFi.mode(WIFI_STA);
}
