#include "WifiFoxHuntScreen.h"

#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <string.h>

static uint8_t _foxTargetBssid[6];
static volatile int _foxWifiRssi = 0;
static volatile uint32_t _foxWifiSeenMs = 0;

static void _foxWifiSnifferCb(void* buf, wifi_promiscuous_pkt_type_t type)
{
  if (type != WIFI_PKT_DATA && type != WIFI_PKT_MGMT) return;

  const auto* pkt = (const wifi_promiscuous_pkt_t*)buf;
  if (pkt->rx_ctrl.sig_len < 24) return;

  // addr2 is the transmitter address. We only want frames emitted by the
  // selected AP, not client uplinks on the same BSSID/channel.
  const uint8_t* a2 = pkt->payload + 10;
  if (memcmp(a2, _foxTargetBssid, 6) != 0) return;

  _foxWifiRssi = pkt->rx_ctrl.rssi;
  _foxWifiSeenMs = millis();
}

WifiFoxHuntScreen::~WifiFoxHuntScreen()
{
  _stopTracking();
  _destroyTrackingUi();
}

void WifiFoxHuntScreen::onInit()
{
  _doScan();
}

void WifiFoxHuntScreen::onUpdate()
{
  if (_state == STATE_SCAN) {
    ListScreen::onUpdate();
    return;
  }

  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _stopTracking();
      _showScan();
      return;
    }
  }

  _updateTracking();
}

void WifiFoxHuntScreen::onRender()
{
  if (_state == STATE_TRACK) {
    _renderTracking(true);
    return;
  }
  ListScreen::onRender();
}

void WifiFoxHuntScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_SCAN) {
    if (index == 0) {
      _doScan();
      return;
    }
    const int apIndex = (int)index - 1;
    if (apIndex >= 0 && apIndex < _entryCount) _startTracking(apIndex);
  }
}

void WifiFoxHuntScreen::onBack()
{
  if (_state == STATE_TRACK) {
    _stopTracking();
    _showScan();
  } else {
    WiFi.scanDelete();
    Screen.goBack();
  }
}

void WifiFoxHuntScreen::_doScan()
{
  _state = STATE_SCAN;
  _selected = -1;
  ShowStatusAction::show("Scanning...", 0);

  WiFi.mode(WIFI_STA);
  WiFi.scanDelete();
  int total = WiFi.scanNetworks();

  _entryCount = 0;
  if (total <= 0) {
    WiFi.scanDelete();
    ShowStatusAction::show("No networks found");
    _showScan();
    return;
  }

  if (total > MAX_SCAN) total = MAX_SCAN;
  for (int i = 0; i < total; i++) {
    WifiEntry& e = _entries[_entryCount];
    e.ssid = WiFi.SSID(i);
    if (e.ssid.length() == 0) e.ssid = "<hidden>";
    e.bssid = WiFi.BSSIDstr(i);
    e.rssi = (int)WiFi.RSSI(i);
    e.channel = (int)WiFi.channel(i);

    _labels[_entryCount] = e.ssid;
    _subs[_entryCount] = e.bssid;
    _entryCount++;
  }

  WiFi.scanDelete();
  _showScan();
}

void WifiFoxHuntScreen::_showScan()
{
  _state = STATE_SCAN;
  _selected = -1;
  _items[0] = {"Rescan"};

  for (int i = 0; i < _entryCount; i++) {
    _items[i + 1] = {_labels[i].c_str(), _subs[i].c_str()};
    _items[i + 1].rssi = (int16_t)_entries[i].rssi;
    _items[i + 1].hasRssi = true;
    _items[i + 1].sublabelMarquee = true;
  }

  setItems(_items, _entryCount + 1);
}

void WifiFoxHuntScreen::_startTracking(int index)
{
  _selected = index;
  _state = STATE_TRACK;

  unsigned v[6] = {0};
  sscanf(_entries[index].bssid.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x",
         &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
  for (int i = 0; i < 6; i++) _foxTargetBssid[i] = (uint8_t)v[i];

  _foxWifiRssi = 0;
  _foxWifiSeenMs = 0;
  _lastRawRssi = 0;
  _lastLiveSeen = millis();
  _lastDraw = 0;
  _wasLive = false;
  _uiInitialized = false;
  _displayedRssi = 127;
  _displayedLabel = "";
  _displayedLive = false;
  _feedback.reset(_entries[index].rssi);

  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&_foxWifiSnifferCb);
  esp_wifi_set_channel((uint8_t)_entries[index].channel, WIFI_SECOND_CHAN_NONE);

  setItems(nullptr, 0);
  _initTrackingUi();
  _renderTracking(true);
}

void WifiFoxHuntScreen::_stopTracking()
{
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  delay(10);
  esp_wifi_set_promiscuous(false);
  if (Uni.Speaker) Uni.Speaker->noTone();
  _destroyTrackingUi();
}

void WifiFoxHuntScreen::_updateTracking()
{
  const uint32_t now = millis();
  const int raw = _foxWifiRssi;
  const uint32_t seen = _foxWifiSeenMs;
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

void WifiFoxHuntScreen::_renderTracking(bool force)
{
  if (_selected < 0) return;
  if (!_uiInitialized) _initTrackingUi();

  auto& lcd = Uni.Lcd;
  const int16_t x = bodyX();
  const int16_t y = bodyY();
  const int16_t w = bodyW();
  const int16_t h = bodyH();
  const uint32_t now = millis();
  const bool live = _foxWifiSeenMs != 0 && now - _foxWifiSeenMs <= 3000;

  if (force) {
    lcd.fillRect(x, y, w, h, TFT_BLACK);
    String name = _entries[_selected].ssid;
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

void WifiFoxHuntScreen::_initTrackingUi()
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

void WifiFoxHuntScreen::_destroyTrackingUi()
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
