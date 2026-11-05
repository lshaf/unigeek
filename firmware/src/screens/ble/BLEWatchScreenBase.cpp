#include "BLEWatchScreenBase.h"

#include "core/ConfigManager.h"
#include "core/Device.h"
#include "core/ScreenManager.h"

#include <algorithm>
#include <cctype>
#include <cstring>

BLEWatchScreenBase* BLEWatchScreenBase::_instance = nullptr;

static const NimBLEUUID kFlipperWhite("00003082-0000-1000-8000-00805f9b34fb");
static const NimBLEUUID kFlipperBlack("00003081-0000-1000-8000-00805f9b34fb");
static const NimBLEUUID kFlipperTrans("00003083-0000-1000-8000-00805f9b34fb");
static const NimBLEUUID kSmartTagService("0000fd5a-0000-1000-8000-00805f9b34fb");
static const NimBLEUUID kSmartTagUnregisteredService("0000fd59-0000-1000-8000-00805f9b34fb");
static const NimBLEUUID kTileService("0000feed-0000-1000-8000-00805f9b34fb");

struct BleSpamPattern {
  const char* pattern;
  const char* type;
};

static const BleSpamPattern kSpamPatterns[] = {
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

class BLEWatchScreenBase::ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* dev) override {
    if (_instance) _instance->_onDevice(dev);
  }
};

BLEWatchScreenBase::~BLEWatchScreenBase()
{
  if (_instance == this) _instance = nullptr;
  _stopScan();
  if (_stateMutex) {
    vSemaphoreDelete(_stateMutex);
    _stateMutex = nullptr;
  }
#ifdef DEVICE_HAS_TOUCH_NAV
  Uni.Nav->setSuppressKeys(false);
#endif
}

const char* BLEWatchScreenBase::title()
{
  static constexpr const char* kDogTitles[] = {
    "Watchdog", "BLE Spam", "Suspicious Advertisers", "ESP32 Devices", "Flipper Zero"
  };
  static constexpr const char* kCatTitles[] = {
    "Watchcat", "Find My", "SmartTag", "Tile", "iBeacon"
  };
  return (_mode == Mode::Watchdog ? kDogTitles : kCatTitles)[_view];
}

void BLEWatchScreenBase::onInit()
{
  if (!_stateMutex) _stateMutex = xSemaphoreCreateRecursiveMutex();
  _instance = this;
  _view = VIEW_OVERALL;
  _gridSel = 0;
  _prevGridSel = -1;
  _holdCell = -1;
  _entryCount = 0;
  _activityCount = 0;
  _itemCount = 0;
  _lastUpdate = millis();
  for (int i = 0; i < 4; ++i) _prevCounts[i] = -1;
  _scroll.resetScroll();

#ifdef DEVICE_HAS_TOUCH_NAV
  Uni.Nav->setSuppressKeys(true);
#endif

  _startScan();
  render();
}

void BLEWatchScreenBase::_startScan()
{
  NimBLEDevice::init("");
  _bleScan = NimBLEDevice::getScan();
  static ScanCallbacks callbacks;
  _bleScan->setMaxResults(0);
  _bleScan->setAdvertisedDeviceCallbacks(&callbacks, true);
  _bleScan->setActiveScan(true);
  _bleScan->setInterval(100);
  _bleScan->setWindow(99);
  _bleScan->start(0, nullptr, false);
  _scanning = true;
}

void BLEWatchScreenBase::_stopScan()
{
  if (_bleScan) {
    _bleScan->stop();
    _bleScan->setAdvertisedDeviceCallbacks(nullptr, false);
    _bleScan->setMaxResults(0xFF);
    _bleScan = nullptr;
  }
  if (_scanning) NimBLEDevice::deinit(true);
  _scanning = false;
}

void BLEWatchScreenBase::_restartScanIfNeeded()
{
  if (_scanning && _bleScan && !_bleScan->isScanning()) {
    _bleScan->start(0, nullptr, false);
  }
}

void BLEWatchScreenBase::_enterView(View view)
{
#ifdef DEVICE_HAS_TOUCH_NAV
  Uni.Nav->setSuppressKeys(false);
#endif
  _holdCell = -1;
  _view = view;
  _itemCount = 0;
  _scroll.resetScroll();
  _renderView();
}

void BLEWatchScreenBase::onUpdate()
{
  _restartScanIfNeeded();

  if (_view == VIEW_OVERALL) {
    if (Uni.Nav->wasPressed()) {
      const auto dir = Uni.Nav->readDirection();
#ifdef DEVICE_HAS_TOUCH_NAV
      const int16_t tx = Uni.Nav->lastTouchX();
      const int16_t ty = Uni.Nav->lastTouchY();
      const int backW = bodyW() / 6;
      if (dir == INavigation::DIR_BACK || (tx >= 0 && (int)tx < (int)bodyX() + backW)) {
        _stopScan();
        Screen.goBack();
        return;
      }
      if (tx >= 0 && ty >= (int16_t)bodyY()) {
        const int gx = (int)tx - (int)bodyX() - backW;
        const int gw = bodyW() - backW;
        const int col = gx / (gw / 2);
        const int row = ((int)ty - (int)bodyY()) / (bodyH() / 2);
        if (col >= 0 && col < 2 && row >= 0 && row < 2) {
          _enterView((View)(VIEW_CAT0 + row * 2 + col));
          return;
        }
      }
#else
      if (dir == INavigation::DIR_BACK) {
        _stopScan();
        Screen.goBack();
        return;
      }
      const bool nav4 = Uni.Nav->is4Way();
      if (nav4 && (dir == INavigation::DIR_UP || dir == INavigation::DIR_DOWN)) {
        _gridSel ^= 2;
        _renderOverall();
        if (Uni.Speaker) Uni.Speaker->beep();
      } else if (nav4 && (dir == INavigation::DIR_LEFT || dir == INavigation::DIR_RIGHT)) {
        _gridSel ^= 1;
        _renderOverall();
        if (Uni.Speaker) Uni.Speaker->beep();
      } else if (dir == INavigation::DIR_LEFT || dir == INavigation::DIR_UP) {
        _gridSel = (_gridSel + 3) % 4;
        _renderOverall();
        if (Uni.Speaker) Uni.Speaker->beep();
      } else if (dir == INavigation::DIR_RIGHT || dir == INavigation::DIR_DOWN) {
        _gridSel = (_gridSel + 1) % 4;
        _renderOverall();
        if (Uni.Speaker) Uni.Speaker->beep();
      } else if (dir == INavigation::DIR_PRESS) {
        _enterView((View)(VIEW_CAT0 + _gridSel));
        return;
      }
#endif
    }
#ifdef DEVICE_HAS_TOUCH_NAV
    {
      const int backW = bodyW() / 6;
      if (Uni.Nav->isPressed()) {
        const int16_t tx = Uni.Nav->lastTouchX();
        const int16_t ty = Uni.Nav->lastTouchY();
        int newHold = -1;
        if (tx >= 0) {
          if ((int)tx < (int)bodyX() + backW) {
            newHold = 4;
          } else if (ty >= (int16_t)bodyY()) {
            const int gx = (int)tx - (int)bodyX() - backW;
            const int gw = bodyW() - backW;
            const int col = gx / (gw / 2);
            const int row = ((int)ty - (int)bodyY()) / (bodyH() / 2);
            if (col >= 0 && col < 2 && row >= 0 && row < 2) newHold = row * 2 + col;
          }
        }
        if (newHold != _holdCell) {
          const bool backChanged = (_holdCell == 4) || (newHold == 4);
          if (_holdCell >= 0 && _holdCell < 4) _prevCounts[_holdCell] = -1;
          if (newHold >= 0 && newHold < 4) _prevCounts[newHold] = -1;
          _holdCell = newHold;
          if (backChanged) _drawBackButton();
          _renderOverall();
        }
      } else if (_holdCell >= 0) {
        const bool backWasHeld = (_holdCell == 4);
        if (_holdCell < 4) _prevCounts[_holdCell] = -1;
        _holdCell = -1;
        if (backWasHeld) _drawBackButton();
        _renderOverall();
      }
    }
#endif
  } else if (Uni.Nav->wasPressed()) {
    const auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _view = VIEW_OVERALL;
      _prevGridSel = -1;
#ifdef DEVICE_HAS_TOUCH_NAV
      Uni.Nav->drawOverlay();
      Uni.Nav->setSuppressKeys(true);
#endif
      Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
      render();
      return;
    }
    if (dir == INavigation::DIR_UP || dir == INavigation::DIR_DOWN ||
        dir == INavigation::DIR_LEFT || dir == INavigation::DIR_RIGHT) {
      _scroll.onNav(dir);
    }
  }

  if (millis() - _lastUpdate >= 500) {
    _lastUpdate = millis();
    _prune();
    _renderView();
  }
}

void BLEWatchScreenBase::onRender()
{
  if (_view == VIEW_OVERALL) {
    _prevGridSel = -1;
    _renderOverall();
    return;
  }
  if (_itemCount > 0) {
    _scroll.render(bodyX(), bodyY(), bodyW(), bodyH());
  } else {
    Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    Uni.Lcd.setTextDatum(MC_DATUM);
    Uni.Lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    Uni.Lcd.drawString("Monitoring...", bodyX() + bodyW() / 2, bodyY() + bodyH() / 2);
  }
}

void BLEWatchScreenBase::_onDevice(NimBLEAdvertisedDevice* dev)
{
  if (!dev) return;
  char mac[18];
  snprintf(mac, sizeof(mac), "%s", dev->getAddress().toString().c_str());

  if (_stateMutex) xSemaphoreTakeRecursive(_stateMutex, portMAX_DELAY);
  if (_mode == Mode::Watchdog) _classifyWatchdog(dev, mac);
  else _classifyWatchcat(dev, mac);
  if (_stateMutex) xSemaphoreGiveRecursive(_stateMutex);
}

void BLEWatchScreenBase::_classifyWatchdog(NimBLEAdvertisedDevice* dev, const char* mac)
{
  bool known = false;
  const int8_t rssi = dev->getRSSI();

  if (dev->isAdvertisingService(kFlipperWhite) ||
      dev->isAdvertisingService(kFlipperBlack) ||
      dev->isAdvertisingService(kFlipperTrans)) {
    const char* variant = dev->isAdvertisingService(kFlipperWhite) ? "Flipper White" :
                          dev->isAdvertisingService(kFlipperBlack) ? "Flipper Black" : "Flipper Trans";
    _addOrUpdate(3, mac, variant, mac, rssi);
    known = true;
  }

  std::string name = dev->haveName() ? dev->getName() : std::string();
  if (!name.empty()) {
    const char* firmware = nullptr;
    if (_isNamed(name, "bruce")) firmware = "Bruce";
    else if (_containsNoCase(name, "evil-cardputer") || _containsNoCase(name, "evil cardputer"))
      firmware = "Evil-Cardputer";
    else if (_containsNoCase(name, "ghostesp") || _containsNoCase(name, "ghost esp")) firmware = "GhostESP";
    else if (_containsNoCase(name, "marauder")) firmware = "ESP32 Marauder";
    else if (_isNamed(name, "nemo")) firmware = "NEMO";
    else if (_containsNoCase(name, "unigeek")) firmware = "UniGeek";

    if (firmware) {
      _addOrUpdate(2, mac, firmware, mac, rssi);
      known = true;
    }
  }

  if (const char* spam = _spamType(dev)) {
    char key[32];
    snprintf(key, sizeof(key), "spam:%s", spam);
    _addOrUpdate(0, key, spam, mac, rssi, false, true);
    known = true;
  }

  _observeActivity(mac, rssi, known);
}

void BLEWatchScreenBase::_classifyWatchcat(NimBLEAdvertisedDevice* dev, const char* mac)
{
  const int8_t rssi = dev->getRSSI();
  bool findMy = false;
  bool iBeacon = false;
  bool samsungCid = false;
  bool tileCid = false;

  for (size_t i = 0; i < dev->getManufacturerDataCount(); ++i) {
    const std::string& m = dev->getManufacturerData(i);
    if (m.size() < 2) continue;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(m.data());
    const uint16_t cid = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    if (cid == 0x004C && m.size() >= 4) {
      if (p[2] == 0x12 && p[3] == 0x19) findMy = true;
      if (p[2] == 0x02 && p[3] == 0x15) iBeacon = true;
    }
    if (cid == 0x0075) samsungCid = true;
    if (cid == 0x023D) tileCid = true;
  }

  if (findMy) _addOrUpdate(0, mac, "Find My Device", mac, rssi);

  std::string name = dev->haveName() ? dev->getName() : std::string();
  const bool smartTagName = !name.empty() &&
    (_containsNoCase(name, "smarttag") || _containsNoCase(name, "smart tag") ||
     _containsNoCase(name, "galaxy tag"));
  if (dev->isAdvertisingService(kSmartTagService) ||
      dev->isAdvertisingService(kSmartTagUnregisteredService) ||
      (samsungCid && smartTagName)) {
    _addOrUpdate(1, mac, name.empty() ? "Samsung SmartTag" : name.c_str(), mac, rssi);
  }

  if (dev->isAdvertisingService(kTileService) || tileCid ||
      (!name.empty() && _isNamed(name, "tile"))) {
    _addOrUpdate(2, mac, name.empty() ? "Tile" : name.c_str(), mac, rssi);
  }

  if (iBeacon) {
    char label[32] = "iBeacon";
    for (size_t i = 0; i < dev->getManufacturerDataCount(); ++i) {
      const std::string& m = dev->getManufacturerData(i);
      if (m.size() >= 25) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(m.data());
        const uint16_t cid = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
        if (cid == 0x004C && p[2] == 0x02 && p[3] == 0x15) {
          const uint16_t major = ((uint16_t)p[20] << 8) | p[21];
          const uint16_t minor = ((uint16_t)p[22] << 8) | p[23];
          snprintf(label, sizeof(label), "iBeacon %u/%u", major, minor);
          break;
        }
      }
    }
    _addOrUpdate(3, mac, label, mac, rssi);
  }
}

BLEWatchScreenBase::Entry* BLEWatchScreenBase::_findEntry(uint8_t category, const char* key)
{
  for (int i = 0; i < _entryCount; ++i)
    if (_entries[i].category == category && strcmp(_entries[i].key, key) == 0) return &_entries[i];
  return nullptr;
}

void BLEWatchScreenBase::_addOrUpdate(uint8_t category, const char* key, const char* label,
                                      const char* mac, int8_t rssi, bool visible, bool burst)
{
  const unsigned long now = millis();
  Entry* e = _findEntry(category, key);
  if (!e) {
    if (_entryCount >= MAX_ENTRIES) {
      int oldest = 0;
      for (int i = 1; i < _entryCount; ++i)
        if (_entries[i].lastSeen < _entries[oldest].lastSeen) oldest = i;
      e = &_entries[oldest];
    } else {
      e = &_entries[_entryCount++];
    }
    *e = Entry{};
    e->category = category;
    strncpy(e->key, key, sizeof(e->key) - 1);
    strncpy(e->label, label, sizeof(e->label) - 1);
    e->firstSeen = now;
    e->burstStart = now;
    e->visible = visible;
  }

  if (mac) strncpy(e->mac, mac, sizeof(e->mac) - 1);
  e->rssi = rssi;
  e->lastSeen = now;
  if (e->count < 65535) ++e->count;

  if (burst) {
    if (now - e->burstStart > SPAM_WINDOW_MS) {
      e->burstStart = now;
      e->burstCount = 1;
    } else if (e->burstCount < 65535) {
      ++e->burstCount;
    }
    if (e->burstCount >= SPAM_THRESHOLD) e->visible = true;
  }
}

void BLEWatchScreenBase::_observeActivity(const char* mac, int8_t rssi, bool known)
{
  const unsigned long now = millis();
  Activity* a = nullptr;
  for (int i = 0; i < _activityCount; ++i) {
    if (strcmp(_activity[i].mac, mac) == 0) { a = &_activity[i]; break; }
  }
  if (!a) {
    if (_activityCount >= MAX_ACTIVITY) {
      int oldest = 0;
      for (int i = 1; i < _activityCount; ++i)
        if (_activity[i].lastSeen < _activity[oldest].lastSeen) oldest = i;
      a = &_activity[oldest];
    } else {
      a = &_activity[_activityCount++];
    }
    *a = Activity{};
    strncpy(a->mac, mac, sizeof(a->mac) - 1);
    a->windowStart = now;
  }
  a->lastSeen = now;
  a->rssi = rssi;
  if (now - a->windowStart > RATE_WINDOW_MS) {
    a->windowStart = now;
    a->packets = 1;
    return;
  }
  if (a->packets < 65535) ++a->packets;
  if (!known && a->packets >= SUSPICIOUS_RATE_THRESHOLD) {
    _addOrUpdate(1, mac, "High-rate advertiser", mac, rssi);
  }
}

void BLEWatchScreenBase::_prune()
{
  if (_stateMutex) xSemaphoreTakeRecursive(_stateMutex, portMAX_DELAY);
  const unsigned long now = millis();
  for (int i = 0; i < _entryCount; ) {
    if (now - _entries[i].lastSeen > WINDOW_MS) {
      _entries[i] = _entries[_entryCount - 1];
      --_entryCount;
    } else {
      ++i;
    }
  }
  for (int i = 0; i < _activityCount; ) {
    if (now - _activity[i].lastSeen > WINDOW_MS) {
      _activity[i] = _activity[_activityCount - 1];
      --_activityCount;
    } else {
      ++i;
    }
  }
  if (_stateMutex) xSemaphoreGiveRecursive(_stateMutex);
}

void BLEWatchScreenBase::_renderView()
{
  if (_view == VIEW_OVERALL) _renderOverall();
  else _renderCategory((uint8_t)(_view - VIEW_CAT0));
}

void BLEWatchScreenBase::_renderOverall()
{
  int counts[4] = {0, 0, 0, 0};
  if (_stateMutex) xSemaphoreTakeRecursive(_stateMutex, portMAX_DELAY);
  for (int i = 0; i < _entryCount; ++i)
    if (_entries[i].visible && _entries[i].category < 4) ++counts[_entries[i].category];
  if (_stateMutex) xSemaphoreGiveRecursive(_stateMutex);

  const bool forceAll = (_prevGridSel < 0);
#ifdef DEVICE_HAS_TOUCH_NAV
  if (forceAll) _drawBackButton();
  for (int i = 0; i < 4; ++i) {
    if (forceAll || counts[i] != _prevCounts[i]) {
      _drawGridCell(i, counts[i]);
      _prevCounts[i] = counts[i];
    }
  }
  _prevGridSel = 0;
#else
  for (int i = 0; i < 4; ++i) {
    const bool selDirty = (i == (int)_gridSel) != (i == _prevGridSel);
    if (forceAll || selDirty || counts[i] != _prevCounts[i]) {
      _drawGridCell(i, counts[i]);
      _prevCounts[i] = counts[i];
    }
  }
  _prevGridSel = (int)_gridSel;
#endif
}

void BLEWatchScreenBase::_renderCategory(uint8_t category)
{
  if (_stateMutex) xSemaphoreTakeRecursive(_stateMutex, portMAX_DELAY);
  Entry* items[MAX_ENTRIES];
  int itemN = 0;
  for (int i = 0; i < _entryCount; ++i)
    if (_entries[i].visible && _entries[i].category == category) items[itemN++] = &_entries[i];

  std::sort(items, items + itemN, [](const Entry* a, const Entry* b) {
    if (a->rssi != b->rssi) return a->rssi > b->rssi;
    return a->lastSeen > b->lastSeen;
  });

  int n = 0;
  for (int i = 0; i < itemN && n < MAX_ROWS; ++i) {
    const Entry& e = *items[i];
    snprintf(_labels[n], sizeof(_labels[n]), "%s", e.label);
    if (_mode == Mode::Watchdog && category == 0)
      snprintf(_values[n], sizeof(_values[n]), "%ddBm x%u", e.rssi, e.count);
    else
      snprintf(_values[n], sizeof(_values[n]), "%ddBm", e.rssi);
    _rows[n].label = _labels[n];
    _rows[n].value = _values[n];
    ++n;

    if (n < MAX_ROWS) {
      snprintf(_labels[n], sizeof(_labels[n]), "  %s", e.mac);
      _rows[n].label = _labels[n];
      _rows[n].value = "";
      ++n;
    }
  }
  if (_stateMutex) xSemaphoreGiveRecursive(_stateMutex);
  _setListState(n);
}

void BLEWatchScreenBase::_setListState(int count)
{
  _itemCount = count;
  _scroll.setRows(_rows, (uint8_t)_itemCount);
  render();
}

void BLEWatchScreenBase::_drawBackButton()
{
#ifdef DEVICE_HAS_TOUCH_NAV
  const int backW = bodyW() / 6;
  const bool held = (_holdCell == 4);
  Sprite back(&Uni.Lcd);
  back.createSprite(backW, bodyH());
  back.fillSprite(TFT_BLACK);
  back.drawRoundRect(2, 2, backW - 4, bodyH() - 4, 6, held ? Config.getThemeColor() : 0x2104);
  back.setTextDatum(MC_DATUM);
  back.setTextColor(held ? TFT_WHITE : TFT_DARKGREY, TFT_BLACK);
  back.drawString("<", backW / 2, bodyH() / 2);
  back.pushSprite(bodyX(), bodyY());
  back.deleteSprite();
#endif
}

void BLEWatchScreenBase::_drawGridCell(int idx, int count)
{
  static constexpr const char* kDogNames[] = {"BLE Spam", "Suspicious", "ESP32 Devices", "Flipper Zero"};
  static constexpr const char* kCatNames[] = {"Find My", "SmartTag", "Tile", "iBeacon"};
  const char* name = (_mode == Mode::Watchdog ? kDogNames : kCatNames)[idx];
#ifdef DEVICE_HAS_TOUCH_NAV
  const int backW = bodyW() / 6;
  const int gw = bodyW() - backW;
  const int cellW = gw / 2;
  const int cellH = bodyH() / 2;
  const int px = bodyX() + backW + (idx % 2) * cellW;
  const int py = bodyY() + (idx / 2) * cellH;
  const bool held = (idx == _holdCell);
#else
  const int cellW = bodyW() / 2;
  const int cellH = bodyH() / 2;
  const int px = bodyX() + (idx % 2) * cellW;
  const int py = bodyY() + (idx / 2) * cellH;
  const bool held = (idx == (int)_gridSel);
#endif

  Sprite sp(&Uni.Lcd);
  sp.createSprite(cellW, cellH);
  sp.fillSprite(TFT_BLACK);
  sp.drawRoundRect(2, 2, cellW - 4, cellH - 4, 4, held ? Config.getThemeColor() : 0x2104);
  sp.setTextSize(1);
  sp.setTextDatum(TC_DATUM);
  sp.setTextColor(held ? TFT_WHITE : TFT_LIGHTGREY, TFT_BLACK);
  sp.drawString(name, cellW / 2, 8);
  char buf[6];
  snprintf(buf, sizeof(buf), "%d", count);
  sp.setTextSize(2);
  sp.setTextDatum(MC_DATUM);
  const uint16_t activeCountColor = (_mode == Mode::Watchcat) ? TFT_GREEN : TFT_RED;
  sp.setTextColor(count > 0 ? activeCountColor : TFT_DARKGREY, TFT_BLACK);
  sp.drawString(buf, cellW / 2, cellH / 2 + 4);
  sp.setTextSize(1);
  sp.pushSprite(px, py);
  sp.deleteSprite();
}

bool BLEWatchScreenBase::_matchPattern(const char* pattern, const uint8_t* data, size_t len)
{
  if (!pattern || !data) return false;

  const size_t patLen = strlen(pattern);
  if (len * 2 < patLen) return false;

  auto hexNibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };

  for (size_t i = 0; i < patLen; ++i) {
    const char expected = pattern[i];
    if (expected == '_') continue;
    const int nibble = hexNibble(expected);
    if (nibble < 0) return false;
    const uint8_t actual = (i & 1) ? (data[i / 2] & 0x0F) : (data[i / 2] >> 4);
    if (actual != static_cast<uint8_t>(nibble)) return false;
  }
  return true;
}

const char* BLEWatchScreenBase::_spamType(NimBLEAdvertisedDevice* dev)
{
  for (size_t m = 0; m < dev->getManufacturerDataCount(); ++m) {
    const std::string& data = dev->getManufacturerData(m);
    const uint8_t* p = reinterpret_cast<const uint8_t*>(data.data());
    for (const auto& sig : kSpamPatterns)
      if (_matchPattern(sig.pattern, p, data.size())) return sig.type;
  }
  return nullptr;
}

bool BLEWatchScreenBase::_containsNoCase(const std::string& text, const char* needle)
{
  if (!needle || !*needle) return true;

  const size_t needleLen = strlen(needle);
  if (needleLen > text.size()) return false;

  for (size_t i = 0; i + needleLen <= text.size(); ++i) {
    size_t j = 0;
    for (; j < needleLen; ++j) {
      const unsigned char a = static_cast<unsigned char>(text[i + j]);
      const unsigned char b = static_cast<unsigned char>(needle[j]);
      if (tolower(a) != tolower(b)) break;
    }
    if (j == needleLen) return true;
  }
  return false;
}

bool BLEWatchScreenBase::_isNamed(const std::string& text, const char* needle)
{
  const size_t needleLen = strlen(needle);
  if (text.size() < needleLen) return false;
  for (size_t i = 0; i < needleLen; ++i)
    if (tolower(static_cast<unsigned char>(text[i])) != tolower(static_cast<unsigned char>(needle[i]))) return false;
  if (text.size() == needleLen) return true;
  const char next = text[needleLen];
  return next == '-' || next == '_' || isdigit(static_cast<unsigned char>(next));
}
