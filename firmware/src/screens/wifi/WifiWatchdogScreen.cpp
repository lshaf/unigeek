#include "WifiWatchdogScreen.h"
#include "screens/wifi/WifiChannelMonitorDetailsScreen.h"
#include "screens/wifi/WifiTargetActions.h"

#include <algorithm>
#include "karma/WifiKarmaDetectorScreen.h"
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
std::unordered_map<WifiWatchdogScreen::MacAddr, WifiWatchdogScreen::BeaconWindow,WifiWatchdogScreen::MacHash, WifiWatchdogScreen::MacEqual> WifiWatchdogScreen::_beaconWindow;
std::unordered_map<WifiWatchdogScreen::MacAddr, WifiWatchdogScreen::BeaconEntry, WifiWatchdogScreen::MacHash, WifiWatchdogScreen::MacEqual> WifiWatchdogScreen::_beaconMap;
std::unordered_map<std::string, std::vector<WifiWatchdogScreen::BssidInfo>>                                                                  WifiWatchdogScreen::_twinMap;

WifiWatchdogScreen::DeauthEvent  WifiWatchdogScreen::_ring[MAX_RING]              = {};
volatile int                     WifiWatchdogScreen::_ringHead                    = 0;
volatile int                     WifiWatchdogScreen::_ringTail                    = 0;

WifiWatchdogScreen::SsidEvent    WifiWatchdogScreen::_ssidRing[MAX_RING]          = {};
volatile int                     WifiWatchdogScreen::_ssidRingHead                = 0;
volatile int                     WifiWatchdogScreen::_ssidRingTail                = 0;

WifiWatchdogScreen::BeaconEvent  WifiWatchdogScreen::_beaconRing[MAX_BEACON_RING] = {};
volatile int                     WifiWatchdogScreen::_beaconRingHead              = 0;
volatile int                     WifiWatchdogScreen::_beaconRingTail              = 0;

portMUX_TYPE WifiWatchdogScreen::_ringLock = portMUX_INITIALIZER_UNLOCKED;

// ── Title ─────────────────────────────────────────────────────────────────────

const char* WifiWatchdogScreen::title()
{
  static constexpr const char* kNames[] = {
    "Watchdog", "Deauth/Disassoc", "Beacon Flood", "Evil Twin"
  };
  if (_view == VIEW_TWIN_APS) return _twinAps.first.c_str();
  return kNames[_view];
}

// ── Destructor ────────────────────────────────────────────────────────────────

WifiWatchdogScreen::~WifiWatchdogScreen()
{
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  esp_wifi_set_promiscuous(false);
  _deauthMap.clear();
  _ssidMap.clear();
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
  _paused      = false;
  _holdHandled = false;
  _actionOpen  = false;
  _deauthSnap.clear();
  _floodSnap.clear();
  _twinSnap.clear();

  _ringHead = _ringTail = 0;
  _ssidRingHead = _ssidRingTail = 0;
  _beaconRingHead = _beaconRingTail = 0;

  _deauthMap.clear();
  _ssidMap.clear();
  _beaconWindow.clear();
  _beaconMap.clear();
  _twinMap.clear();

  _karmaBaseline = Achievement.getInt("wifi_karma_attack_detected");

  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&WifiWatchdogScreen::_promiscuousCb);

#ifdef DEVICE_HAS_TOUCH_NAV
  Uni.Nav->setSuppressKeys(true);
#endif

  if (_initialView != InitialView::None) {
    static constexpr View kInitialViewMap[] = {
      VIEW_DEAUTH, VIEW_FLOOD, VIEW_EVILTWIN
    };
    _enterView(kInitialViewMap[static_cast<int>(_initialView)]);
  } else {
    render();
  }
}

void WifiWatchdogScreen::_enterView(View view)
{
#ifdef DEVICE_HAS_TOUCH_NAV
  Uni.Nav->setSuppressKeys(false);
#endif
  _holdCell  = -1;
  _view      = view;
  _itemCount = 0;
  _paused    = false;
  _deauthSnap.clear();
  _floodSnap.clear();
  _twinSnap.clear();
  _scroll.setSelectable(false);
  _scroll.resetScroll();
  _renderView();
}

void WifiWatchdogScreen::onRestore()
{
  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&WifiWatchdogScreen::_promiscuousCb);

  // Back from a tool opened on a paused list: stay on that list and row.
  if (_actionOpen) {
    _actionOpen = false;
    esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);
    _lastUpdate = millis();
    render();
    return;
  }

  _view        = VIEW_OVERALL;
  _prevGridSel = -1;
  _holdCell    = -1;
  for (int i = 0; i < 4; i++) _prevCounts[i] = -1;

#ifdef DEVICE_HAS_TOUCH_NAV
  Uni.Nav->setSuppressKeys(true);
#endif
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
          const int idx = row * 2 + col;
          if (idx == 3) {
            Uni.Nav->setSuppressKeys(false);
            Screen.push(new WifiKarmaDetectorScreen());
            return;
          }
          static constexpr View kViewMap[] = {VIEW_DEAUTH, VIEW_FLOOD, VIEW_EVILTWIN};
          _enterView(kViewMap[idx]);
          return;
        }
      }
#else
      if (dir == INavigation::DIR_BACK) { onBack(); return; }

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
        if (_gridSel == 3) {
          Screen.push(new WifiKarmaDetectorScreen());
          return;
        }
        static constexpr View kViewMap[] = {VIEW_DEAUTH, VIEW_FLOOD, VIEW_EVILTWIN};
        _enterView(kViewMap[_gridSel]);
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
    // Like Traffic Analyzer. Live: [Press] pauses, [Back] returns to the grid.
    // Paused: arrows select, [Press] opens Options (Details first), [Back]
    // resumes; [Hold] opens Options too. Evil Twin networks: [Press] lists
    // the network's APs, where [Press] opens Options.
    const bool canAct = _paused && _view != VIEW_EVILTWIN;
    if (canAct && Uni.Nav->isPressed() && Uni.Nav->currentDirection() == INavigation::DIR_PRESS &&
        Uni.Nav->heldDuration() >= 700 && !_holdHandled) {
      _holdHandled = true;
      // Fire at the hold threshold and swallow the release, so the popup
      // can't take it as a selection.
      Uni.Nav->suppressCurrentPress();
      _openOptions();
      return;
    }
    if (_holdHandled && !Uni.Nav->isPressed()) _holdHandled = false;

    if (Uni.Nav->wasPressed()) {
      const auto     dir      = Uni.Nav->readDirection();
      const uint32_t duration = Uni.Nav->pressDuration();
      if (dir == INavigation::DIR_BACK) {
        if (_view == VIEW_TWIN_APS) {
          // Back to the paused network list, on the same network.
          _view = VIEW_EVILTWIN;
          _renderView();
          _scroll.setSelected(_twinSel);
          render();
        } else if (_paused) {
          _resume();
        } else {
          _leaveList();
        }
        return;
      }
      if (dir == INavigation::DIR_PRESS && duration < 700) {
        if (!_paused) {
          _pause();
        } else if (_view == VIEW_EVILTWIN) {
          const int index = _scroll.selected();
          if (index >= 0 && index < (int)_twinSnap.size()) {
            _twinSel = index;
            _twinAps = _twinSnap[index];
            _view    = VIEW_TWIN_APS;
            _scroll.setSelected(0);
            _renderView();
          }
        } else {
          _openOptions();
        }
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
    // A paused list keeps its snapshot; capture goes on in the background.
    if (!_paused) _renderView();
  }
}

void WifiWatchdogScreen::onRender()
{
  if (_view == VIEW_OVERALL) {
    _prevGridSel = -1;
    _renderOverall();
    return;
  }
  const int listH = _listHeight();
  if (_itemCount > 0) {
    _scroll.render(bodyX(), bodyY(), bodyW(), listH);
  } else {
    Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), listH, TFT_BLACK);
    Uni.Lcd.setTextDatum(MC_DATUM);
    Uni.Lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    Uni.Lcd.drawString(_paused ? "Nothing captured" : "Monitoring...",
                       bodyX() + bodyW() / 2, bodyY() + listH / 2);
  }
  _renderFooter();
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
      if (_deauthMap.size() < MAX_TRACKED_MAC) {
        DeauthEntry e{};
        e.timestamp  = ev.timestamp;
        e.counter    = 1;
        e.isDisassoc = ev.isDisassoc;
        e.target     = ev.target;
        e.rssi       = ev.rssi;
        e.channel    = ev.channel;
        auto ssidIt = _ssidMap.find(ev.mac);
        if (ssidIt != _ssidMap.end()) e.ssid = ssidIt->second;
        _deauthMap.emplace(ev.mac, e);
      }
      gotDeauth = true;
      if (Achievement.inc("wifi_deauth_detected") == 1)
        Achievement.unlock("wifi_deauth_detected");
    } else {
      if (it->second.counter < 1000) ++it->second.counter;
      it->second.timestamp  = ev.timestamp;
      it->second.isDisassoc = ev.isDisassoc;
      it->second.target     = ev.target;
      it->second.rssi       = ev.rssi;
      it->second.channel    = ev.channel;
    }
    _ringTail = (_ringTail + 1) % MAX_RING;
  }

  for (int i = 0; i < MAX_RING && _ssidRingTail != _ssidRingHead; i++) {
    const auto& ev = _ssidRing[_ssidRingTail];
    MacAddr bssid{};
    memcpy(bssid.data(), ev.bssid.data(), 6);
    if (_ssidMap.find(bssid) == _ssidMap.end() && _ssidMap.size() < MAX_TRACKED_MAC)
      _ssidMap.emplace(bssid, std::string(ev.ssid));

    // Build twin map: SSID → list of unique BSSIDs
    if (ev.ssid[0] != '\0') {
      std::string ssidKey(ev.ssid);
      auto twinIt = _twinMap.find(ssidKey);
      if (twinIt == _twinMap.end()) {
        if (_twinMap.size() < MAX_TRACKED_MAC)
          twinIt = _twinMap.emplace(ssidKey, std::vector<BssidInfo>{}).first;
      }
      if (twinIt != _twinMap.end()) {
        auto& list = twinIt->second;
        bool found = false;
        for (auto& b : list) {
          if (b.bssid == bssid) {
            found     = true;
            b.channel = ev.channel;
            b.rssi    = ev.rssi;
            break;
          }
        }
        if (!found && list.size() < 8) {
          list.push_back({bssid, ev.channel, ev.rssi});
          // Counted once, when a network first shows a second AP (it used to
          // be counted on every list refresh, writing storage every second).
          if (list.size() == 2 && Achievement.inc("wifi_evil_twin_detected") == 1)
            Achievement.unlock("wifi_evil_twin_detected");
        }
      }
    }

    _ssidRingTail = (_ssidRingTail + 1) % MAX_RING;
  }

  for (int i = 0; i < MAX_BEACON_RING && _beaconRingTail != _beaconRingHead; i++) {
    const auto& ev = _beaconRing[_beaconRingTail];
    MacAddr bssid{};
    memcpy(bssid.data(), ev.bssid.data(), 6);
    auto it = _beaconWindow.find(bssid);
    if (it == _beaconWindow.end()) {
      if (_beaconWindow.size() < MAX_TRACKED_MAC) {
        BeaconWindow w{};
        w.count   = 1;
        w.rssi    = ev.rssi;
        w.channel = ev.channel;
        auto ssidIt = _ssidMap.find(bssid);
        if (ssidIt != _ssidMap.end()) strncpy(w.ssid, ssidIt->second.c_str(), 32);
        _beaconWindow.emplace(bssid, w);
      }
    } else {
      if (it->second.count < 65535) ++it->second.count;
      it->second.rssi    = ev.rssi;
      it->second.channel = ev.channel;
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
      if (_beaconMap.size() < MAX_TRACKED_MAC) {
        BeaconEntry e{};
        strncpy(e.ssid, kv.second.ssid, 32);
        e.ratePerSec = kv.second.count;
        e.lastSeen   = now;
        e.rssi       = kv.second.rssi;
        e.channel    = kv.second.channel;
        _beaconMap.emplace(kv.first, e);
      }
    } else {
      it->second.ratePerSec = kv.second.count;
      it->second.lastSeen   = now;
      it->second.rssi       = kv.second.rssi;
      it->second.channel    = kv.second.channel;
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
    case VIEW_FLOOD:    _renderFlood();    break;
    case VIEW_EVILTWIN: _renderEviltwin(); break;
    case VIEW_TWIN_APS: _renderTwinAps();  break;
  }
}

// One row per network that more than one AP announces; [Press] on a paused
// row lists its APs.
void WifiWatchdogScreen::_renderEviltwin()
{
  std::vector<TwinItem> live;
  if (!_paused) _collectTwins(live);
  const std::vector<TwinItem>& items = _paused ? _twinSnap : live;

  int n = 0;
  for (const auto& kv : items) {
    if (n >= MAX_ITEMS) break;
    snprintf(_labels[n], sizeof(_labels[n]), "%s", kv.first.c_str());
    snprintf(_sublabels[n], sizeof(_sublabels[n]), "%d APs", (int)kv.second.size());
    _rows[n].label     = _labels[n];
    _rows[n].value     = _sublabels[n];
    _rows[n].secondary = nullptr;
    n++;
  }
  _setListState(n);
}

// The APs of the picked network: BSSID, channel and signal.
void WifiWatchdogScreen::_renderTwinAps()
{
  int n = 0;
  for (const auto& b : _twinAps.second) {
    if (n >= MAX_ITEMS) break;
    snprintf(_labels[n], sizeof(_labels[n]), "%02X:%02X:%02X:%02X:%02X:%02X",
             b.bssid[0], b.bssid[1], b.bssid[2], b.bssid[3], b.bssid[4], b.bssid[5]);
    snprintf(_sublabels[n], sizeof(_sublabels[n]), "CH%d %d", b.channel, b.rssi);
    _rows[n].label     = _labels[n];
    _rows[n].value     = _sublabels[n];
    _rows[n].secondary = nullptr;
    n++;
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

  int counts[4];
  counts[0] = (int)_deauthMap.size();
  counts[1] = 0;
  for (auto& kv : _beaconMap)
    if (kv.second.ratePerSec >= FLOOD_THRESHOLD) counts[1]++;
  counts[2] = 0;
  for (auto& kv : _twinMap)
    if ((int)kv.second.size() >= 2) counts[2]++;
  counts[3] = Achievement.getInt("wifi_karma_attack_detected") - _karmaBaseline;
  if (counts[3] < 0) counts[3] = 0;

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
  if (!_paused) {
    std::vector<MacAddr> toErase;
    for (auto& kv : _deauthMap)
      if (now - kv.second.timestamp > WINDOW_MS) toErase.push_back(kv.first);
    for (auto& k : toErase) _deauthMap.erase(k);
  }

  std::vector<DeauthItem> live;
  if (!_paused) _collectDeauth(live);
  const std::vector<DeauthItem>& items = _paused ? _deauthSnap : live;

  int n = 0;
  for (const auto& kv : items) {
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
    _rows[n].label     = _labels[n];
    _rows[n].value     = _sublabels[n];
    _rows[n].secondary = nullptr;
    n++;
  }

  _setListState(n);
}

void WifiWatchdogScreen::_renderFlood()
{
  std::vector<FloodItem> live;
  if (!_paused) _collectFlood(live);
  const std::vector<FloodItem>& items = _paused ? _floodSnap : live;

  int n = 0;
  for (const auto& kv : items) {
    if (n >= MAX_ITEMS) break;
    const MacAddr&     mac = kv.first;
    const BeaconEntry& e   = kv.second;
    const bool flood = e.ratePerSec >= FLOOD_THRESHOLD;
    if (e.ssid[0] != '\0')
      snprintf(_labels[n], sizeof(_labels[n]), "%s  %d/s", e.ssid, e.ratePerSec);
    else
      snprintf(_labels[n], sizeof(_labels[n]), "%02X:%02X:%02X:%02X:%02X:%02X  %d/s",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], e.ratePerSec);
    snprintf(_sublabels[n], sizeof(_sublabels[n]), "%s", flood ? "Beacon flood" : "Normal");
    _rows[n].label     = _labels[n];
    _rows[n].value     = _sublabels[n];
    _rows[n].secondary = nullptr;
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
    "Deauth", "Beacon Flood", "Evil Twin", "Karma"
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
      memcpy(_ring[_ringHead].target.data(), pay + 4, 6);
      _ring[_ringHead].timestamp  = millis();
      _ring[_ringHead].isDisassoc = (fc_sub == 0xA);
      _ring[_ringHead].rssi       = pkt->rx_ctrl.rssi;
      _ring[_ringHead].channel    = pkt->rx_ctrl.channel;
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
          _ssidRing[_ssidRingHead].rssi       = pkt->rx_ctrl.rssi;
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
      _beaconRing[_beaconRingHead].rssi    = pkt->rx_ctrl.rssi;
      _beaconRing[_beaconRingHead].channel = pkt->rx_ctrl.channel;
      _beaconRingHead = next;
    }
    portEXIT_CRITICAL_ISR(&_ringLock);
  }

}

// ── Pause / select / actions ──────────────────────────────────────────────────

static constexpr int kFooterH = 27;

void WifiWatchdogScreen::_collectDeauth(std::vector<DeauthItem>& out) const
{
  out.assign(_deauthMap.begin(), _deauthMap.end());
  std::sort(out.begin(), out.end(), [](const DeauthItem& a, const DeauthItem& b) {
    return a.second.timestamp > b.second.timestamp;  // most recent first
  });
}

void WifiWatchdogScreen::_collectFlood(std::vector<FloodItem>& out) const
{
  out.assign(_beaconMap.begin(), _beaconMap.end());
  std::sort(out.begin(), out.end(), [](const FloodItem& a, const FloodItem& b) {
    return a.second.ratePerSec > b.second.ratePerSec;  // busiest first
  });
}

void WifiWatchdogScreen::_collectTwins(std::vector<TwinItem>& out) const
{
  out.clear();
  for (const auto& kv : _twinMap)
    if (kv.second.size() >= 2) out.push_back(kv);
  std::sort(out.begin(), out.end(), [](const TwinItem& a, const TwinItem& b) {
    return a.second.size() > b.second.size();  // most APs first
  });
}

int WifiWatchdogScreen::_listHeight()
{
  return bodyH() - kFooterH;
}

// Hints follow Traffic Analyzer, with [Press] for Options. Selecting a network
// in Evil Twin needs no hint.
void WifiWatchdogScreen::_renderFooter()
{
  auto& lcd = Uni.Lcd;
  const int x = bodyX(), y = bodyY(), w = bodyW(), h = bodyH();
  const int footerTop = y + h - kFooterH;
  lcd.fillRect(x, footerTop, w, kFooterH, TFT_BLACK);
  lcd.drawFastHLine(x, footerTop, w, TFT_DARKGREY);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.setTextDatum(BC_DATUM);
  if (_view == VIEW_TWIN_APS) {
    // [Back] returns to the network list.
    lcd.drawString("[Press] Options", x + w / 2, y + h - 1);
  } else if (_paused && _view == VIEW_EVILTWIN) {
    const char* hint = "[Back] Resume Scan";
    if (lcd.textWidth(hint) > w - 6) hint = "[Back] Resume";
    lcd.drawString(hint, x + w / 2, y + h - 1);
  } else if (_paused) {
    const char* hint = "[Back] Resume Scan";
    if (lcd.textWidth(hint) > w - 6) hint = "[Back] Resume";
    lcd.drawString(hint, x + w / 2, y + h - 14);
    lcd.drawString("[Press] Options", x + w / 2, y + h - 1);
  } else {
    lcd.drawString("[Press] Pause", x + w / 2, y + h - 1);
  }
  lcd.setTextDatum(TL_DATUM);
}

void WifiWatchdogScreen::_pause()
{
  _paused = true;
  if (_view == VIEW_DEAUTH)        _collectDeauth(_deauthSnap);
  else if (_view == VIEW_FLOOD)    _collectFlood(_floodSnap);
  else if (_view == VIEW_EVILTWIN) _collectTwins(_twinSnap);
  _scroll.setSelectable(true);
  _scroll.setSelected(0);
  _renderView();
}

void WifiWatchdogScreen::_resume()
{
  _paused = false;
  _deauthSnap.clear();
  _floodSnap.clear();
  _twinSnap.clear();
  _scroll.setSelectable(false);
  _renderView();
}

void WifiWatchdogScreen::_leaveList()
{
  _paused = false;
  _deauthSnap.clear();
  _floodSnap.clear();
  _twinSnap.clear();
  _scroll.setSelectable(false);
  if (_initialView != InitialView::None) {
    onBack();
    return;
  }
  _view        = VIEW_OVERALL;
  _prevGridSel = -1;
#ifdef DEVICE_HAS_TOUCH_NAV
  Uni.Nav->drawOverlay();  // clear bar now, before suppress turns it into a no-op
  Uni.Nav->setSuppressKeys(true);
#endif
  Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  render();
}

// Watchdog targets are transmitters treated as APs: the deauth source, the
// flooding BSSID, or one AP of an Evil Twin network.
bool WifiWatchdogScreen::_selectedTarget(WifiTarget& target) const
{
  target = WifiTarget{};
  target.client = false;
  const int index = _scroll.selected();
  if (index < 0) return false;

  const uint8_t* mac = nullptr;
  const char*    ssid = nullptr;
  if (_view == VIEW_DEAUTH) {
    if (index >= (int)_deauthSnap.size()) return false;
    const auto& item = _deauthSnap[index];
    mac = item.first.data();
    ssid = item.second.ssid.c_str();
    target.channel = item.second.channel;
  } else if (_view == VIEW_FLOOD) {
    if (index >= (int)_floodSnap.size()) return false;
    const auto& item = _floodSnap[index];
    mac = item.first.data();
    ssid = item.second.ssid;
    target.channel = item.second.channel;
  } else if (_view == VIEW_TWIN_APS) {
    if (index >= (int)_twinAps.second.size()) return false;
    const auto& ap = _twinAps.second[index];
    mac = ap.bssid.data();
    ssid = _twinAps.first.c_str();
    target.channel = ap.channel;
  } else {
    return false;
  }
  memcpy(target.mac, mac, 6);
  memcpy(target.bssid, mac, 6);
  if (ssid) strncpy(target.ssid, ssid, sizeof(target.ssid) - 1);
  return true;
}

void WifiWatchdogScreen::_openSelectedDetails()
{
  const int index = _scroll.selected();
  if (index < 0) return;
  auto* details = new WifiChannelMonitorDetailsScreen();
  if (_view == VIEW_DEAUTH && index < (int)_deauthSnap.size()) {
    const auto& item = _deauthSnap[index];
    const DeauthEntry& e = item.second;
    details->configureDeauth(item.first.data(), e.ssid.c_str(), e.isDisassoc, e.counter,
                             e.target.data(), e.rssi, e.channel);
  } else if (_view == VIEW_FLOOD && index < (int)_floodSnap.size()) {
    const auto& item = _floodSnap[index];
    const BeaconEntry& e = item.second;
    details->configureBssid(e.ssid, item.first.data(), e.rssi, e.channel,
                            "Beacons", String(e.ratePerSec) + "/s");
  } else if (_view == VIEW_TWIN_APS && index < (int)_twinAps.second.size()) {
    const auto& ap = _twinAps.second[index];
    details->configureBssid(_twinAps.first.c_str(), ap.bssid.data(), ap.rssi, ap.channel);
  } else {
    delete details;
    return;
  }
  _actionOpen = true;
  _releaseRadio();
  Screen.push(details);
}

// No Deauth here: Watchdog defends.
void WifiWatchdogScreen::_openOptions()
{
  WifiTarget target;
  if (!_selectedTarget(target)) return;
  const auto action = WifiTargetActions::choose(
      WifiTargetActions::DETAILS | WifiTargetActions::MONITOR |
      WifiTargetActions::SNIFFER | WifiTargetActions::FOX_HUNT);
  if (action == WifiTargetActions::NONE) { render(); return; }
  if (action == WifiTargetActions::DETAILS) { _openSelectedDetails(); return; }
  _actionOpen = true;
  _releaseRadio();
  WifiTargetActions::open(action, target);
}

// Hand the radio over to the screen being opened; onRestore() takes it back.
void WifiWatchdogScreen::_releaseRadio()
{
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  delay(10);
  esp_wifi_set_promiscuous(false);
}
