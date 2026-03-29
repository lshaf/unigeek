//
// Created by L Shaf on 2026-03-26.
//

#include "utils/gps/GPSModule.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <math.h>
#include <NimBLEDevice.h>
#include "core/ConfigManager.h"
#include "core/RandomSeed.h"

// === Shared scan result buffer (promiscuous WiFi + BLE task) ===

struct ScanResult {
  uint8_t addr[6];
  char name[33];
  int8_t rssi;
  uint8_t channel;
  bool isBle;
  bool hasRSN;
  bool hasWPA;
  bool privacy;
};

static constexpr int SCAN_BUF_SIZE = 16;
static ScanResult s_scanBuf[SCAN_BUF_SIZE];
static volatile int s_scanCount = 0;
static portMUX_TYPE s_scanMux = portMUX_INITIALIZER_UNLOCKED;

// === Promiscuous WiFi callback — parses beacon/probe response frames ===

static void promiscCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;

  auto* pkt = (wifi_promiscuous_pkt_t*)buf;
  const uint8_t* p = pkt->payload;
  uint16_t len = pkt->rx_ctrl.sig_len;
  if (len < 36) return;

  uint8_t subtype = p[0] >> 4;
  if (subtype != 8 && subtype != 5) return; // beacon=8, probe resp=5

  ScanResult r;
  memcpy(r.addr, &p[16], 6); // BSSID at offset 16
  r.rssi = pkt->rx_ctrl.rssi;
  r.channel = pkt->rx_ctrl.channel;
  r.isBle = false;
  memset(r.name, 0, sizeof(r.name));
  r.hasRSN = false;
  r.hasWPA = false;
  r.privacy = (p[34] & 0x10) != 0; // capability info privacy bit

  // Parse tagged parameters starting at offset 36
  uint16_t pos = 36;
  while (pos + 2 <= len) {
    uint8_t tag = p[pos];
    uint8_t tlen = p[pos + 1];
    if (pos + 2 + tlen > len) break;
    if (tag == 0 && tlen <= 32) {
      memcpy(r.name, &p[pos + 2], tlen); // SSID
    } else if (tag == 48) {
      r.hasRSN = true; // RSN = WPA2
    } else if (tag == 221 && tlen >= 4 &&
               p[pos+2]==0x00 && p[pos+3]==0x50 &&
               p[pos+4]==0xF2 && p[pos+5]==0x01) {
      r.hasWPA = true; // WPA vendor IE
    }
    pos += 2 + tlen;
  }

  portENTER_CRITICAL(&s_scanMux);
  if (s_scanCount < SCAN_BUF_SIZE) {
    s_scanBuf[s_scanCount] = r;
    s_scanCount++;
  }
  portEXIT_CRITICAL(&s_scanMux);
}

// === BLE scanning task (NimBLE — lightweight, works without PSRAM) ===

static volatile bool s_bleRunning = false;
static volatile bool s_bleTaskDone = false;

static void bleTaskFunc(void* param) {
  s_bleTaskDone = false;
  NimBLEDevice::init("");
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);

  while (s_bleRunning) {
    NimBLEScanResults results = scan->start(2, false);

    portENTER_CRITICAL(&s_scanMux);
    for (int i = 0; i < results.getCount() && s_scanCount < SCAN_BUF_SIZE; i++) {
      NimBLEAdvertisedDevice dev = results.getDevice(i);
      auto& entry = s_scanBuf[s_scanCount];
      memcpy(entry.addr, dev.getAddress().getNative(), 6);
      strncpy(entry.name, dev.getName().c_str(), 32);
      entry.name[32] = '\0';
      entry.rssi = dev.getRSSI();
      entry.channel = 0;
      entry.isBle = true;
      entry.hasRSN = false;
      entry.hasWPA = false;
      entry.privacy = false;
      s_scanCount++;
    }
    portEXIT_CRITICAL(&s_scanMux);

    scan->clearResults();
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }

  scan->stop();
  NimBLEDevice::deinit(false);
  s_bleTaskDone = true;
  vTaskDelete(NULL);
}

// === GPSModule implementation ===

void GPSModule::begin(uint8_t uartNum, uint32_t baudRate, int8_t rxPin, int8_t txPin) {
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
  char buf[8];
  snprintf(buf, sizeof(buf), "%0*d", width, val);
  return {buf};
}

String GPSModule::getCurrentDate() {
  char buf[12];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
           gps.date.year(), gps.date.month(), gps.date.day());
  return {buf};
}

String GPSModule::getCurrentTime() {
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
           gps.time.hour(), gps.time.minute(), gps.time.second());
  return {buf};
}

bool GPSModule::_isWifiScanned(const uint8_t* bssid) {
  ADDR6 b;
  memcpy(b.data(), bssid, 6);
  for (uint8_t i = 0; i < _wifiCount; i++) {
    if (_wifiBuf[i] == b) return true;
  }
  _wifiBuf[_wifiHead] = b;
  _wifiHead = (_wifiHead + 1) % DEDUP_BUF_SIZE;
  if (_wifiCount < DEDUP_BUF_SIZE) _wifiCount++;
  return false;
}

bool GPSModule::_isBleScanned(const uint8_t* addr) {
  ADDR6 b;
  memcpy(b.data(), addr, 6);
  for (uint8_t i = 0; i < _bleCount; i++) {
    if (_bleBuf[i] == b) return true;
  }
  _bleBuf[_bleHead] = b;
  _bleHead = (_bleHead + 1) % DEDUP_BUF_SIZE;
  if (_bleCount < DEDUP_BUF_SIZE) _bleCount++;
  return false;
}

void GPSModule::_addWigleRecord(IStorage* storage, const String& ssid, const String& bssid,
                                 const String& authMode, int32_t rssi, int32_t channel,
                                 const char* type) {
  String fullPath = _savePath + "/" + _filename;
  fs::File f = storage->open(fullPath.c_str(), FILE_APPEND);
  if (!f) return;

  bool isWifi = strcmp(type, "WIFI") == 0;
  int freq = isWifi ? (channel != 14 ? 2407 + (channel * 5) : 2484) : 0;
  int alt = (int)gps.altitude.meters();
  float acc = gps.hdop.hdop() * 5.0f;

  // date/time
  char dt[20];
  snprintf(dt, sizeof(dt), "%04d-%02d-%02d %02d:%02d:%02d",
           gps.date.year(), gps.date.month(), gps.date.day(),
           gps.time.hour(), gps.time.minute(), gps.time.second());

  // Write CSV line directly — no String temporaries
  f.print(bssid);
  f.print(",\"");
  f.print(ssid);
  f.print("\",");
  f.print(authMode);
  f.print(',');
  f.print(dt);
  f.print(',');
  f.print(channel);
  f.print(',');
  if (isWifi) f.print(freq);
  f.print(',');
  f.print(rssi);
  f.print(',');
  f.print(gps.location.lat(), 6);
  f.print(',');
  f.print(gps.location.lng(), 6);
  f.print(',');
  f.print(alt);
  f.print(',');
  f.print(acc, 1);
  f.print(",,,");
  f.println(type);
  f.close();
}

// === Distance tracking ===

double GPSModule::_haversine(double lat1, double lng1, double lat2, double lng2) {
  constexpr double R = 6371000.0;
  double dLat = radians(lat2 - lat1);
  double dLng = radians(lng2 - lng1);
  double a = sin(dLat / 2) * sin(dLat / 2) +
             cos(radians(lat1)) * cos(radians(lat2)) *
             sin(dLng / 2) * sin(dLng / 2);
  return R * 2 * atan2(sqrt(a), sqrt(1 - a));
}

float GPSModule::totalDistance() {
  if (!_hasStartPos || !gps.location.isValid()) return 0;
  return (float)_haversine(_startLat, _startLng, gps.location.lat(), gps.location.lng());
}

// === Recent finds for log ===

void GPSModule::_addRecentFind(const char* name, const char* addr, int8_t rssi, uint8_t channel, bool isBle) {
  auto& entry = _recentFinds[_recentHead];
  strncpy(entry.name, name, 32);
  entry.name[32] = '\0';
  strncpy(entry.addr, addr, 17);
  entry.addr[17] = '\0';
  entry.rssi = rssi;
  entry.channel = channel;
  entry.isBle = isBle;
  _recentHead = (_recentHead + 1) % MAX_RECENT;
  if (_recentCount < MAX_RECENT) _recentCount++;
}

uint8_t GPSModule::getRecentFinds(FoundEntry* out, uint8_t maxCount) {
  if (_recentCount == 0) return 0;
  uint8_t count = _recentCount < maxCount ? _recentCount : maxCount;
  uint8_t start = (_recentHead + MAX_RECENT - _recentCount) % MAX_RECENT;
  for (uint8_t i = 0; i < count; i++) {
    out[i] = _recentFinds[(start + i) % MAX_RECENT];
  }
  _recentCount = 0;
  _recentHead = 0;
  return count;
}

unsigned long GPSModule::wardriveRuntime() {
  if (_wardriveStartTime == 0) return 0;
  return millis() - _wardriveStartTime;
}

// === Promiscuous mode control ===

void GPSModule::_startPromiscuous() {
  s_scanCount = 0;
  _currentChannel = 1;
  _lastChannelHop = millis();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  wifi_promiscuous_filter_t filter;
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(promiscCb);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(_currentChannel, WIFI_SECOND_CHAN_NONE);
}

void GPSModule::_stopPromiscuous() {
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(nullptr);
}

// === BLE scan control ===

void GPSModule::_startBleScan() {
  s_bleRunning = true;
  s_bleTaskDone = false;
  xTaskCreatePinnedToCore(bleTaskFunc, "ble_wd", 4096, nullptr, 1, &_bleTask, 0);
}

void GPSModule::_stopBleScan() {
  if (!_bleTask) return;
  s_bleRunning = false;
  unsigned long start = millis();
  while (!s_bleTaskDone && millis() - start < 5000) delay(100);
  _bleTask = nullptr;
}

// === Active WiFi scan (driving mode) ===

void GPSModule::_startActiveScan() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  WiFi.scanNetworks(true); // async
  _activeScanPending = true;
  _lastWifiScan = millis();
}

void GPSModule::_doActiveScan(IStorage* storage) {
  int16_t result = WiFi.scanComplete();
  if (result == WIFI_SCAN_RUNNING) return;

  if (result > 0) {
    for (int i = 0; i < result; i++) {
      uint8_t* bssid = WiFi.BSSID(i);
      if (!bssid || _isWifiScanned(bssid)) continue;

      char addrStr[18];
      snprintf(addrStr, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
               bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);

      wifi_auth_mode_t auth = WiFi.encryptionType(i);
      String authMode;
      switch (auth) {
        case WIFI_AUTH_OPEN:         authMode = "[OPEN][ESS]"; break;
        case WIFI_AUTH_WEP:          authMode = "[WEP][ESS]"; break;
        case WIFI_AUTH_WPA_PSK:      authMode = "[WPA-PSK][ESS]"; break;
        case WIFI_AUTH_WPA2_PSK:     authMode = "[WPA2-PSK][ESS]"; break;
        case WIFI_AUTH_WPA_WPA2_PSK: authMode = "[WPA-WPA2-PSK][ESS]"; break;
        default:                     authMode = "[WPA2][ESS]"; break;
      }

      _addWigleRecord(storage, WiFi.SSID(i), String(addrStr),
                       authMode, WiFi.RSSI(i), WiFi.channel(i));
      _totalDiscovered++;

      const char* displayName = WiFi.SSID(i).length() > 0 ? WiFi.SSID(i).c_str() : "<hidden>";
      _addRecentFind(displayName, addrStr, WiFi.RSSI(i), WiFi.channel(i), false);
    }
  }

  WiFi.scanDelete();

  // Start next scan every 3 seconds
  if (millis() - _lastWifiScan > 3000) {
    WiFi.scanNetworks(true);
    _activeScanPending = true;
    _lastWifiScan = millis();
  } else {
    _activeScanPending = false;
  }
}

void GPSModule::_stopActiveScan() {
  if (_activeScanPending) {
    WiFi.scanDelete();
    _activeScanPending = false;
  }
}

// === Wardrive lifecycle ===

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

  _wardriveStartTime = millis();
  _wardriveState = WARDRIVE_SCANNING;

  if (_scanMode != SCAN_BLE_ONLY) {
    if (_wardriveMode == MODE_DRIVING) _startActiveScan();
    else _startPromiscuous();
  }
  if (_scanMode != SCAN_WIFI_ONLY) _startBleScan();
  return true;
}

void GPSModule::doWardrive(IStorage* storage) {
  // Active scan mode (driving) — process WiFi results directly
  if (_scanMode != SCAN_BLE_ONLY && _wardriveMode == MODE_DRIVING) {
    _doActiveScan(storage);
  }

  // Passive mode (walking) — channel hop
  if (_scanMode != SCAN_BLE_ONLY && _wardriveMode == MODE_WALKING && millis() - _lastChannelHop > 200) {
    _currentChannel = (_currentChannel % 13) + 1;
    esp_wifi_set_channel(_currentChannel, WIFI_SECOND_CHAN_NONE);
    _lastChannelHop = millis();
  }

  // Process buffered scan results from promiscuous + BLE
  ScanResult localBuf[SCAN_BUF_SIZE];
  int localCount;

  portENTER_CRITICAL(&s_scanMux);
  localCount = s_scanCount;
  if (localCount > 0) {
    memcpy(localBuf, s_scanBuf, localCount * sizeof(ScanResult));
    s_scanCount = 0;
  }
  portEXIT_CRITICAL(&s_scanMux);

  if (localCount > 0) {
    _wardriveState = WARDRIVE_SAVING;

    for (int i = 0; i < localCount; i++) {
      auto& r = localBuf[i];
      bool dup = r.isBle ? _isBleScanned(r.addr) : _isWifiScanned(r.addr);
      if (dup) continue;

      char addrStr[18];
      snprintf(addrStr, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
               r.addr[0], r.addr[1], r.addr[2],
               r.addr[3], r.addr[4], r.addr[5]);

      if (r.isBle) {
        String bleName = strlen(r.name) > 0 ? String(r.name) : "";
        _addWigleRecord(storage, bleName, String(addrStr),
                         "Misc [LE]", r.rssi, 0, "BLE");
        _totalBleDiscovered++;
      } else {
        String authMode;
        if (r.hasRSN && r.hasWPA) authMode = "[WPA-WPA2-PSK][ESS]";
        else if (r.hasRSN) authMode = "[WPA2-PSK][ESS]";
        else if (r.hasWPA) authMode = "[WPA-PSK][ESS]";
        else if (r.privacy) authMode = "[WEP][ESS]";
        else authMode = "[OPEN][ESS]";

        _addWigleRecord(storage, String(r.name), String(addrStr),
                         authMode, r.rssi, r.channel);
        _totalDiscovered++;
      }

      const char* displayName = r.name[0] ? r.name : "<hidden>";
      _addRecentFind(displayName, addrStr, r.rssi, r.channel, r.isBle);
    }

    _wardriveState = WARDRIVE_SCANNING;
  }

  // Record start position on first valid GPS fix
  if (!_hasStartPos && gps.location.isValid()) {
    _startLat = gps.location.lat();
    _startLng = gps.location.lng();
    _hasStartPos = true;
    RandomSeed::reseed();
  }
}

void GPSModule::endWardrive() {
  if (_scanMode != SCAN_BLE_ONLY) {
    if (_wardriveMode == MODE_DRIVING) _stopActiveScan();
    else _stopPromiscuous();
  }
  if (_scanMode != SCAN_WIFI_ONLY) _stopBleScan();

  _wardriveState = WARDRIVE_IDLE;
  _lastError = "";
  _filename = "";
  _totalDiscovered = 0;
  _totalBleDiscovered = 0;
  _hasStartPos = false;
  _wifiCount = 0;
  _wifiHead = 0;
  _bleCount = 0;
  _bleHead = 0;
  _recentCount = 0;
  _recentHead = 0;
  _wardriveStartTime = 0;
  s_scanCount = 0;

  if (_scanMode != SCAN_BLE_ONLY) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
}