#include "WifiFoxHuntScreen.h"

#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <string.h>

static uint8_t _foxTargetBssid[6];
static volatile bool _foxDiscovering = false;
static volatile bool _foxTrackClient = false;
struct FoxClientSample { uint8_t mac[6]; int8_t rssi; uint8_t channel; uint16_t count; };
static FoxClientSample _foxClients[20];
static volatile int _foxClientCount = 0;
static volatile uint8_t _foxScanChannel = 1;

// Use both infrastructure data directions during discovery. Tracking keeps
// station-originated frames only, so RSSI reflects the selected transmitter.
static const uint8_t* _foxStationAddress(const uint8_t* p, uint16_t len,
                                         bool discovery) {
  if (len < 24) return nullptr;
  const uint8_t type = (p[0] >> 2) & 3;
  const uint8_t subtype = (p[0] >> 4) & 15;
  if (type == 0)
    return (subtype == 4 || subtype == 0 || subtype == 2 || subtype == 11)
             ? p + 10 : nullptr;
  if (type != 2) return nullptr;
  const uint8_t ds = p[1] & 3;
  if (ds == 1) return p + 10;
  if (discovery && ds == 2) return p + 4;
  return nullptr;
}
static bool _foxValidMac(const uint8_t* mac) {
  if (mac[0] & 1) return false;
  bool zero = true;
  for (int i = 0; i < 6; ++i) if (mac[i]) zero = false;
  return !zero;
}

static volatile int _foxWifiRssi = 0;
static volatile uint32_t _foxWifiSeenMs = 0;

static void _foxWifiSnifferCb(void* buf, wifi_promiscuous_pkt_type_t type)
{
  if (type != WIFI_PKT_DATA && type != WIFI_PKT_MGMT) return;

  const auto* pkt = (const wifi_promiscuous_pkt_t*)buf;
  if (pkt->rx_ctrl.sig_len < 24) return;

  // addr2 is the transmitter. Station tracking only accepts station frames.
  if (_foxDiscovering) {
    const uint8_t* mac = _foxStationAddress(pkt->payload, pkt->rx_ctrl.sig_len, true);
    if (!mac) return;
    if (!_foxValidMac(mac)) return;
    int index = 0;
    for (; index < _foxClientCount; ++index)
      if (memcmp(_foxClients[index].mac, mac, 6) == 0) break;
    if (index == _foxClientCount) {
      if (index >= 20) return;
      memcpy(_foxClients[index].mac, mac, 6);
      _foxClients[index].count = 0;
      _foxClientCount = index + 1;
    }
    _foxClients[index].rssi = pkt->rx_ctrl.rssi;
    _foxClients[index].channel = _foxScanChannel;
    ++_foxClients[index].count;
    return;
  }
  // Only use client-originated frames in client mode.
  if (_foxTrackClient && !_foxStationAddress(pkt->payload, pkt->rx_ctrl.sig_len, false)) return;
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

void WifiFoxHuntScreen::configureTarget(bool client, const uint8_t mac[6], uint8_t channel, const char* ssid)
{
  _directTarget = true;
  _mode = client ? MODE_CLIENT : MODE_AP;
  char formatted[18];
  snprintf(formatted, sizeof(formatted), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  _entryCount = 1;
  _entries[0].bssid = formatted;
  _entries[0].ssid = ssid ? ssid : "";
  _entries[0].channel = channel;
  _entries[0].rssi = -90;
}

void WifiFoxHuntScreen::onInit()
{
  if (_directTarget) _startTracking(0);
  else _showModes();
}

void WifiFoxHuntScreen::onUpdate()
{
  if (_state != STATE_TRACK) {
    ListScreen::onUpdate();
    return;
  }

  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _stopTracking();
      if (_directTarget) Screen.goBack();
      else _showScan();
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
  if (_state == STATE_MODE) {
    _mode = index == 0 ? MODE_AP : MODE_CLIENT;
    _doScan();
    return;
  }
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
    if (_directTarget) Screen.goBack();
    else _showScan();
  } else {
    WiFi.scanDelete();
    if (_state == STATE_SCAN) _showModes();
    else Screen.goBack();
  }
}

void WifiFoxHuntScreen::_showModes()
{
  _state = STATE_MODE;
  _items[0] = {"Access Points"};
  _items[1] = {"Clients/Stations"};
  setItems(_items, 2);
}

void WifiFoxHuntScreen::_scanClients()
{
  _entryCount = 0;
  _foxClientCount = 0;
  _foxTrackClient = false;
  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(&_foxWifiSnifferCb);
  _foxDiscovering = true;
  esp_wifi_set_promiscuous(true);
  // Revisit each channel to catch intermittent station traffic.
  // Two complete sweeps; retain discoveries between passes.
  for (uint8_t pass = 0; pass < 2; ++pass) {
    for (uint8_t channel = 1; channel <= 13; ++channel) {
    _foxScanChannel = channel;
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
      delay(500);
    }
  }
  esp_wifi_set_promiscuous(false);
  _foxDiscovering = false;
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  for (int i = 1; i < _foxClientCount; ++i) {
    FoxClientSample sample = _foxClients[i];
    int j = i;
    while (j > 0 && _foxClients[j - 1].rssi < sample.rssi) {
      _foxClients[j] = _foxClients[j - 1];
      --j;
    }
    _foxClients[j] = sample;
  }
  const int count = _foxClientCount;
  for (int i = 0; i < count && i < MAX_SCAN; ++i) {
    const FoxClientSample& sample = _foxClients[i];
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             sample.mac[0], sample.mac[1], sample.mac[2],
             sample.mac[3], sample.mac[4], sample.mac[5]);
    WifiEntry& entry = _entries[_entryCount];
    entry.ssid = mac;
    entry.bssid = mac;
    entry.rssi = sample.rssi;
    entry.channel = sample.channel;
    _labels[_entryCount] = String(mac) + " CH" + String(sample.channel);
    _subs[_entryCount] = "";
    ++_entryCount;
  }
  if (!_entryCount) ShowStatusAction::show("No stations found");
  _showScan();
}

void WifiFoxHuntScreen::_doScan()
{
  _state = STATE_SCAN;
  _selected = -1;
  render();
  ShowStatusAction::show("Scanning...", 0);

  if (_mode == MODE_CLIENT) {
    _scanClients();
    return;
  }
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
    _subs[_entryCount] = e.bssid + " CH" + String(e.channel);
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
    _items[i + 1].sublabelMarquee = (_mode != MODE_CLIENT);
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

  _foxTrackClient = (_mode == MODE_CLIENT);
  _foxWifiRssi = 0;
  _foxWifiSeenMs = 0;
  _lastRawRssi = 0;
  _lastLiveSeen = millis();
  _lastDraw = 0;
  _wasLive = false;
  _uiInitialized = false;
  _displayedRssi = 127;
  _displayedLive = false;
  _continuousTone = false;
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
  _foxTrackClient = false;
  delay(10);
  esp_wifi_set_promiscuous(false);
  if (Uni.Speaker) Uni.Speaker->noTone();
  _continuousTone = false;
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

  // At -20 dBm or stronger, switch from cadence beeps to a continuous
  // lock tone. A 3 dB hysteresis prevents rapid on/off chatter around the
  // threshold. The long tone is restarted if the speaker task ever expires.
  if (Uni.Speaker && live) {
    const int filteredRssi = _feedback.rssi();
    if (!_continuousTone && filteredRssi >= -20) {
      _continuousTone = true;
      Uni.Speaker->tone(1200, 60000);
      _feedback.markBeep(now);
    } else if (_continuousTone && filteredRssi < -23) {
      Uni.Speaker->noTone();
      _continuousTone = false;
    }

    if (_continuousTone) {
      if (!Uni.Speaker->isPlaying()) Uni.Speaker->tone(1200, 60000);
    } else if (_feedback.beepDue(now) && !Uni.Speaker->isPlaying()) {
      Uni.Speaker->tone(1200, 55);
      _feedback.markBeep(now);
    }
  } else if (_continuousTone) {
    if (Uni.Speaker) Uni.Speaker->noTone();
    _continuousTone = false;
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
    _displayedLive = !live;  // force status redraw below
  }

  const int shownRssi = live ? _feedback.rssi() : 0;
  const bool statusChanged = force || live != _displayedLive ||
                             (live && shownRssi != _displayedRssi);
  if (statusChanged) {
    lcd.fillRect(x, y + 16, w, 12, TFT_BLACK);
    lcd.setTextDatum(TC_DATUM);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    if (!live) {
      lcd.drawString(_mode == MODE_CLIENT && _foxWifiSeenMs ? "Signal stale" : "Searching...", x + w / 2, y + 18);
    }
  }

  if (statusChanged) {
    lcd.fillRect(x, y + h - 16, w, 16, TFT_BLACK);
    lcd.setTextDatum(BC_DATUM);
    lcd.setTextColor(live ? TFT_WHITE : TFT_DARKGREY, TFT_BLACK);
    if (live) {
      char rssiText[20];
      snprintf(rssiText, sizeof(rssiText), "%d dBm", shownRssi);
      lcd.drawString(rssiText, x + w / 2, y + h - 5);
    }
  }
  _displayedRssi = shownRssi;
  _displayedLive = live;

  int16_t maxR = min((int16_t)(w / 4), (int16_t)(h / 3));
  if (maxR < 16) maxR = 16;
  const int16_t baseR = maxR - 8;
  const int16_t pulseR = live ? (int16_t)(8.0f * _feedback.pulse(now)) : 0;
  const int16_t r = baseR + pulseR;
  const uint16_t color = live ? _feedback.color() : TFT_DARKGREY;

  const int16_t cx = x + w / 2;
  const int16_t cy = y + h / 2 - 4;
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
