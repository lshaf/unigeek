#include "WifiDeauthDetectorScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "screens/wifi/WifiMenuScreen.h"

#include <vector>
#include <cstring>

// ── Static definitions ────────────────────────────────────────────────────

std::unordered_map<
  WifiDeauthDetectorScreen::MacAddr,
  std::string,
  WifiDeauthDetectorScreen::MacHash,
  WifiDeauthDetectorScreen::MacEqual
> WifiDeauthDetectorScreen::_ssidMap = {};

std::unordered_map<
  WifiDeauthDetectorScreen::MacAddr,
  WifiDeauthDetectorScreen::DeauthEntry,
  WifiDeauthDetectorScreen::MacHash,
  WifiDeauthDetectorScreen::MacEqual
> WifiDeauthDetectorScreen::_deauthMap = {};

WifiDeauthDetectorScreen::DeauthEvent WifiDeauthDetectorScreen::_ring[MAX_RING] = {};
volatile int WifiDeauthDetectorScreen::_ringHead = 0;
volatile int WifiDeauthDetectorScreen::_ringTail = 0;

WifiDeauthDetectorScreen::SsidEvent WifiDeauthDetectorScreen::_ssidRing[MAX_RING] = {};
volatile int WifiDeauthDetectorScreen::_ssidRingHead = 0;
volatile int WifiDeauthDetectorScreen::_ssidRingTail = 0;

portMUX_TYPE  WifiDeauthDetectorScreen::_ringLock    = portMUX_INITIALIZER_UNLOCKED;
volatile bool WifiDeauthDetectorScreen::_newDetection = false;

// ── Destructor ────────────────────────────────────────────────────────────

WifiDeauthDetectorScreen::~WifiDeauthDetectorScreen()
{
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  esp_wifi_set_promiscuous(false);
  _deauthMap.clear();
  _ssidMap.clear();
}

// ── Lifecycle ─────────────────────────────────────────────────────────────

void WifiDeauthDetectorScreen::onInit()
{
  _state      = STATE_EMPTY;
  _channel    = 1;
  _itemCount  = 0;
  _newDetection = false;
  _ringHead = _ringTail = 0;
  _ssidRingHead = _ssidRingTail = 0;
  _deauthMap.clear();
  _ssidMap.clear();

  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&WifiDeauthDetectorScreen::_promiscuousCb);

  render();
}

void WifiDeauthDetectorScreen::onUpdate()
{
  // Drain ring buffers — process nav FIRST so input is never starved
  if (_state == STATE_LISTED) {
    ListScreen::onUpdate();
  } else {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        onBack();
        return;
      }
    }
  }

  // Drain deauth ring buffer (cap per frame to avoid stalling)
  bool gotNew = false;
  for (int i = 0; i < MAX_RING && _ringTail != _ringHead; i++) {
    const auto& ev = _ring[_ringTail];
    auto it = _deauthMap.find(ev.mac);
    if (it == _deauthMap.end()) {
      DeauthEntry e{};
      e.timestamp = ev.timestamp;
      e.counter   = 1;
      auto ssidIt = _ssidMap.find(ev.mac);
      if (ssidIt != _ssidMap.end()) e.ssid = ssidIt->second;
      _deauthMap.emplace(ev.mac, e);
      gotNew = true;
    } else {
      if (it->second.counter < 1000) ++it->second.counter;
      it->second.timestamp = ev.timestamp;
    }
    _ringTail = (_ringTail + 1) % MAX_RING;
  }

  // Drain SSID ring buffer
  for (int i = 0; i < MAX_RING && _ssidRingTail != _ssidRingHead; i++) {
    const auto& ev = _ssidRing[_ssidRingTail];
    MacAddr bssid{};
    memcpy(bssid.data(), ev.bssid.data(), 6);
    if (_ssidMap.find(bssid) == _ssidMap.end()) {
      _ssidMap.emplace(bssid, std::string(ev.ssid));
    }
    _ssidRingTail = (_ssidRingTail + 1) % MAX_RING;
  }

  if (gotNew) {
    if (Uni.Speaker && !Uni.Speaker->isPlaying()) Uni.Speaker->playNotification();
  }

  if (millis() - _lastUpdate >= 1000) {
    _lastUpdate = millis();
    _channel = (_channel % 13) + 1;
    esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);
    _refresh();
  }
}

void WifiDeauthDetectorScreen::onRender()
{
  if (_state == STATE_LISTED) {
    ListScreen::onRender();
    return;
  }

  TFT_eSprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);
  sp.setTextDatum(MC_DATUM);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString("Waiting for deauth packets...", bodyW() / 2, bodyH() / 2);
  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void WifiDeauthDetectorScreen::onBack()
{
  Screen.setScreen(new WifiMenuScreen());
}

// ── Private ───────────────────────────────────────────────────────────────

void WifiDeauthDetectorScreen::_refresh()
{
  const unsigned long now = millis();

  // Collect stale keys, then erase
  std::vector<MacAddr> toErase;
  for (auto& kv : _deauthMap) {
    if (now - kv.second.timestamp > WINDOW_MS) toErase.push_back(kv.first);
  }
  for (auto& k : toErase) _deauthMap.erase(k);

  int newCount = 0;
  for (auto& kv : _deauthMap) {
    if (newCount >= MAX_ITEMS) break;

    const MacAddr&    mac = kv.first;
    const DeauthEntry& e  = kv.second;

    const char* countStr = e.counter >= 1000 ? "999+" : nullptr;
    char countBuf[8];
    if (!countStr) { snprintf(countBuf, sizeof(countBuf), "%d", e.counter); countStr = countBuf; }

    if (!e.ssid.empty()) {
      snprintf(_labels[newCount], sizeof(_labels[newCount]),
               "%s (%s)", e.ssid.c_str(), countStr);
    } else {
      snprintf(_labels[newCount], sizeof(_labels[newCount]),
               "%02X:%02X:%02X:%02X:%02X:%02X (%s)",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], countStr);
    }

    const unsigned long secsAgo = (now - e.timestamp) / 1000UL;
    if (secsAgo == 0) {
      snprintf(_sublabels[newCount], sizeof(_sublabels[newCount]), "just now");
    } else {
      snprintf(_sublabels[newCount], sizeof(_sublabels[newCount]), "%lus ago", secsAgo);
    }

    _items[newCount] = {_labels[newCount], _sublabels[newCount]};
    newCount++;
  }

  if (newCount == 0) {
    if (_state != STATE_EMPTY) {
      _state     = STATE_EMPTY;
      _itemCount = 0;
      render();
    }
    return;
  }

  const bool countChanged = (newCount != _itemCount);
  _itemCount = newCount;
  _state     = STATE_LISTED;

  if (countChanged) {
    setItems(_items, _itemCount);
  } else {
    render();
  }
}

// ── Promiscuous callback (IRAM_ATTR — must be in .cpp) ────────────────────

void IRAM_ATTR WifiDeauthDetectorScreen::_promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type)
{
  if (type != WIFI_PKT_MGMT || buf == nullptr) return;

  const auto     pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* pay = pkt->payload;
  const size_t   len = pkt->rx_ctrl.sig_len;

  if (len < 4) return;

  const uint8_t fc_sub = (pay[0] >> 4) & 0x0F;
  const uint8_t fc_type = (pay[0] >> 2) & 0x03;
  if (fc_type != 0) return;

  // Deauth frame (subtype 0xC) — just push MAC + timestamp into ring buffer
  if (fc_sub == 0xC && len >= 16) {
    portENTER_CRITICAL_ISR(&_ringLock);
    int next = (_ringHead + 1) % MAX_RING;
    if (next != _ringTail) {  // drop if full (backpressure)
      memcpy(_ring[_ringHead].mac.data(), pay + 10, 6);
      _ring[_ringHead].timestamp = millis();
      _ringHead = next;
    }
    portEXIT_CRITICAL_ISR(&_ringLock);
  }

  // Beacon (8) or Probe Response (5) — push into SSID ring buffer
  if ((fc_sub == 8 || fc_sub == 5) && len >= 36) {
    size_t pos = 36;
    while (pos + 2 <= len) {
      const uint8_t id   = pay[pos];
      const uint8_t elen = pay[pos + 1];
      if (pos + 2 + elen > len) break;
      if (id == 0 && elen > 0 && elen <= 32) {
        portENTER_CRITICAL_ISR(&_ringLock);
        int next = (_ssidRingHead + 1) % MAX_RING;
        if (next != _ssidRingTail) {
          memcpy(_ssidRing[_ssidRingHead].bssid.data(), pay + 16, 6);
          memcpy(_ssidRing[_ssidRingHead].ssid, pay + pos + 2, elen);
          _ssidRing[_ssidRingHead].ssid[elen] = '\0';
          _ssidRingHead = next;
        }
        portEXIT_CRITICAL_ISR(&_ringLock);
        break;
      }
      pos += 2 + elen;
    }
  }
}