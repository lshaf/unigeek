#include "BLEDetectorScreen.h"
#include "BLEAnalyzerScreen.h"
#include "BLEFoxHuntScreen.h"
#include "ui/actions/InputSelectAction.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "core/ConfigManager.h"
#include "screens/ble/BLEMenuScreen.h"

namespace {
// List geometry of Traffic Analyzer: 19 px rows from 2 px below the body top,
// a 27 px two-line footer, and a 3 px scrollbar on the right when needed.
constexpr int kRowH    = 19;
constexpr int kTop     = 2;
constexpr int kFooterH = 27;

void drawListScrollBar(int x, int y, int height, int total, int visible, int offset) {
  if (height <= 0) return;
  auto& lcd = Uni.Lcd;
  if (total <= visible || visible <= 0) {
    lcd.fillRect(x, y, 3, height, TFT_BLACK);
    return;
  }
  lcd.fillRect(x, y, 3, height, 0x2104);
  const int thumb = min(height, max(4, height * visible / total));
  const int top = y + (height - thumb) * offset / (total - visible);
  lcd.fillRect(x, top, 3, thumb, Config.getThemeColor());
}
}  // namespace

BLEDetectorScreen* BLEDetectorScreen::_instance = nullptr;

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
  _paused = false;
  _startScan();
  _draw();
}

void BLEDetectorScreen::onUpdate()
{
  // Child screens share the NimBLE scanner. Reattach our callback when
  // returning, but keep the frozen selection until Back resumes scanning.
  if (_returningFromAction) {
    _returningFromAction = false;
    _bleScan = NimBLEDevice::getScan();
    if (_bleScan) {
      _bleScan->stop();
      _bleScan->setAdvertisedDeviceCallbacks(nullptr, false);
    }
    _chromeDrawn = false;
    render();
  }

  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (_paused) {
      if (dir == INavigation::DIR_BACK) {
        _resume();
        return;
      }
      if (dir == INavigation::DIR_PRESS && _deviceCount > 0) {
        _showOptions();
        return;
      }
      if (dir == INavigation::DIR_UP && _selected > 0) --_selected;
      if (dir == INavigation::DIR_DOWN && _selected + 1 < _deviceCount) ++_selected;
      _ensureSelectedVisible();
      _chromeDrawn = false;
      render();
    } else {
      if (dir == INavigation::DIR_PRESS) {
        _pause();
        return;
      }
      if (dir == INavigation::DIR_BACK) {
        _stopScan();
        Screen.goBack();
        return;
      }
      if (dir == INavigation::DIR_UP && _scrollOffset > 0) --_scrollOffset;
      if (dir == INavigation::DIR_DOWN) {
        int total = _deviceCount + (_alertCount ? min(_alertCount, 5) + 1 : 0) + 1;
        int visible = max(1, (bodyH() - kTop - kFooterH) / kRowH);
        if (_scrollOffset + visible < total) ++_scrollOffset;
      }
      _chromeDrawn = false;
      render();
    }
  }

  if (_scanning && _bleScan && !_bleScan->isScanning())
    _bleScan->start(0, nullptr, false);

  uint32_t now = millis();
  if (!_paused && now - _lastDrawMs >= 500) {
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
  static ScanCallbacks callbacks;
  _bleScan->setAdvertisedDeviceCallbacks(&callbacks, true);
  _bleScan->setActiveScan(true);
  _bleScan->setInterval(100);
  _bleScan->setWindow(99);
  _bleScan->setMaxResults(0);
  _bleScan->start(0, nullptr, false);
  _scanning = true;
}

void BLEDetectorScreen::_stopScan()
{
  if (_bleScan) {
    _bleScan->stop();
    _bleScan->setAdvertisedDeviceCallbacks(nullptr, false);
    _bleScan->setMaxResults(0xFF);
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
      _snapshots[i] = _snapshots[_deviceCount - 1];
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

  // BLE spam (shared with Watchdog): alert once a type comes from several
  // random addresses, then at most every 30 s while the attack goes on.
  if (const char* spam = BleSpam::signatureType(dev)) {
    const unsigned long now = millis();
    const std::string addr = dev->getAddress().toString();
    if (_spamTracker.observe(spam, addr.c_str(), BleSpam::payloadHash(dev), now)) {
      AlertCooldown* slot = nullptr;
      for (auto& c : _cooldown)
        if (c.type == spam || !c.type) { slot = &c; break; }
      if (slot && (!slot->type || now - slot->at >= 30000UL)) {
        slot->type = spam;
        slot->at = now;
        _pushAlert(spam);
      }
    }
  }

  // AirTag / Find My
  if (dev->getManufacturerDataCount() > 0) {
    std::string mfr = dev->getManufacturerData(0);
    const uint8_t* payload = reinterpret_cast<const uint8_t*>(mfr.data());
    size_t len = mfr.size();

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
    _pushAlert("Skimmer found");
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

  _snapshots[idx] = *dev;
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

void BLEDetectorScreen::_pause()
{
  if (_bleScan) _bleScan->stop();
  _scanning = false;
  _paused = true;
  _selected = 0;
  _scrollOffset = 0;
  _chromeDrawn = false;
  render();
}

void BLEDetectorScreen::_resume()
{
  _paused = false;
  _scrollOffset = 0;
  _chromeDrawn = false;
  if (!_bleScan) _bleScan = NimBLEDevice::getScan();
  if (_bleScan) {
    static ScanCallbacks callbacks;
    _bleScan->setAdvertisedDeviceCallbacks(&callbacks, true);
    _bleScan->setActiveScan(true);
    _bleScan->setInterval(100);
    _bleScan->setWindow(99);
    _bleScan->setMaxResults(0);
    _bleScan->start(0, nullptr, false);
    _scanning = true;
  }
  render();
}

void BLEDetectorScreen::_ensureSelectedVisible()
{
  const int visible = max(1, (bodyH() - kTop - kFooterH) / kRowH);
  const int row = _selected + 1; // counts header is row zero
  if (row < _scrollOffset) _scrollOffset = row;
  if (row >= _scrollOffset + visible) _scrollOffset = row - visible + 1;
}

void BLEDetectorScreen::_showOptions()
{
  if (_selected < 0 || _selected >= _deviceCount) return;
  static const InputSelectAction::Option options[] = {
    {"Details", "details", nullptr, 0, false},
    {"Fox Hunt", "fox", nullptr, 0, false},
  };
  const char* choice = InputSelectAction::popup("Options", options, 2);
  if (!choice) { _chromeDrawn = false; render(); return; }

  // Copy the snapshot before opening a child, which may reuse NimBLE buffers.
  NimBLEAdvertisedDevice target = _snapshots[_selected];
  if (strcmp(choice, "details") == 0) {
    auto* details = new BLEAnalyzerScreen();
    details->configureTarget(target);
    _returningFromAction = true;
    Screen.push(details);
  } else if (strcmp(choice, "fox") == 0) {
    auto* fox = new BLEFoxHuntScreen();
    fox->configureTarget(target);
    _returningFromAction = true;
    Screen.push(fox);
  }
}

int BLEDetectorScreen::_findByMac(const char* mac)
{
  for (int i = 0; i < _deviceCount; i++) {
    if (strcmp(_devices[i].mac, mac) == 0) return i;
  }
  return -1;
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
  auto& lcd = Uni.Lcd;

  const int rowH = kRowH;
  const int contentY = bodyY();
  const int visibleRows = max(1, (bodyH() - kTop - kFooterH) / rowH);
  const int contentH = visibleRows * rowH;
  const int totalRows = 1 + _deviceCount + (_alertCount ? min(_alertCount, 5) + 1 : 0);
  const bool hasScrollbar = totalRows > visibleRows;
  const int rightEdge = bodyW() - (hasScrollbar ? 7 : 3);  // sprite coordinates
  const int ty = kTop;  // text offset inside a row, as in Traffic Analyzer

  if (!_chromeDrawn) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    _chromeDrawn = true;
  }

  // Same footer as Traffic Analyzer and the Wi-Fi Watchdog / Watchcat: two
  // lines, the Options hint on the upper one, Pause / Resume on the lower.
  // The upper line stays reserved while live, so content doesn't jump.
  const int footerTop = bodyY() + bodyH() - 27;
  lcd.fillRect(bodyX(), footerTop, bodyW(), 27, TFT_BLACK);
  lcd.drawFastHLine(bodyX(), footerTop, bodyW(), TFT_DARKGREY);
  lcd.setTextDatum(BC_DATUM);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  if (_paused) {
    const char* resume = "[Back] Resume Scan";
    if (lcd.textWidth(resume) > bodyW() - 6) resume = "[Back] Resume";
    lcd.drawString("[Press] Options", bodyX() + bodyW() / 2,
                   bodyY() + bodyH() - 14);
    lcd.drawString(resume, bodyX() + bodyW() / 2,
                   bodyY() + bodyH() - 1);
  } else {
    lcd.drawString("[Press] Pause", bodyX() + bodyW() / 2,
                   bodyY() + bodyH() - 1);
  }
  lcd.setTextDatum(TL_DATUM);

  // Empty state — single centered sprite, clear surrounding rows.
  if (_deviceCount == 0 && _alertCount == 0) {
    const int spH = rowH;
    const int spY = contentY + (contentH - spH) / 2;
    Sprite sp(&Uni.Lcd);
    sp.createSprite(bodyW(), spH);
    sp.fillSprite(TFT_BLACK);
    sp.setTextDatum(MC_DATUM);
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sp.drawString(_paused ? "Nothing captured" : "Monitoring...", bodyW() / 2, spH / 2);
    sp.pushSprite(bodyX(), spY);
    sp.deleteSprite();
    if (spY > contentY)
      lcd.fillRect(bodyX(), contentY, bodyW(), spY - contentY, TFT_BLACK);
    int below = (contentY + contentH) - (spY + spH);
    if (below > 0)
      lcd.fillRect(bodyX(), spY + spH, bodyW(), below, TFT_BLACK);
    return;
  }

  // Build list of rows in scroll order, then draw the visible window into a
  // single content sprite. Per-row sprites would be cleaner but require a
  // helper, and a single content sprite at bodyW × contentH stays well within
  // RAM budget for this screen's content area (~ a few KB).
  Sprite sp(&Uni.Lcd);
  sp.createSprite(bodyW() - 3, contentH);  // scrollbar column drawn separately
  sp.fillSprite(TFT_BLACK);
  sp.setTextSize(1);

  int row = 0;
  int rowY = 0;
  auto rowVisible = [&]() {
    return row >= _scrollOffset && row < _scrollOffset + visibleRows;
  };
  auto advance = [&]() { rowY += rowH; };

  // Header: counts by type
  if (rowVisible()) {
    int flp = 0, skim = 0, atag = 0, bchat = 0;
    for (int i = 0; i < _deviceCount; i++) {
      if (strncmp(_devices[i].type, "Flipper", 7) == 0) flp++;
      else if (strcmp(_devices[i].type, "Skimmer") == 0) skim++;
      else if (strcmp(_devices[i].type, "AirTag") == 0) atag++;
      else if (strncmp(_devices[i].type, "BitChat", 7) == 0) bchat++;
    }
    char hdr[64];
    snprintf(hdr, sizeof(hdr), "F:%d S:%d A:%d B:%d", flp, skim, atag, bchat);
    sp.setTextDatum(TL_DATUM);
    sp.setTextColor(TFT_CYAN, TFT_BLACK);
    sp.drawString(hdr, 3, rowY + ty);
    advance();
  }
  row++;

  // Device list
  for (int i = 0; i < _deviceCount; i++, row++) {
    if (!rowVisible()) continue;
    auto& d = _devices[i];

    uint16_t dotColor = TFT_WHITE;
    if (strncmp(d.type, "Flipper", 7) == 0)
      dotColor = d.spoofed ? TFT_RED : TFT_GREEN;
    else if (strcmp(d.type, "Skimmer") == 0) dotColor = TFT_RED;
    else if (strcmp(d.type, "AirTag")  == 0) dotColor = TFT_BLUE;
    else if (strncmp(d.type, "BitChat", 7) == 0) dotColor = TFT_MAGENTA;
    // Selection as in the other lists: theme-coloured rounded row.
    const bool selected = _paused && i == _selected;
    const uint16_t bg = selected ? Config.getThemeColor() : TFT_BLACK;
    if (selected) sp.fillRoundRect(0, rowY, rightEdge, rowH, 3, bg);
    sp.fillCircle(6, rowY + rowH / 2, 3, dotColor);

    char label[24];
    if (strcmp(d.type, "AirTag") == 0) {
      char trend = '=';
      int delta = d.rssi - d.prevRssi;
      if (delta > 3) trend = '+';
      else if (delta < -3) trend = '-';
      snprintf(label, sizeof(label), "AT %.1fm %c", d.distance, trend);
    } else {
      snprintf(label, sizeof(label), "%s", d.name);
    }

    // Name in white, signal in dark grey (white on the selected row), as in
    // Traffic Analyzer; the name uses the width left by the signal.
    char rssiStr[8];
    snprintf(rssiStr, sizeof(rssiStr), "%ddBm", d.rssi);
    sp.setTextDatum(TR_DATUM);
    sp.setTextColor(selected ? TFT_WHITE : TFT_DARKGREY, bg);
    sp.drawString(rssiStr, rightEdge - 3, rowY + ty);

    const int labelMax = rightEdge - 3 - sp.textWidth(rssiStr) - 6 - 14;
    size_t n = strlen(label);
    while (n > 1 && sp.textWidth(String(label).substring(0, n).c_str()) > labelMax) n--;
    label[n] = '\0';
    sp.setTextDatum(TL_DATUM);
    sp.setTextColor(TFT_WHITE, bg);
    sp.drawString(label, 14, rowY + ty);

    advance();
  }

  // Spam alerts section
  if (_alertCount > 0) {
    if (rowVisible()) {
      sp.setTextDatum(TL_DATUM);
      sp.setTextColor(TFT_RED, TFT_BLACK);
      sp.drawString("-- Spam Alerts --", 3, rowY + ty);
      advance();
    }
    row++;

    int shown = min(_alertCount, 5);
    for (int i = 0; i < shown; i++, row++) {
      if (!rowVisible()) continue;
      int idx = (_alertHead - _alertCount + i + kMaxAlerts) % kMaxAlerts;
      sp.setTextDatum(TL_DATUM);
      sp.setTextColor(TFT_YELLOW, TFT_BLACK);
      sp.drawString(_alerts[idx].type, 14, rowY + ty);

      unsigned long ago = (millis() - _alerts[idx].timestamp) / 1000;
      char ageStr[8];
      snprintf(ageStr, sizeof(ageStr), "%lus", ago);
      sp.setTextDatum(TR_DATUM);
      sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
      sp.drawString(ageStr, rightEdge - 3, rowY + ty);

      advance();
    }
  }

  sp.pushSprite(bodyX(), contentY);
  sp.deleteSprite();
  drawListScrollBar(bodyX() + bodyW() - 3, contentY, contentH, totalRows, visibleRows, _scrollOffset);
}
