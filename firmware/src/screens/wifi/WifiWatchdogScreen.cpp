#include "WifiWatchdogScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/WifiMenuScreen.h"

#include <vector>
#include <cstring>

// ── Static definitions ────────────────────────────────────────────────────────

std::unordered_map<WifiWatchdogScreen::MacAddr, std::string,                     WifiWatchdogScreen::MacHash, WifiWatchdogScreen::MacEqual> WifiWatchdogScreen::_ssidMap;
std::unordered_map<WifiWatchdogScreen::MacAddr, WifiWatchdogScreen::DeauthEntry, WifiWatchdogScreen::MacHash, WifiWatchdogScreen::MacEqual> WifiWatchdogScreen::_deauthMap;
std::unordered_map<WifiWatchdogScreen::MacAddr, WifiWatchdogScreen::ProbeEntry,  WifiWatchdogScreen::MacHash, WifiWatchdogScreen::MacEqual> WifiWatchdogScreen::_probeMap;
std::unordered_map<WifiWatchdogScreen::MacAddr, WifiWatchdogScreen::BeaconWindow,WifiWatchdogScreen::MacHash, WifiWatchdogScreen::MacEqual> WifiWatchdogScreen::_beaconWindow;
std::unordered_map<WifiWatchdogScreen::MacAddr, WifiWatchdogScreen::BeaconEntry, WifiWatchdogScreen::MacHash, WifiWatchdogScreen::MacEqual> WifiWatchdogScreen::_beaconMap;
std::unordered_map<std::string, std::vector<WifiWatchdogScreen::BssidInfo>>                                                                  WifiWatchdogScreen::_twinMap;

WifiWatchdogScreen::DeauthEvent  WifiWatchdogScreen::_ring[MAX_RING]              = {};
volatile int                     WifiWatchdogScreen::_ringHead                    = 0;
volatile int                     WifiWatchdogScreen::_ringTail                    = 0;

WifiWatchdogScreen::SsidEvent    WifiWatchdogScreen::_ssidRing[MAX_RING]          = {};
volatile int                     WifiWatchdogScreen::_ssidRingHead                = 0;
volatile int                     WifiWatchdogScreen::_ssidRingTail                = 0;

WifiWatchdogScreen::ProbeEvent   WifiWatchdogScreen::_probeRing[MAX_RING]         = {};
volatile int                     WifiWatchdogScreen::_probeRingHead               = 0;
volatile int                     WifiWatchdogScreen::_probeRingTail               = 0;

WifiWatchdogScreen::BeaconEvent  WifiWatchdogScreen::_beaconRing[MAX_BEACON_RING] = {};
volatile int                     WifiWatchdogScreen::_beaconRingHead              = 0;
volatile int                     WifiWatchdogScreen::_beaconRingTail              = 0;

portMUX_TYPE WifiWatchdogScreen::_ringLock = portMUX_INITIALIZER_UNLOCKED;

// ── Title ─────────────────────────────────────────────────────────────────────

const char* WifiWatchdogScreen::title()
{
  static constexpr const char* kNames[] = {
    "WiFi Watchdog", "Deauth/Disassoc", "Probe Requests", "Beacon Flood", "Evil Twin"
  };
  return kNames[_view];
}

// ── Destructor ────────────────────────────────────────────────────────────────

WifiWatchdogScreen::~WifiWatchdogScreen()
{
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  esp_wifi_set_promiscuous(false);
  _deauthMap.clear();
  _ssidMap.clear();
  _probeMap.clear();
  _beaconWindow.clear();
  _beaconMap.clear();
  _twinMap.clear();
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void WifiWatchdogScreen::onInit()
{
  _view       = VIEW_OVERALL;
  _state      = STATE_EMPTY;
  _channel    = 1;
  _itemCount  = 0;
  _lastUpdate = millis();

  _ringHead = _ringTail = 0;
  _ssidRingHead = _ssidRingTail = 0;
  _probeRingHead = _probeRingTail = 0;
  _beaconRingHead = _beaconRingTail = 0;

  _deauthMap.clear();
  _ssidMap.clear();
  _probeMap.clear();
  _beaconWindow.clear();
  _beaconMap.clear();
  _twinMap.clear();

  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&WifiWatchdogScreen::_promiscuousCb);

  render();
}

void WifiWatchdogScreen::onUpdate()
{
  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) {
      onBack();
      return;
    }
    if (dir == INavigation::DIR_UP) {
      _view = (View)((_view + 4) % 5);
      _renderView();
      return;
    }
    if (dir == INavigation::DIR_DOWN) {
      _view = (View)((_view + 1) % 5);
      _renderView();
      return;
    }
  }

  _drainRings();

  if (millis() - _lastUpdate >= 1000) {
    _lastUpdate = millis();
    _channel = (_channel % 13) + 1;
    esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);
    _updateRates();
    _renderView();
  }
}

void WifiWatchdogScreen::onRender()
{
  if (_state == STATE_LISTED) {
    ListScreen::onRender();
    return;
  }

  auto& lcd = Uni.Lcd;
  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.drawString("Monitoring...", bodyX() + bodyW() / 2, bodyY() + bodyH() / 2);
}

void WifiWatchdogScreen::onBack()
{
  Screen.setScreen(new WifiMenuScreen());
}

// ── Private: ring draining ────────────────────────────────────────────────────

void WifiWatchdogScreen::_drainRings()
{
  bool gotDeauth = false;

  for (int i = 0; i < MAX_RING && _ringTail != _ringHead; i++) {
    const auto& ev = _ring[_ringTail];
    auto it = _deauthMap.find(ev.mac);
    if (it == _deauthMap.end()) {
      DeauthEntry e{};
      e.timestamp  = ev.timestamp;
      e.counter    = 1;
      e.isDisassoc = ev.isDisassoc;
      auto ssidIt = _ssidMap.find(ev.mac);
      if (ssidIt != _ssidMap.end()) e.ssid = ssidIt->second;
      _deauthMap.emplace(ev.mac, e);
      gotDeauth = true;
      if (Achievement.inc("wifi_deauth_detected") == 1)
        Achievement.unlock("wifi_deauth_detected");
    } else {
      if (it->second.counter < 1000) ++it->second.counter;
      it->second.timestamp  = ev.timestamp;
      it->second.isDisassoc = ev.isDisassoc;
    }
    _ringTail = (_ringTail + 1) % MAX_RING;
  }

  for (int i = 0; i < MAX_RING && _ssidRingTail != _ssidRingHead; i++) {
    const auto& ev = _ssidRing[_ssidRingTail];
    MacAddr bssid{};
    memcpy(bssid.data(), ev.bssid.data(), 6);
    if (_ssidMap.find(bssid) == _ssidMap.end())
      _ssidMap.emplace(bssid, std::string(ev.ssid));

    // Build twin map: SSID → list of unique BSSIDs
    if (ev.ssid[0] != '\0') {
      std::string ssidKey(ev.ssid);
      auto& list = _twinMap[ssidKey];
      bool found = false;
      for (auto& b : list)
        if (b.bssid == bssid) { found = true; break; }
      if (!found && list.size() < 8)
        list.push_back({bssid, ev.channel});
    }

    _ssidRingTail = (_ssidRingTail + 1) % MAX_RING;
  }

  for (int i = 0; i < MAX_RING && _probeRingTail != _probeRingHead; i++) {
    const auto& ev = _probeRing[_probeRingTail];
    MacAddr src{};
    memcpy(src.data(), ev.src.data(), 6);
    auto it = _probeMap.find(src);
    if (it == _probeMap.end()) {
      ProbeEntry e{};
      e.timestamp = ev.timestamp;
      e.count     = 1;
      if (ev.ssid[0] != '\0') {
        memcpy(e.ssids[0], ev.ssid, 33);
        e.ssidCount = 1;
      }
      _probeMap.emplace(src, e);
      if (Achievement.inc("wifi_probe_logged") == 1)
        Achievement.unlock("wifi_probe_logged");
    } else {
      ++it->second.count;
      it->second.timestamp = ev.timestamp;
      if (ev.ssid[0] != '\0' && it->second.ssidCount < 3) {
        bool found = false;
        for (int j = 0; j < it->second.ssidCount; j++) {
          if (strcmp(it->second.ssids[j], ev.ssid) == 0) { found = true; break; }
        }
        if (!found) memcpy(it->second.ssids[it->second.ssidCount++], ev.ssid, 33);
      }
    }
    _probeRingTail = (_probeRingTail + 1) % MAX_RING;
  }

  for (int i = 0; i < MAX_BEACON_RING && _beaconRingTail != _beaconRingHead; i++) {
    const auto& ev = _beaconRing[_beaconRingTail];
    MacAddr bssid{};
    memcpy(bssid.data(), ev.bssid.data(), 6);
    auto it = _beaconWindow.find(bssid);
    if (it == _beaconWindow.end()) {
      BeaconWindow w{};
      w.count = 1;
      auto ssidIt = _ssidMap.find(bssid);
      if (ssidIt != _ssidMap.end()) strncpy(w.ssid, ssidIt->second.c_str(), 32);
      _beaconWindow.emplace(bssid, w);
    } else {
      if (it->second.count < 65535) ++it->second.count;
      if (it->second.ssid[0] == '\0') {
        auto ssidIt = _ssidMap.find(bssid);
        if (ssidIt != _ssidMap.end()) strncpy(it->second.ssid, ssidIt->second.c_str(), 32);
      }
    }
    _beaconRingTail = (_beaconRingTail + 1) % MAX_BEACON_RING;
  }

  if (gotDeauth && Uni.Speaker) Uni.Speaker->playNotification();
}

// ── Private: rate update (called every 1s) ────────────────────────────────────

void WifiWatchdogScreen::_updateRates()
{
  const unsigned long now = millis();

  for (auto& kv : _beaconWindow) {
    auto it = _beaconMap.find(kv.first);
    if (it == _beaconMap.end()) {
      BeaconEntry e{};
      strncpy(e.ssid, kv.second.ssid, 32);
      e.ratePerSec = kv.second.count;
      e.lastSeen   = now;
      _beaconMap.emplace(kv.first, e);
    } else {
      it->second.ratePerSec = kv.second.count;
      it->second.lastSeen   = now;
      if (it->second.ssid[0] == '\0' && kv.second.ssid[0] != '\0')
        strncpy(it->second.ssid, kv.second.ssid, 32);
    }
    if (kv.second.count >= FLOOD_THRESHOLD) {
      if (Achievement.inc("wifi_beacon_flood") == 1)
        Achievement.unlock("wifi_beacon_flood");
    }
  }
  _beaconWindow.clear();

  {
    std::vector<MacAddr> toErase;
    for (auto& kv : _beaconMap)
      if (now - kv.second.lastSeen > WINDOW_MS) toErase.push_back(kv.first);
    for (auto& k : toErase) _beaconMap.erase(k);
  }
}

// ── Private: view rendering ───────────────────────────────────────────────────

void WifiWatchdogScreen::_renderView()
{
  switch (_view) {
    case VIEW_OVERALL:  _renderOverall();  break;
    case VIEW_DEAUTH:   _renderDeauth();   break;
    case VIEW_PROBES:   _renderProbes();   break;
    case VIEW_FLOOD:    _renderFlood();    break;
    case VIEW_EVILTWIN: _renderEviltwin(); break;
  }
}

void WifiWatchdogScreen::_renderEviltwin()
{
  int n = 0;
  for (auto& kv : _twinMap) {
    if (n >= MAX_ITEMS) break;
    if (kv.second.size() < 2) continue;

    snprintf(_labels[n], sizeof(_labels[n]),
             "?? %s  (%d BSSIDs)", kv.first.c_str(), (int)kv.second.size());

    // Show first two BSSIDs with their channels in sublabel
    const BssidInfo& b0 = kv.second[0];
    const BssidInfo& b1 = kv.second[1];
    snprintf(_sublabels[n], sizeof(_sublabels[n]),
             "%02X:%02X:%02X CH%d / %02X:%02X:%02X CH%d",
             b0.bssid[0], b0.bssid[1], b0.bssid[2], b0.channel,
             b1.bssid[0], b1.bssid[1], b1.bssid[2], b1.channel);

    _items[n] = {_labels[n], _sublabels[n]};
    n++;

    if (Achievement.inc("wifi_evil_twin_detected") == 1)
      Achievement.unlock("wifi_evil_twin_detected");
  }

  _setListState(n);
}

void WifiWatchdogScreen::_setListState(int newCount)
{
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
  if (countChanged) setItems(_items, _itemCount);
  else              render();
}

void WifiWatchdogScreen::_renderOverall()
{
  const unsigned long now = millis();
  {
    std::vector<MacAddr> toErase;
    for (auto& kv : _deauthMap)
      if (now - kv.second.timestamp > WINDOW_MS) toErase.push_back(kv.first);
    for (auto& k : toErase) _deauthMap.erase(k);
  }
  {
    std::vector<MacAddr> toErase;
    for (auto& kv : _probeMap)
      if (now - kv.second.timestamp > WINDOW_MS) toErase.push_back(kv.first);
    for (auto& k : toErase) _probeMap.erase(k);
  }

  int n = 0;

  for (auto& kv : _deauthMap) {
    if (n >= MAX_ITEMS) break;
    const MacAddr&     mac = kv.first;
    const DeauthEntry& e   = kv.second;
    const char* tag = e.isDisassoc ? "[DIS]" : "[DEA]";
    char cnt[8];
    snprintf(cnt, sizeof(cnt), e.counter >= 1000 ? "999+" : "%d", e.counter);
    if (!e.ssid.empty())
      snprintf(_labels[n], sizeof(_labels[n]), "%s %s x%s", tag, e.ssid.c_str(), cnt);
    else
      snprintf(_labels[n], sizeof(_labels[n]), "%s %02X:%02X:%02X x%s",
               tag, mac[0], mac[1], mac[2], cnt);
    const unsigned long s = (now - e.timestamp) / 1000UL;
    if (s == 0) snprintf(_sublabels[n], sizeof(_sublabels[n]), "just now");
    else        snprintf(_sublabels[n], sizeof(_sublabels[n]), "%lus ago", s);
    _items[n] = {_labels[n], _sublabels[n]};
    n++;
  }

  for (auto& kv : _probeMap) {
    if (n >= MAX_ITEMS) break;
    const MacAddr&    mac = kv.first;
    const ProbeEntry& e   = kv.second;
    snprintf(_labels[n], sizeof(_labels[n]),
             "[PRB] %02X:%02X:%02X:%02X x%d",
             mac[0], mac[1], mac[2], mac[3], e.count);
    if (e.ssidCount > 1)
      snprintf(_sublabels[n], sizeof(_sublabels[n]), "%s +%d more", e.ssids[0], e.ssidCount - 1);
    else if (e.ssidCount == 1)
      snprintf(_sublabels[n], sizeof(_sublabels[n]), "%s", e.ssids[0]);
    else
      snprintf(_sublabels[n], sizeof(_sublabels[n]), "wildcard");
    _items[n] = {_labels[n], _sublabels[n]};
    n++;
  }

  for (auto& kv : _beaconMap) {
    if (n >= MAX_ITEMS) break;
    if (kv.second.ratePerSec < FLOOD_THRESHOLD) continue;
    const MacAddr&     mac = kv.first;
    const BeaconEntry& e   = kv.second;
    if (e.ssid[0] != '\0')
      snprintf(_labels[n], sizeof(_labels[n]), "[BCN!] %s %d/s", e.ssid, e.ratePerSec);
    else
      snprintf(_labels[n], sizeof(_labels[n]), "[BCN!] %02X:%02X:%02X %d/s",
               mac[0], mac[1], mac[2], e.ratePerSec);
    snprintf(_sublabels[n], sizeof(_sublabels[n]), "beacon flood");
    _items[n] = {_labels[n], _sublabels[n]};
    n++;
  }

  _setListState(n);
}

void WifiWatchdogScreen::_renderDeauth()
{
  const unsigned long now = millis();
  {
    std::vector<MacAddr> toErase;
    for (auto& kv : _deauthMap)
      if (now - kv.second.timestamp > WINDOW_MS) toErase.push_back(kv.first);
    for (auto& k : toErase) _deauthMap.erase(k);
  }

  int n = 0;
  for (auto& kv : _deauthMap) {
    if (n >= MAX_ITEMS) break;
    const MacAddr&     mac = kv.first;
    const DeauthEntry& e   = kv.second;
    const char* tag = e.isDisassoc ? "DIS" : "DEA";
    char cnt[8];
    snprintf(cnt, sizeof(cnt), e.counter >= 1000 ? "999+" : "%d", e.counter);
    if (!e.ssid.empty())
      snprintf(_labels[n], sizeof(_labels[n]), "%s (%s x%s)", e.ssid.c_str(), tag, cnt);
    else
      snprintf(_labels[n], sizeof(_labels[n]), "%02X:%02X:%02X:%02X:%02X:%02X (%s x%s)",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], tag, cnt);
    const unsigned long s = (now - e.timestamp) / 1000UL;
    if (s == 0) snprintf(_sublabels[n], sizeof(_sublabels[n]), "just now");
    else        snprintf(_sublabels[n], sizeof(_sublabels[n]), "%lus ago", s);
    _items[n] = {_labels[n], _sublabels[n]};
    n++;
  }

  _setListState(n);
}

void WifiWatchdogScreen::_renderProbes()
{
  const unsigned long now = millis();
  {
    std::vector<MacAddr> toErase;
    for (auto& kv : _probeMap)
      if (now - kv.second.timestamp > WINDOW_MS) toErase.push_back(kv.first);
    for (auto& k : toErase) _probeMap.erase(k);
  }

  int n = 0;
  for (auto& kv : _probeMap) {
    if (n >= MAX_ITEMS) break;
    const MacAddr&    mac = kv.first;
    const ProbeEntry& e   = kv.second;
    snprintf(_labels[n], sizeof(_labels[n]),
             "%02X:%02X:%02X:%02X:%02X:%02X (x%d)",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], e.count);
    if (e.ssidCount == 0) {
      snprintf(_sublabels[n], sizeof(_sublabels[n]), "wildcard probe");
    } else {
      char buf[64] = {};
      for (int i = 0; i < e.ssidCount; i++) {
        if (i > 0) strncat(buf, ", ", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, e.ssids[i], sizeof(buf) - strlen(buf) - 1);
      }
      snprintf(_sublabels[n], sizeof(_sublabels[n]), "%s", buf);
    }
    _items[n] = {_labels[n], _sublabels[n]};
    n++;
  }

  _setListState(n);
}

void WifiWatchdogScreen::_renderFlood()
{
  int n = 0;
  for (auto& kv : _beaconMap) {
    if (n >= MAX_ITEMS) break;
    const MacAddr&     mac = kv.first;
    const BeaconEntry& e   = kv.second;
    const bool flood = e.ratePerSec >= FLOOD_THRESHOLD;
    if (e.ssid[0] != '\0')
      snprintf(_labels[n], sizeof(_labels[n]), "%s%s  %d/s",
               flood ? "!! " : "", e.ssid, e.ratePerSec);
    else
      snprintf(_labels[n], sizeof(_labels[n]), "%s%02X:%02X:%02X:%02X:%02X:%02X  %d/s",
               flood ? "!! " : "",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], e.ratePerSec);
    snprintf(_sublabels[n], sizeof(_sublabels[n]), "%s", flood ? "beacon flood!" : "normal");
    _items[n] = {_labels[n], _sublabels[n]};
    n++;
  }

  _setListState(n);
}

// ── Promiscuous callback ──────────────────────────────────────────────────────

void WifiWatchdogScreen::_promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type)
{
  if (type != WIFI_PKT_MGMT || buf == nullptr) return;

  const auto     pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* pay = pkt->payload;
  const size_t   len = pkt->rx_ctrl.sig_len;

  if (len < 4) return;

  const uint8_t fc_sub  = (pay[0] >> 4) & 0x0F;
  const uint8_t fc_type = (pay[0] >> 2) & 0x03;
  if (fc_type != 0) return;

  // Deauth (0xC) or Disassoc (0xA)
  if ((fc_sub == 0xC || fc_sub == 0xA) && len >= 16) {
    portENTER_CRITICAL_ISR(&_ringLock);
    int next = (_ringHead + 1) % MAX_RING;
    if (next != _ringTail) {
      memcpy(_ring[_ringHead].mac.data(), pay + 10, 6);
      _ring[_ringHead].timestamp  = millis();
      _ring[_ringHead].isDisassoc = (fc_sub == 0xA);
      _ringHead = next;
    }
    portEXIT_CRITICAL_ISR(&_ringLock);
  }

  // Beacon (8) or Probe Response (5) — SSID resolution
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
          _ssidRing[_ssidRingHead].channel    = pkt->rx_ctrl.channel;
          _ssidRingHead = next;
        }
        portEXIT_CRITICAL_ISR(&_ringLock);
        break;
      }
      pos += 2 + elen;
    }
  }

  // Beacon (8) — rate tracking for flood detection
  if (fc_sub == 8 && len >= 16) {
    portENTER_CRITICAL_ISR(&_ringLock);
    int next = (_beaconRingHead + 1) % MAX_BEACON_RING;
    if (next != _beaconRingTail) {
      memcpy(_beaconRing[_beaconRingHead].bssid.data(), pay + 16, 6);
      _beaconRingHead = next;
    }
    portEXIT_CRITICAL_ISR(&_ringLock);
  }

  // Probe Request (4) — neighbor device detection
  if (fc_sub == 0x4 && len >= 26) {
    char ssid[33] = {};
    const uint8_t id   = pay[24];
    const uint8_t elen = pay[25];
    if (id == 0 && elen > 0 && elen <= 32 && (size_t)(26 + elen) <= len) {
      memcpy(ssid, pay + 26, elen);
      ssid[elen] = '\0';
    }
    portENTER_CRITICAL_ISR(&_ringLock);
    int next = (_probeRingHead + 1) % MAX_RING;
    if (next != _probeRingTail) {
      memcpy(_probeRing[_probeRingHead].src.data(), pay + 10, 6);
      memcpy(_probeRing[_probeRingHead].ssid, ssid, 33);
      _probeRing[_probeRingHead].timestamp = millis();
      _probeRingHead = next;
    }
    portEXIT_CRITICAL_ISR(&_ringLock);
  }
}
