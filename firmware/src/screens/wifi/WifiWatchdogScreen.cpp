#include "WifiWatchdogScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "core/ConfigManager.h"
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
  _view        = VIEW_OVERALL;
  _channel     = 1;
  _itemCount   = 0;
  _gridSel     = 0;
  _prevGridSel = -1;
  _holdCell    = -1;
  for (int i = 0; i < 4; i++) _prevCounts[i] = -1;
  _lastUpdate  = millis();

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

#ifdef DEVICE_HAS_TOUCH_NAV
  Uni.Nav->setSuppressKeys(true);
#endif

  render();
}

void WifiWatchdogScreen::onUpdate()
{
  if (_view == VIEW_OVERALL) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();

#ifdef DEVICE_HAS_TOUCH_NAV
      const int16_t tx = Uni.Nav->lastTouchX();
      const int16_t ty = Uni.Nav->lastTouchY();
      const int backW  = bodyW() / 6;
      if (dir == INavigation::DIR_BACK || (tx >= 0 && (int)tx < (int)bodyX() + backW)) {
        onBack(); return;
      }
      if (tx >= 0) {
        const int gx  = (int)tx - (int)bodyX() - backW;
        const int gw  = bodyW() - backW;
        const int col = gx / (gw / 2);
        const int row = ((int)ty - (int)bodyY()) / (bodyH() / 2);
        if (col >= 0 && col < 2 && row >= 0 && row < 2) {
          static constexpr View kViewMap[] = {VIEW_DEAUTH, VIEW_PROBES, VIEW_FLOOD, VIEW_EVILTWIN};
          Uni.Nav->setSuppressKeys(false);
          _holdCell  = -1;
          _view      = kViewMap[row * 2 + col];
          _itemCount = 0;
          _scroll.resetScroll();
          _renderView();
        }
      }
#else
      if (dir == INavigation::DIR_BACK) { onBack(); return; }

#ifdef DEVICE_HAS_4WAY_NAV
      if (dir == INavigation::DIR_UP || dir == INavigation::DIR_DOWN) {
        _gridSel ^= 2;
        _renderOverall();
        if (Uni.Speaker) Uni.Speaker->beep();
      } else if (dir == INavigation::DIR_LEFT || dir == INavigation::DIR_RIGHT) {
        _gridSel ^= 1;
        _renderOverall();
        if (Uni.Speaker) Uni.Speaker->beep();
      } else
#endif
      if (dir == INavigation::DIR_LEFT || dir == INavigation::DIR_UP) {
        _gridSel = (_gridSel + 3) % 4;
        _renderOverall();
        if (Uni.Speaker) Uni.Speaker->beep();
      } else if (dir == INavigation::DIR_RIGHT || dir == INavigation::DIR_DOWN) {
        _gridSel = (_gridSel + 1) % 4;
        _renderOverall();
        if (Uni.Speaker) Uni.Speaker->beep();
      } else if (dir == INavigation::DIR_PRESS) {
        static constexpr View kViewMap[] = {VIEW_DEAUTH, VIEW_PROBES, VIEW_FLOOD, VIEW_EVILTWIN};
        _view      = kViewMap[_gridSel];
        _itemCount = 0;
        _scroll.resetScroll();
        _renderView();
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
            newHold = 4; // back zone
          } else {
            const int gx  = (int)tx - (int)bodyX() - backW;
            const int gw  = bodyW() - backW;
            const int col = gx / (gw / 2);
            const int row = ((int)ty - (int)bodyY()) / (bodyH() / 2);
            if (col >= 0 && col < 2 && row >= 0 && row < 2)
              newHold = row * 2 + col;
          }
        }
        if (newHold != _holdCell) {
          const bool backChanged = (_holdCell == 4) || (newHold == 4);
          if (_holdCell >= 0 && _holdCell < 4) _prevCounts[_holdCell] = -1;
          if (newHold  >= 0 && newHold  < 4)  _prevCounts[newHold]   = -1;
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

  } else {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        _view        = VIEW_OVERALL;
        _prevGridSel = -1;
#ifdef DEVICE_HAS_TOUCH_NAV
        Uni.Nav->drawOverlay();  // clear bar now, before suppress turns it into a no-op
        Uni.Nav->setSuppressKeys(true);
#endif
        Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
        render();
        return;
      }
      if (dir == INavigation::DIR_UP   || dir == INavigation::DIR_DOWN  ||
          dir == INavigation::DIR_LEFT  || dir == INavigation::DIR_RIGHT) {
        _scroll.onNav(dir);
      }
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
  if (_view == VIEW_OVERALL) {
    _prevGridSel = -1;
    _renderOverall();
    return;
  }
  if (_itemCount > 0) {
    _scroll.render(bodyX(), bodyY(), bodyW(), bodyH());
  } else {
    Sprite sp(&Uni.Lcd);
    sp.createSprite(bodyW(), bodyH());
    sp.fillSprite(TFT_BLACK);
    sp.setTextDatum(MC_DATUM);
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sp.drawString("Monitoring...", bodyW() / 2, bodyH() / 2);
    sp.pushSprite(bodyX(), bodyY());
    sp.deleteSprite();
  }
}

void WifiWatchdogScreen::onBack()
{
#ifdef DEVICE_HAS_TOUCH_NAV
  Uni.Nav->setSuppressKeys(false);
#endif
  Screen.goBack();
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
  int ssidCount = 0;
  for (auto& kv : _twinMap) {
    if (ssidCount >= MAX_ITEMS || n >= MAX_ROWS) break;
    if (kv.second.size() < 2) continue;

    snprintf(_labels[n], sizeof(_labels[n]),
             "?? %s (%d)", kv.first.c_str(), (int)kv.second.size());
    _rows[n].label = _labels[n];
    _rows[n].value = "";
    n++;

    for (const auto& b : kv.second) {
      if (n >= MAX_ROWS) break;
      snprintf(_labels[n], sizeof(_labels[n]),
               "  - %02X:%02X:%02X CH%d",
               b.bssid[0], b.bssid[1], b.bssid[2], b.channel);
      _rows[n].label = _labels[n];
      _rows[n].value = "";
      n++;
    }

    if (Achievement.inc("wifi_evil_twin_detected") == 1)
      Achievement.unlock("wifi_evil_twin_detected");
    ssidCount++;
  }

  _setListState(n);
}

void WifiWatchdogScreen::_setListState(int newCount)
{
  _itemCount = newCount;
  _scroll.setRows(_rows, (uint8_t)_itemCount);
  render();
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

  int counts[4];
  counts[0] = (int)_deauthMap.size();
  counts[1] = (int)_probeMap.size();
  counts[2] = 0;
  for (auto& kv : _beaconMap)
    if (kv.second.ratePerSec >= FLOOD_THRESHOLD) counts[2]++;
  counts[3] = 0;
  for (auto& kv : _twinMap)
    if ((int)kv.second.size() >= 2) counts[3]++;

  const bool forceAll = (_prevGridSel < 0);

#ifdef DEVICE_HAS_TOUCH_NAV
  if (forceAll) {
    _drawBackButton();
  }
  for (int i = 0; i < 4; i++) {
    if (forceAll || counts[i] != _prevCounts[i]) {
      _drawGridCell(i, counts[i]);
      _prevCounts[i] = counts[i];
    }
  }
  _prevGridSel = 0;
#else
  for (int i = 0; i < 4; i++) {
    const bool selDirty = (i == (int)_gridSel) != (i == _prevGridSel);
    if (forceAll || selDirty || counts[i] != _prevCounts[i]) {
      _drawGridCell(i, counts[i]);
      _prevCounts[i] = counts[i];
    }
  }
  _prevGridSel = (int)_gridSel;
#endif
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
    _rows[n].label = _labels[n];
    _rows[n].value = _sublabels[n];
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
  int macCount = 0;
  for (auto& kv : _probeMap) {
    if (macCount >= MAX_ITEMS || n >= MAX_ROWS) break;
    const MacAddr&    mac = kv.first;
    const ProbeEntry& e   = kv.second;

    snprintf(_labels[n], sizeof(_labels[n]),
             "%02X:%02X:%02X:%02X:%02X:%02X (x%d)",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], e.count);
    _rows[n].label = _labels[n];
    _rows[n].value = "";
    n++;

    if (e.ssidCount == 0) {
      if (n < MAX_ROWS) {
        snprintf(_labels[n], sizeof(_labels[n]), "  - (wildcard)");
        _rows[n].label = _labels[n];
        _rows[n].value = "";
        n++;
      }
    } else {
      for (int i = 0; i < e.ssidCount && n < MAX_ROWS; i++) {
        snprintf(_labels[n], sizeof(_labels[n]), "  - %s", e.ssids[i]);
        _rows[n].label = _labels[n];
        _rows[n].value = "";
        n++;
      }
    }
    macCount++;
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
    _rows[n].label = _labels[n];
    _rows[n].value = _sublabels[n];
    n++;
  }

  _setListState(n);
}

// ── Back button renderer ──────────────────────────────────────────────────────

void WifiWatchdogScreen::_drawBackButton()
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

// ── Grid cell renderer ────────────────────────────────────────────────────────

void WifiWatchdogScreen::_drawGridCell(int idx, int count)
{
  static constexpr const char* kNames[] = {
    "Deauth", "Probes", "Beacon Flood", "Evil Twin"
  };

#ifdef DEVICE_HAS_TOUCH_NAV
  const int backW = bodyW() / 6;
  const int gw    = bodyW() - backW;
  const int cellW = gw / 2;
  const int cellH = bodyH() / 2;
  const int px    = bodyX() + backW + (idx % 2) * cellW;
  const int py    = bodyY() + (idx / 2) * cellH;

  const bool held = (idx == _holdCell);
  Sprite sp(&Uni.Lcd);
  sp.createSprite(cellW, cellH);
  sp.fillSprite(TFT_BLACK);
  sp.drawRoundRect(2, 2, cellW - 4, cellH - 4, 4, held ? Config.getThemeColor() : 0x2104);
  sp.setTextSize(1);
  sp.setTextDatum(TC_DATUM);
  sp.setTextColor(held ? TFT_WHITE : TFT_LIGHTGREY, TFT_BLACK);
  sp.drawString(kNames[idx], cellW / 2, 8);
  char buf[6];
  snprintf(buf, sizeof(buf), "%d", count);
  sp.setTextSize(2);
  sp.setTextDatum(MC_DATUM);
  sp.setTextColor(count > 0 ? TFT_RED : TFT_DARKGREY, TFT_BLACK);
  sp.drawString(buf, cellW / 2, cellH / 2 + 4);
  sp.setTextSize(1);
  sp.pushSprite(px, py);
  sp.deleteSprite();
#else
  const int  cellW = bodyW() / 2;
  const int  cellH = bodyH() / 2;
  const int  px    = bodyX() + (idx % 2) * cellW;
  const int  py    = bodyY() + (idx / 2) * cellH;
  const bool sel   = (idx == (int)_gridSel);

  Sprite sp(&Uni.Lcd);
  sp.createSprite(cellW, cellH);
  sp.fillSprite(TFT_BLACK);
  sp.drawRoundRect(2, 2, cellW - 4, cellH - 4, 4, sel ? Config.getThemeColor() : 0x2104);
  sp.setTextSize(1);
  sp.setTextDatum(TC_DATUM);
  sp.setTextColor(sel ? TFT_WHITE : TFT_LIGHTGREY, TFT_BLACK);
  sp.drawString(kNames[idx], cellW / 2, 8);
  char buf[6];
  snprintf(buf, sizeof(buf), "%d", count);
  sp.setTextSize(2);
  sp.setTextDatum(MC_DATUM);
  sp.setTextColor(count > 0 ? TFT_RED : TFT_DARKGREY, TFT_BLACK);
  sp.drawString(buf, cellW / 2, cellH / 2 + 4);
  sp.setTextSize(1);
  sp.pushSprite(px, py);
  sp.deleteSprite();
#endif
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
