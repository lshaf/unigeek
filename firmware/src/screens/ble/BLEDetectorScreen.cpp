#include "BLEDetectorScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/ble/BLEMenuScreen.h"

BLEDetectorScreen* BLEDetectorScreen::_instance = nullptr;

static void scanDoneCB(NimBLEScanResults) {}

// ── Spam patterns (manufacturer data hex matching) ──────────────────────────

const BLEDetectorScreen::SpamPattern BLEDetectorScreen::_patterns[] = {
  {"4c0007190_______________00_____", "Apple Popup"},
  {"4c000f05c0_____________________", "Apple Action"},
  {"4c00071907_____________________", "Apple Connect"},
  {"4c0004042a0000000f05c1__604c950", "Apple Setup"},
  {"2cfe___________________________", "Android Connect"},
  {"750000000000000000000000000000_", "Samsung Buds"},
  {"7500010002000101ff000043_______", "Samsung Watch"},
  {"0600030080_____________________", "Windows Swift"},
  {"ff006db643ce97fe427c___________", "Love Toys"},
};
const int BLEDetectorScreen::_patternCount =
  sizeof(_patterns) / sizeof(_patterns[0]);

// ── Skimmer detection ────────────────────────────────────────────────────────

static const char* const kSkimmerNames[] = {
  "HC-03", "HC-05", "HC-06", "HC-08", "BT04-A", "BT05"
};
static const int kSkimmerNameCount = sizeof(kSkimmerNames) / sizeof(kSkimmerNames[0]);

static const char* const kSkimmerMacPrefixes[] = {
  "00:11:22", "00:18:E4", "20:16:04",
  "00:11:22", "00:18:e4", "20:16:04"   // lowercase variants
};
static const int kSkimmerMacPrefixCount = sizeof(kSkimmerMacPrefixes) / sizeof(kSkimmerMacPrefixes[0]);

// ── Flipper Zero service UUIDs ──────────────────────────────────────────────

static const NimBLEUUID kFlipperWhite("00003082-0000-1000-8000-00805f9b34fb");
static const NimBLEUUID kFlipperBlack("00003081-0000-1000-8000-00805f9b34fb");
static const NimBLEUUID kFlipperTrans("00003083-0000-1000-8000-00805f9b34fb");

// ── BitChat service UUIDs ──────────────────────────────────────────────────

static const NimBLEUUID kBitChatMain("f47b5e2d-4a9e-4c5a-9b3f-8e1d2c3a4b5c");
static const NimBLEUUID kBitChatTest("f47b5e2d-4a9e-4c5a-9b3f-8e1d2c3a4b5a");

// ── Scan callbacks ──────────────────────────────────────────────────────────

class BLEDetectorScreen::ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* dev) override {
    if (_instance) _instance->_onDevice(dev);
  }
};

// ── Lifecycle ───────────────────────────────────────────────────────────────

BLEDetectorScreen::~BLEDetectorScreen()
{
  _stopScan();
  if (_instance == this) _instance = nullptr;
}

void BLEDetectorScreen::onInit()
{
  _instance = this;
  _startScan();
  _draw();
}

void BLEDetectorScreen::onUpdate()
{
  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _stopScan();
      Screen.setScreen(new BLEMenuScreen());
      return;
    }
    if (dir == INavigation::DIR_UP && _scrollOffset > 0) {
      _scrollOffset--;
      render();
    }
    if (dir == INavigation::DIR_DOWN) {
      int total = _deviceCount + _alertCount;
      int visible = bodyH() / 14;
      if (_scrollOffset + visible < total + 2) {
        _scrollOffset++;
        render();
      }
    }
  }

  // Re-trigger scan if previous one finished
  if (_scanning && _bleScan && !_bleScan->isScanning()) {
    _bleScan->clearResults();
    _bleScan->start(1, scanDoneCB);
  }

  uint32_t now = millis();
  if (now - _lastDrawMs >= 500) {
    _purgeOld();
    _lastDrawMs = now;
    render();
  }
}

void BLEDetectorScreen::onRender()
{
  _draw();
}

// ── Private ─────────────────────────────────────────────────────────────────

void BLEDetectorScreen::_startScan()
{
  int n = Achievement.inc("ble_detector_first");
  if (n == 1) Achievement.unlock("ble_detector_first");

  NimBLEDevice::init("");
  _bleScan = NimBLEDevice::getScan();
  _bleScan->setAdvertisedDeviceCallbacks(new ScanCallbacks(), true);
  _bleScan->setActiveScan(true);
  _bleScan->setInterval(100);
  _bleScan->setWindow(99);
  _bleScan->start(1, scanDoneCB);  // non-blocking 1s scan
  _scanning = true;
}

void BLEDetectorScreen::_stopScan()
{
  if (_bleScan) {
    _bleScan->stop();
    _bleScan = nullptr;
  }
  NimBLEDevice::deinit(true);
  _scanning = false;
}

void BLEDetectorScreen::_purgeOld()
{
  unsigned long now = millis();
  for (int i = 0; i < _deviceCount; ) {
    if (now - _devices[i].lastSeen > 5000) {
      _devices[i] = _devices[_deviceCount - 1];
      _deviceCount--;
    } else {
      i++;
    }
  }
}

void BLEDetectorScreen::_onDevice(NimBLEAdvertisedDevice* dev)
{
  const char* devType = nullptr;
  bool spoofed = false;
  float distance = 0;

  // Check for Flipper Zero
  if (dev->isAdvertisingService(kFlipperWhite))      devType = "Flipper White";
  else if (dev->isAdvertisingService(kFlipperBlack)) devType = "Flipper Black";
  else if (dev->isAdvertisingService(kFlipperTrans)) devType = "Flipper Trans";

  // Check for BitChat
  if (!devType) {
    if (dev->isAdvertisingService(kBitChatMain))      devType = "BitChat";
    else if (dev->isAdvertisingService(kBitChatTest)) devType = "BitChat Test";
  }

  // Check manufacturer data against spam patterns + AirTag
  if (dev->getManufacturerDataCount() > 0) {
    std::string mfr = dev->getManufacturerData(0);
    const uint8_t* payload = reinterpret_cast<const uint8_t*>(mfr.data());
    size_t len = mfr.size();

    // Spam pattern matching
    for (int i = 0; i < _patternCount; i++) {
      if (_matchPattern(_patterns[i].pattern, payload, len)) {
        _pushAlert(_patterns[i].type);
        break;
      }
    }

    // AirTag / FindMy detection: 4C 00 12 19
    if (len >= 4 && payload[0] == 0x4C && payload[1] == 0x00 &&
        payload[2] == 0x12 && payload[3] == 0x19) {
      if (!devType) devType = "AirTag";
      // Distance estimation
      int8_t txPower = -59;
      int rssi = dev->getRSSI();
      distance = pow(10.0f, (float)(txPower - rssi) / 20.0f);
    }
  }

  // Check for skimmer by name
  std::string name = dev->getName();
  if (!devType && name.length() > 0) {
    for (int i = 0; i < kSkimmerNameCount; i++) {
      if (name == kSkimmerNames[i]) {
        devType = "Skimmer";
        break;
      }
    }
  }

  // Check for skimmer by MAC prefix
  char mac[18];
  snprintf(mac, sizeof(mac), "%s", dev->getAddress().toString().c_str());
  if (!devType) {
    for (int i = 0; i < kSkimmerMacPrefixCount; i++) {
      if (strncmp(mac, kSkimmerMacPrefixes[i], 8) == 0) {
        devType = "Skimmer";
        break;
      }
    }
  }

  if (!devType) return; // Not a device we care about

  // Alert on skimmer detection
  if (strcmp(devType, "Skimmer") == 0) {
    _pushAlert("Skimmer Found!");
  }

  // Check Flipper OUI for spoofed status
  if (strncmp(devType, "Flipper", 7) == 0) {
    spoofed = true;
    if (strncmp(mac, "80:e1:26", 8) == 0 || strncmp(mac, "80:e1:27", 8) == 0 ||
        strncmp(mac, "0c:fa:22", 8) == 0 || strncmp(mac, "80:E1:26", 8) == 0 ||
        strncmp(mac, "80:E1:27", 8) == 0 || strncmp(mac, "0C:FA:22", 8) == 0) {
      spoofed = false;
    }
  }

  // Add/update device
  int idx = _findByMac(mac);
  if (idx < 0) {
    if (_deviceCount >= kMaxDevices) return;
    idx = _deviceCount++;
  }

  auto& d = _devices[idx];
  strncpy(d.mac, mac, sizeof(d.mac) - 1);
  d.mac[sizeof(d.mac) - 1] = '\0';

  if (name.length() > 0) {
    strncpy(d.name, name.c_str(), sizeof(d.name) - 1);
    d.name[sizeof(d.name) - 1] = '\0';
  } else {
    strncpy(d.name, devType, sizeof(d.name) - 1);
    d.name[sizeof(d.name) - 1] = '\0';
  }

  strncpy(d.type, devType, sizeof(d.type) - 1);
  d.type[sizeof(d.type) - 1] = '\0';
  d.prevRssi = d.rssi;
  d.rssi = dev->getRSSI();
  d.spoofed = spoofed;
  d.distance = distance;
  d.lastSeen = millis();
}

int BLEDetectorScreen::_findByMac(const char* mac)
{
  for (int i = 0; i < _deviceCount; i++) {
    if (strcmp(_devices[i].mac, mac) == 0) return i;
  }
  return -1;
}

bool BLEDetectorScreen::_matchPattern(const char* pattern, const uint8_t* data, size_t len)
{
  size_t patLen = strlen(pattern);
  for (size_t i = 0, j = 0; i < patLen && j < len; i += 2, j++) {
    if (pattern[i] == '_' && pattern[i + 1] == '_') continue;
    char hex[3] = {pattern[i], pattern[i + 1], 0};
    uint8_t val = (uint8_t)strtoul(hex, nullptr, 16);
    if (data[j] != val) return false;
  }
  return true;
}

void BLEDetectorScreen::_pushAlert(const char* type)
{
  int idx = _alertHead;
  strncpy(_alerts[idx].type, type, sizeof(_alerts[idx].type) - 1);
  _alerts[idx].type[sizeof(_alerts[idx].type) - 1] = '\0';
  _alerts[idx].timestamp = millis();
  _alertHead = (_alertHead + 1) % kMaxAlerts;
  if (_alertCount < kMaxAlerts) _alertCount++;

  int na = Achievement.inc("ble_spam_detected");
  if (na == 1) Achievement.unlock("ble_spam_detected");

  if (Uni.Speaker) Uni.Speaker->playNotification();
}

void BLEDetectorScreen::_draw()
{
  Sprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);
  sp.setTextDatum(TL_DATUM);

  int y = 2;
  int row = 0;
  int visibleRows = bodyH() / 14;

  // Header: counts by type
  if (row >= _scrollOffset && row < _scrollOffset + visibleRows) {
    sp.setTextColor(TFT_CYAN, TFT_BLACK);
    char hdr[64];
    int flp = 0, skim = 0, atag = 0, bchat = 0;
    for (int i = 0; i < _deviceCount; i++) {
      if (strncmp(_devices[i].type, "Flipper", 7) == 0) flp++;
      else if (strcmp(_devices[i].type, "Skimmer") == 0) skim++;
      else if (strcmp(_devices[i].type, "AirTag") == 0) atag++;
      else if (strncmp(_devices[i].type, "BitChat", 7) == 0) bchat++;
    }
    snprintf(hdr, sizeof(hdr), "F:%d S:%d A:%d B:%d", flp, skim, atag, bchat);
    sp.drawString(hdr, 2, y);
    y += 14;
  }
  row++;

  // Device list
  for (int i = 0; i < _deviceCount; i++, row++) {
    if (row < _scrollOffset) continue;
    if (row >= _scrollOffset + visibleRows) break;

    auto& d = _devices[i];

    // Color dot by type
    uint16_t dotColor = TFT_WHITE;
    if (strncmp(d.type, "Flipper", 7) == 0)
      dotColor = d.spoofed ? TFT_RED : TFT_GREEN;
    else if (strcmp(d.type, "Skimmer") == 0)
      dotColor = TFT_RED;
    else if (strcmp(d.type, "AirTag") == 0)
      dotColor = TFT_BLUE;
    else if (strncmp(d.type, "BitChat", 7) == 0)
      dotColor = TFT_MAGENTA;
    sp.fillCircle(4, y + 3, 3, dotColor);

    // Name or type (truncated)
    sp.setTextColor(TFT_WHITE, TFT_BLACK);
    char label[24];
    if (strcmp(d.type, "AirTag") == 0) {
      // Show distance for AirTags
      char trend = '=';
      int delta = d.rssi - d.prevRssi;
      if (delta > 3) trend = '+';
      else if (delta < -3) trend = '-';
      snprintf(label, sizeof(label), "AT %.1fm %c", d.distance, trend);
    } else {
      snprintf(label, sizeof(label), "%.12s", d.name);
    }
    sp.drawString(label, 12, y);

    // RSSI on right
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    char rssiStr[8];
    snprintf(rssiStr, sizeof(rssiStr), "%ddBm", d.rssi);
    sp.setTextDatum(TR_DATUM);
    sp.drawString(rssiStr, bodyW() - 2, y);
    sp.setTextDatum(TL_DATUM);

    y += 14;
  }

  // Spam alerts section
  if (_alertCount > 0) {
    if (row >= _scrollOffset && row < _scrollOffset + visibleRows) {
      sp.setTextColor(TFT_RED, TFT_BLACK);
      sp.drawString("-- Spam Alerts --", 2, y);
      y += 14;
    }
    row++;

    int shown = min(_alertCount, 5);
    for (int i = 0; i < shown; i++, row++) {
      if (row < _scrollOffset) continue;
      if (row >= _scrollOffset + visibleRows) break;

      int idx = (_alertHead - _alertCount + i + kMaxAlerts) % kMaxAlerts;
      sp.setTextColor(TFT_YELLOW, TFT_BLACK);
      sp.drawString(_alerts[idx].type, 4, y);

      unsigned long ago = (millis() - _alerts[idx].timestamp) / 1000;
      char ageStr[8];
      snprintf(ageStr, sizeof(ageStr), "%lus", ago);
      sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
      sp.setTextDatum(TR_DATUM);
      sp.drawString(ageStr, bodyW() - 2, y);
      sp.setTextDatum(TL_DATUM);

      y += 14;
    }
  }

  // Empty state
  if (_deviceCount == 0 && _alertCount == 0) {
    sp.setTextDatum(MC_DATUM);
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sp.drawString("Scanning...", bodyW() / 2, bodyH() / 2);
  }

  // Footer
  sp.setTextDatum(BC_DATUM);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  #ifdef DEVICE_HAS_KEYBOARD
    sp.drawString("BACK: Exit", bodyW() / 2, bodyH() - 2);
  #else
    sp.fillRect(0, bodyH() - 16, bodyW(), 16, Config.getThemeColor());
    sp.setTextColor(TFT_WHITE, Config.getThemeColor());
    sp.drawString("< Back", bodyW() / 2, bodyH() - 3);
  #endif

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}
