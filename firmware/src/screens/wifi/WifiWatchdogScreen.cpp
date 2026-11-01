#include "WifiWatchdogScreen.h"
#include "screens/wifi/WifiChannelMonitorDetailsScreen.h"
#include "screens/wifi/WifiTargetActions.h"
#include "screens/wifi/WifiFoxHuntScreen.h"

#include <algorithm>
#include <cctype>
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
std::unordered_map<std::string, WifiWatchdogScreen::SpamEntry>                   WifiWatchdogScreen::_spamMap;
std::unordered_map<WifiWatchdogScreen::MacAddr, std::string, WifiWatchdogScreen::MacHash, WifiWatchdogScreen::MacEqual> WifiWatchdogScreen::_laaSsids;
unsigned long                                                                    WifiWatchdogScreen::_lastSpamMs = 0;

namespace {
// Random BSSIDs (beacon spam tools) are locally administered addresses.
inline bool isLocallyAdministered(const uint8_t* mac) { return (mac[0] & 0x02) != 0; }

// "Name 07" -> "Name ##" (suffix 7); other names unchanged (suffix -1).
std::string spamKey(const std::string& ssid, int& suffix) {
  suffix = -1;
  const size_t n = ssid.size();
  if (n >= 4 && ssid[n - 3] == ' ' && isdigit((unsigned char)ssid[n - 2]) &&
      isdigit((unsigned char)ssid[n - 1])) {
    suffix = (ssid[n - 2] - '0') * 10 + (ssid[n - 1] - '0');
    return ssid.substr(0, n - 2) + "##";
  }
  return ssid;
}
}  // namespace

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
  _spamMap.clear();
  _laaSsids.clear();
  _lastSpamMs = 0;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void WifiWatchdogScreen::onInit()
{
  _scroll.setResultStyle(true);  // Scanner-like rows: signal colour, grey detail
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
  _spamMap.clear();
  _laaSsids.clear();
  _lastSpamMs = 0;

  _karmaBaseline = Achievement.getInt("wifi_karma_attack_detected");
  _karmaRan      = false;

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
            _karmaRan = true;
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
          _karmaRan = true;
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

    // Random BSSIDs: remember their network for this second's spam check, and
    // while a spam is going on keep them out of the name and Evil Twin maps
    // (a spammed name would otherwise look like one network on many APs).
    const bool laa = isLocallyAdministered(bssid.data());
    if (laa && _laaSsids.size() < MAX_TRACKED_MAC) _laaSsids[bssid] = ev.ssid;
    const bool skip = laa && _lastSpamMs && millis() - _lastSpamMs < WINDOW_MS;

    if (!skip && _ssidMap.find(bssid) == _ssidMap.end() && _ssidMap.size() < MAX_TRACKED_MAC)
      _ssidMap.emplace(bssid, std::string(ev.ssid));

    // Build twin map: SSID → list of unique BSSIDs
    if (!skip && ev.ssid[0] != '\0') {
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
          // Counted once, when a network first shows a second AP with a
          // factory (not random) address: a beacon spam reuses names from
          // random addresses for a moment before it is recognised.
          if (!laa) {
            int factory = 0;
            for (const auto& b : list)
              if (!isLocallyAdministered(b.bssid.data())) factory++;
            if (factory == 2 && Achievement.inc("wifi_evil_twin_detected") == 1)
              Achievement.unlock("wifi_evil_twin_detected");
          }
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

  // Beacon spam: many random BSSIDs, each heard only in a short burst.
  std::vector<MacAddr> singles;
  for (auto& kv : _beaconWindow)
    if (isLocallyAdministered(kv.first.data()) && kv.second.count <= SPAM_MAX_BEACONS)
      singles.push_back(kv.first);
  const bool spam = (int)singles.size() >= SPAM_MIN_BSSIDS;

  if (spam) {
    _lastSpamMs = now;
    for (const auto& mac : singles) {
      auto nameIt = _laaSsids.find(mac);
      int suffix = -1;
      const std::string ssid = spamKey(nameIt != _laaSsids.end() ? nameIt->second : std::string(), suffix);
      const auto& w = _beaconWindow[mac];
      auto it = _spamMap.find(ssid);
      if (it == _spamMap.end()) {
        if (_spamMap.size() >= MAX_TRACKED_MAC) continue;
        it = _spamMap.emplace(ssid, SpamEntry{}).first;
        if (Achievement.inc("wifi_beacon_flood") == 1)  // once per spammed network
          Achievement.unlock("wifi_beacon_flood");
      }
      if (it->second.bssids < 9999) ++it->second.bssids;
      if (suffix >= 0) it->second.variants.set(suffix);
      it->second.lastSeen = now;
      it->second.rssi     = w.rssi;
      it->second.channel  = w.channel;

      // Throwaway addresses: drop them from the name and Evil Twin maps.
      _ssidMap.erase(mac);
      for (auto t = _twinMap.begin(); t != _twinMap.end();) {
        auto& list = t->second;
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [&](const BssidInfo& b) { return b.bssid == mac; }),
                   list.end());
        if (list.empty()) t = _twinMap.erase(t);
        else ++t;
      }
    }
  }
  _laaSsids.clear();

  for (auto& kv : _beaconWindow) {
    // Spam BSSIDs are counted per network above, not as single-address floods.
    if (spam && isLocallyAdministered(kv.first.data()) && kv.second.count <= SPAM_MAX_BEACONS)
      continue;
    auto it = _beaconMap.find(kv.first);
    const bool wasFlood = it != _beaconMap.end() && it->second.ratePerSec >= FLOOD_THRESHOLD;
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
    // Counted when a BSSID starts flooding, not every second it keeps going
    // (each count writes to storage).
    if (kv.second.count >= FLOOD_THRESHOLD && !wasFlood) {
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
  for (auto it = _spamMap.begin(); it != _spamMap.end();) {
    if (now - it->second.lastSeen > WINDOW_MS) it = _spamMap.erase(it);
    else ++it;
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
    _rows[n].hasRssi   = false;  // several APs, no single signal
    _rows[n].label     = _labels[n];
    _rows[n].value     = _sublabels[n];
    _rows[n].secondary = nullptr;
    n++;
  }
  _setListState(n);
}

// The APs of the picked network: BSSID and channel (signal in Details).
void WifiWatchdogScreen::_renderTwinAps()
{
  int n = 0;
  for (const auto& b : _twinAps.second) {
    if (n >= MAX_ITEMS) break;
    snprintf(_labels[n], sizeof(_labels[n]), "%02X:%02X:%02X:%02X:%02X:%02X",
             b.bssid[0], b.bssid[1], b.bssid[2], b.bssid[3], b.bssid[4], b.bssid[5]);
    snprintf(_sublabels[n], sizeof(_sublabels[n]), "CH%d", b.channel);
    _rows[n].rssi      = b.rssi;
    _rows[n].hasRssi   = b.rssi != 0;
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
  counts[1] = (int)_spamMap.size();
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
    // The network (or source MAC) and the frame count, like Traffic Analyzer's
    // "N pkts". Type and last-seen time are in Details.
    const MacAddr&     mac = kv.first;
    const DeauthEntry& e   = kv.second;
    if (!e.ssid.empty())
      snprintf(_labels[n], sizeof(_labels[n]), "%s", e.ssid.c_str());
    else
      snprintf(_labels[n], sizeof(_labels[n]), "%02X:%02X:%02X:%02X:%02X:%02X",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(_sublabels[n], sizeof(_sublabels[n]), e.counter >= 1000 ? "x999+" : "x%d", e.counter);
    _rows[n].rssi    = e.rssi;
    _rows[n].hasRssi = e.rssi != 0;
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
    // A flooding BSSID: Scanner's AP row, SSID then "BSSID CHx" (the beacon
    // rate is in Details). A spammed network: SSID then "N BSSIDs CHx".
    const FloodRow& r = kv;
    snprintf(_labels[n], sizeof(_labels[n]), "%s", r.ssid.empty() ? "<hidden>" : r.ssid.c_str());
    if (r.spam)
      snprintf(_sublabels[n], sizeof(_sublabels[n]), "%d BSSIDs CH%d", r.amount, r.channel);
    else
      snprintf(_sublabels[n], sizeof(_sublabels[n]), "%02X:%02X:%02X:%02X:%02X:%02X CH%d",
               r.bssid[0], r.bssid[1], r.bssid[2], r.bssid[3], r.bssid[4], r.bssid[5], r.channel);
    _rows[n].rssi    = r.rssi;
    _rows[n].hasRssi = r.rssi != 0;
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
  // Karma never monitors from this grid (it only runs inside Karma Detector):
  // its title is always dimmed, and its count reads "-" until it has run.
  const bool dimmed = (idx == 3);
  sp.setTextColor(held ? TFT_WHITE : (dimmed ? TFT_DARKGREY : TFT_LIGHTGREY), TFT_BLACK);
  sp.drawString(kNames[idx], cellW / 2, 8);
  char buf[6];
  if (idx == 3 && !_karmaRan) snprintf(buf, sizeof(buf), "-");
  else                        snprintf(buf, sizeof(buf), "%d", count);
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
  // Karma never monitors from this grid (it only runs inside Karma Detector):
  // its title is always dimmed, and its count reads "-" until it has run.
  const bool dimmed = (idx == 3);
  sp.setTextColor(sel ? TFT_WHITE : (dimmed ? TFT_DARKGREY : TFT_LIGHTGREY), TFT_BLACK);
  sp.drawString(kNames[idx], cellW / 2, 8);
  char buf[6];
  if (idx == 3 && !_karmaRan) snprintf(buf, sizeof(buf), "-");
  else                        snprintf(buf, sizeof(buf), "%d", count);
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
  // Most active first, like Traffic Analyzer (ties: most recent first).
  std::sort(out.begin(), out.end(), [](const DeauthItem& a, const DeauthItem& b) {
    if (a.second.counter != b.second.counter) return a.second.counter > b.second.counter;
    return a.second.timestamp > b.second.timestamp;
  });
}

// BSSIDs above the flood rate and spammed networks, like the grid count.
// Busiest first: beacons/s for a BSSID, BSSIDs seen for a spammed network.
void WifiWatchdogScreen::_collectFlood(std::vector<FloodItem>& out) const
{
  out.clear();
  for (const auto& kv : _beaconMap) {
    if (kv.second.ratePerSec < FLOOD_THRESHOLD) continue;
    FloodRow r;
    r.bssid   = kv.first;
    r.ssid    = kv.second.ssid;
    r.amount  = kv.second.ratePerSec;
    r.channel = kv.second.channel;
    r.rssi    = kv.second.rssi;
    out.push_back(r);
  }
  for (const auto& kv : _spamMap) {
    FloodRow r;
    r.spam     = true;
    r.ssid     = kv.first;
    r.amount   = kv.second.bssids;
    r.variants = (int)kv.second.variants.count();
    r.channel = kv.second.channel;
    r.rssi    = kv.second.rssi;
    out.push_back(r);
  }
  std::sort(out.begin(), out.end(), [](const FloodItem& a, const FloodItem& b) {
    return a.amount > b.amount;
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

// Hints follow Traffic Analyzer, with [Press] for Options; "[Press] Select"
// where Press opens a deeper list (Evil Twin networks).
void WifiWatchdogScreen::_renderFooter()
{
  auto& lcd = Uni.Lcd;
  const int x = bodyX(), y = bodyY(), w = bodyW(), h = bodyH();
  const int footerTop = y + h - kFooterH;
  lcd.fillRect(x, footerTop, w, kFooterH, TFT_BLACK);
  lcd.drawFastHLine(x, footerTop, w, TFT_DARKGREY);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.setTextDatum(BC_DATUM);
  // Shared layout: Options on the upper line, Pause / Resume on the lower.
  const char* resume = "[Back] Resume Scan";
  if (lcd.textWidth(resume) > w - 6) resume = "[Back] Resume";
  if (_view == VIEW_TWIN_APS) {
    // [Back] returns to the network list: no hint needed.
    lcd.drawString("[Press] Options", x + w / 2, y + h - 14);
  } else if (_paused && _view == VIEW_EVILTWIN) {
    // Press opens the network's APs; a network is not a device, so no Options.
    lcd.drawString("[Press] Select", x + w / 2, y + h - 14);
    lcd.drawString(resume, x + w / 2, y + h - 1);
  } else if (_paused) {
    lcd.drawString("[Press] Options", x + w / 2, y + h - 14);
    lcd.drawString(resume, x + w / 2, y + h - 1);
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
    const auto& row = _floodSnap[index];
    if (row.spam) return false;  // no single address
    mac = row.bssid.data();
    ssid = row.ssid.c_str();
    target.channel = row.channel;
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
                             e.target.data(), e.rssi, e.channel,
                             (millis() - e.timestamp) / 1000UL);
  } else if (_view == VIEW_FLOOD && index < (int)_floodSnap.size()) {
    const auto& row = _floodSnap[index];
    if (row.spam)
      details->configureSpam(row.ssid.c_str(), row.amount, row.rssi, row.channel, row.variants);
    else
      details->configureBssid(row.ssid.c_str(), row.bssid.data(), row.rssi, row.channel,
                              "Beacons", String(row.amount) + "/s");
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

// Every Watchdog list offers Details, Packet Monitor, Packet Sniffer and Fox
// Hunt (no Deauth: Watchdog defends). For a deauth source, Fox Hunt counts
// only deauth/disassoc frames from the address: when it is a forged copy of a
// real AP's ("Spoofed AP: Likely" in Details), the AP's own beacons stay out
// of the reading, which then follows the attacker.
void WifiWatchdogScreen::_openOptions()
{
  // A spammed network has no single address: Details and channel-wide
  // Packet Monitor / Sniffer only (no Fox Hunt).
  if (_view == VIEW_FLOOD) {
    const int index = _scroll.selected();
    if (index >= 0 && index < (int)_floodSnap.size() && _floodSnap[index].spam) {
      const auto action = WifiTargetActions::choose(
          WifiTargetActions::DETAILS | WifiTargetActions::MONITOR | WifiTargetActions::SNIFFER);
      if (action == WifiTargetActions::NONE) { render(); return; }
      if (action == WifiTargetActions::DETAILS) { _openSelectedDetails(); return; }
      _actionOpen = true;
      _releaseRadio();
      WifiTargetActions::openChannel(action, _floodSnap[index].channel);
      return;
    }
  }

  WifiTarget target;
  if (!_selectedTarget(target)) return;
  const auto action = WifiTargetActions::choose(
      WifiTargetActions::DETAILS | WifiTargetActions::MONITOR |
      WifiTargetActions::SNIFFER | WifiTargetActions::FOX_HUNT);
  if (action == WifiTargetActions::NONE) { render(); return; }
  if (action == WifiTargetActions::DETAILS) { _openSelectedDetails(); return; }
  _actionOpen = true;
  _releaseRadio();
  if (action == WifiTargetActions::FOX_HUNT && _view == VIEW_DEAUTH) {
    char name[48];
    if (target.ssid[0]) snprintf(name, sizeof(name), "Deauth: %s", target.ssid);
    else snprintf(name, sizeof(name), "Deauth: %02X:%02X:%02X:%02X:%02X:%02X",
                  target.mac[0], target.mac[1], target.mac[2],
                  target.mac[3], target.mac[4], target.mac[5]);
    auto* hunt = new WifiFoxHuntScreen();
    hunt->configureDeauthTarget(target.mac, target.channel, name);
    Screen.push(hunt);
    return;
  }
  WifiTargetActions::open(action, target);
}

// Hand the radio over to the screen being opened; onRestore() takes it back.
void WifiWatchdogScreen::_releaseRadio()
{
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  delay(10);
  esp_wifi_set_promiscuous(false);
}
