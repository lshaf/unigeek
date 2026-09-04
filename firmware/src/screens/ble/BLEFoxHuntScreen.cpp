#include "BLEFoxHuntScreen.h"

#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"

static NimBLEAddress _foxBleTarget;
static volatile int _foxBleRssi = 0;
static volatile uint32_t _foxBleSeenMs = 0;

class FoxHuntBleWatcher : public NimBLEAdvertisedDeviceCallbacks {
public:
  void onResult(NimBLEAdvertisedDevice* dev) override
  {
    if (dev->getAddress() != _foxBleTarget) return;
    _foxBleRssi = dev->getRSSI();
    _foxBleSeenMs = millis();
  }
};
static FoxHuntBleWatcher _foxBleWatcher;

BLEFoxHuntScreen::~BLEFoxHuntScreen()
{
  if (_bleScan != nullptr) {
    _stopTracking();
    _bleScan->stop();
    NimBLEDevice::deinit(true);
    _bleScan = nullptr;
  }
}

void BLEFoxHuntScreen::onInit()
{
  NimBLEDevice::init("");
  _bleScan = NimBLEDevice::getScan();
  _doScan();
}

void BLEFoxHuntScreen::onUpdate()
{
  if (_state != STATE_TRACK) {
    ListScreen::onUpdate();
    return;
  }

  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _stopTracking();
      _showList();
      return;
    }
  }

  _updateTracking();
}

void BLEFoxHuntScreen::onRender()
{
  if (_state == STATE_TRACK) {
    _renderTracking(true);
    return;
  }
  ListScreen::onRender();
}

void BLEFoxHuntScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_LIST) {
    if (index == 0) {
      _doScan();
      return;
    }
    const int devIndex = (int)index - 1;
    if (devIndex >= 0 && devIndex < _devCount) _startTracking(devIndex);
  }
}

void BLEFoxHuntScreen::onBack()
{
  if (_state == STATE_TRACK) {
    _stopTracking();
    _showList();
  } else {
    if (_bleScan) _bleScan->stop();
    Screen.goBack();
  }
}

void BLEFoxHuntScreen::_doScan()
{
  _state = STATE_SCAN;
  _selected = -1;
  ShowStatusAction::show("Scanning...", 0);

  _bleScan->setAdvertisedDeviceCallbacks(nullptr, false);
  _bleScan->setMaxResults(0xFF);
  _bleScan->clearResults();
  _scanResults = _bleScan->start(kScanSeconds, false);

  _devCount = min((int)_scanResults.getCount(), (int)kMaxDevices);
  for (int i = 0; i < _devCount; i++) {
    _devices[i] = _scanResults.getDevice(i);
    std::string n = _devices[i].getName();
    String addr = _devices[i].getAddress().toString().c_str();

    _labels[i] = n.empty() ? addr : String(n.c_str());
    _subs[i] = n.empty() ? "" : addr;
  }

  if (_devCount == 0) ShowStatusAction::show("No devices found");
  _showList();
}

void BLEFoxHuntScreen::_showList()
{
  _state = STATE_LIST;
  _selected = -1;
  _items[0] = {"Rescan"};

  for (int i = 0; i < _devCount; i++) {
    _items[i + 1] = {_labels[i].c_str(),
                     _subs[i].length() ? _subs[i].c_str() : nullptr};
    _items[i + 1].rssi = (int16_t)_devices[i].getRSSI();
    _items[i + 1].hasRssi = true;
    _items[i + 1].sublabelMarquee = true;
  }

  setItems(_items, _devCount + 1);
}

void BLEFoxHuntScreen::_startTracking(int index)
{
  _selected = index;
  _selDev = _devices[index];  // self-contained snapshot from the initial scan
  _state = STATE_TRACK;

  _foxBleTarget = _selDev.getAddress();
  _foxBleRssi = 0;
  _foxBleSeenMs = 0;
  _lastRawRssi = 0;
  _lastLiveSeen = millis();
  _lastDraw = 0;
  _wasLive = false;
  _uiInitialized = false;
  _displayedRssi = 127;
  _displayedLabel = "";
  _displayedLive = false;
  _feedback.reset(_selDev.getRSSI());

  // Do not retain results while tracking; pass duplicates so every advertising
  // packet from the chosen address becomes a fresh RSSI sample.
  _bleScan->setMaxResults(0);
  _bleScan->setAdvertisedDeviceCallbacks(&_foxBleWatcher, true);
  _bleScan->start(0, nullptr, false);
  _watching = true;

  setItems(nullptr, 0);
  _initTrackingUi();
  _renderTracking(true);
}

void BLEFoxHuntScreen::_stopTracking()
{
  if (_bleScan && _watching) {
    _bleScan->stop();
    _bleScan->setAdvertisedDeviceCallbacks(nullptr, false);
    _bleScan->setMaxResults(0xFF);
    _watching = false;
  }
  if (Uni.Speaker) Uni.Speaker->noTone();
  _destroyTrackingUi();
}

void BLEFoxHuntScreen::_updateTracking()
{
  const uint32_t now = millis();
  const int raw = _foxBleRssi;
  const uint32_t seen = _foxBleSeenMs;
  const bool live = seen != 0 && now - seen <= 3000;

  if (raw != 0 && (raw != _lastRawRssi || seen != _lastLiveSeen)) {
    _lastRawRssi = raw;
    _lastLiveSeen = seen;

    // A fresh sighting after a real loss must not inherit stale EMA history.
    if (live && !_wasLive) _feedback.reset(raw);
    else                   _feedback.updateRssi(raw);
  }

  if (live && _feedback.beepDue(now) && Uni.Speaker && !Uni.Speaker->isPlaying()) {
    Uni.Speaker->tone(1200, 55);
    _feedback.markBeep(now);
  }

  _wasLive = live;

  // ~25 fps is kept only for the small pulse sprite. Text redraws are selective.
  if (now - _lastDraw >= 40) {
    _lastDraw = now;
    _renderTracking(false);
  }
}

void BLEFoxHuntScreen::_renderTracking(bool force)
{
  if (_selected < 0) return;
  if (!_uiInitialized) _initTrackingUi();

  auto& lcd = Uni.Lcd;
  const int16_t x = bodyX();
  const int16_t y = bodyY();
  const int16_t w = bodyW();
  const int16_t h = bodyH();
  const uint32_t now = millis();
  const bool live = _foxBleSeenMs != 0 && now - _foxBleSeenMs <= 3000;

  if (force) {
    lcd.fillRect(x, y, w, h, TFT_BLACK);
    String name = _labels[_selected];
    if (name.length() > 26) name = name.substring(0, 23) + "...";
    lcd.setTextDatum(TC_DATUM);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    lcd.drawString(name, x + w / 2, y + 4);
    _displayedRssi = 127;
    _displayedLabel = "";
    _displayedLive = !live;  // force status redraw below
  }

  const int shownRssi = live ? _feedback.rssi() : 0;
  if (force || live != _displayedLive || (live && shownRssi != _displayedRssi)) {
    lcd.fillRect(x, y + 16, w, 12, TFT_BLACK);
    lcd.setTextDatum(TC_DATUM);
    lcd.setTextSize(1);
    lcd.setTextColor(live ? TFT_WHITE : TFT_DARKGREY, TFT_BLACK);
    if (live) {
      char rssiText[20];
      snprintf(rssiText, sizeof(rssiText), "%d dBm", shownRssi);
      lcd.drawString(rssiText, x + w / 2, y + 18);
    } else {
      lcd.drawString("Searching...", x + w / 2, y + 18);
    }
    _displayedRssi = shownRssi;
    _displayedLive = live;
  }

  const char* nextLabel = live ? _feedback.label() : "SEARCHING";
  if (force || _displayedLabel != nextLabel) {
    lcd.fillRect(x, y + h - 16, w, 16, TFT_BLACK);
    lcd.setTextDatum(BC_DATUM);
    lcd.setTextColor(live ? _feedback.color() : TFT_DARKGREY, TFT_BLACK);
    lcd.drawString(nextLabel, x + w / 2, y + h - 5);
    _displayedLabel = nextLabel;
  }

  int16_t maxR = min((int16_t)(w / 4), (int16_t)(h / 3));
  if (maxR < 16) maxR = 16;
  const int16_t baseR = maxR - 8;
  const int16_t pulseR = live ? (int16_t)(8.0f * _feedback.pulse(now)) : 0;
  const int16_t r = baseR + pulseR;
  const uint16_t color = live ? _feedback.color() : TFT_DARKGREY;

  const int16_t cx = x + w / 2;
  const int16_t cy = y + h / 2 + 4;
  if (_pulseSprite) {
    _pulseSprite->fillSprite(TFT_BLACK);
    const int16_t scx = _pulseSpriteW / 2;
    const int16_t scy = _pulseSpriteH / 2;
    _pulseSprite->fillCircle(scx, scy, r, color);
    if (pulseR > 0) _pulseSprite->drawCircle(scx, scy, r + 2, TFT_WHITE);
    _pulseSprite->pushSprite(cx - _pulseSpriteW / 2, cy - _pulseSpriteH / 2);
  } else {
    // Low-memory fallback: redraw only the circle region, never the whole body.
    const int16_t outerR = maxR + 3;
    lcd.fillRect(cx - outerR, cy - outerR, outerR * 2 + 1, outerR * 2 + 1, TFT_BLACK);
    lcd.fillCircle(cx, cy, r, color);
    if (pulseR > 0) lcd.drawCircle(cx, cy, r + 2, TFT_WHITE);
  }

  lcd.setTextDatum(TL_DATUM);
}

void BLEFoxHuntScreen::_initTrackingUi()
{
  _destroyTrackingUi();

  int16_t maxR = min((int16_t)(bodyW() / 4), (int16_t)(bodyH() / 3));
  if (maxR < 16) maxR = 16;

  // Include room for the 8 px pulse and its 2 px white outline.
  const int16_t outerR = maxR + 2;
  _pulseSpriteW = outerR * 2 + 2;
  _pulseSpriteH = _pulseSpriteW;
  _pulseSprite = new Sprite(&Uni.Lcd);
  if (!_pulseSprite || !_pulseSprite->createSprite(_pulseSpriteW, _pulseSpriteH)) {
    delete _pulseSprite;
    _pulseSprite = nullptr;
    _pulseSpriteW = 0;
    _pulseSpriteH = 0;
  }

  _uiInitialized = true;
}

void BLEFoxHuntScreen::_destroyTrackingUi()
{
  if (_pulseSprite) {
    _pulseSprite->deleteSprite();
    delete _pulseSprite;
    _pulseSprite = nullptr;
  }
  _pulseSpriteW = 0;
  _pulseSpriteH = 0;
  _uiInitialized = false;
}
