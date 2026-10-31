#include "WifiWatchcatScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "core/ConfigManager.h"
#include "screens/wifi/WifiChannelMonitorDetailsScreen.h"
#include "screens/wifi/WifiTargetActions.h"

#include <algorithm>
#include <cstring>

// ── Static definitions ────────────────────────────────────────────────────────

std::unordered_map<WifiWatchcatScreen::MacAddr, WifiWatchcatScreen::ProbeEntry,
                   WifiWatchcatScreen::MacHash, WifiWatchcatScreen::MacEqual>
  WifiWatchcatScreen::_probeMap;

std::unordered_map<WifiWatchcatScreen::PairKey, WifiWatchcatScreen::ActivityEntry,
                   WifiWatchcatScreen::PairHash, WifiWatchcatScreen::PairEqual>
  WifiWatchcatScreen::_authMap;
std::unordered_map<WifiWatchcatScreen::PairKey, WifiWatchcatScreen::ActivityEntry,
                   WifiWatchcatScreen::PairHash, WifiWatchcatScreen::PairEqual>
  WifiWatchcatScreen::_assocMap;
std::unordered_map<WifiWatchcatScreen::PairKey, WifiWatchcatScreen::ActivityEntry,
                   WifiWatchcatScreen::PairHash, WifiWatchcatScreen::PairEqual>
  WifiWatchcatScreen::_eapolMap;

WifiWatchcatScreen::ActivityEvent WifiWatchcatScreen::_ring[MAX_RING] = {};
volatile int WifiWatchcatScreen::_ringHead = 0;
volatile int WifiWatchcatScreen::_ringTail = 0;
portMUX_TYPE WifiWatchcatScreen::_ringLock = portMUX_INITIALIZER_UNLOCKED;

// ── Title / lifecycle ─────────────────────────────────────────────────────────

const char* WifiWatchcatScreen::title()
{
  static constexpr const char* kNames[] = {
    "Watchcat", "Probe Requests", "Authentication", "Association", "EAPOL"
  };
  return kNames[_view];
}

WifiWatchcatScreen::~WifiWatchcatScreen()
{
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  esp_wifi_set_promiscuous(false);
  _probeMap.clear();
  _authMap.clear();
  _assocMap.clear();
  _eapolMap.clear();
}

void WifiWatchcatScreen::onInit()
{
  _view        = VIEW_OVERALL;
  _channel     = 1;
  _itemCount   = 0;
  _gridSel     = 0;
  _prevGridSel = -1;
  _holdCell    = -1;
  for (int i = 0; i < 4; i++) _prevCounts[i] = -1;
  _lastUpdate = millis();
  _paused      = false;
  _holdHandled = false;
  _probeSnap.clear();
  _pairSnap.clear();

  _ringHead = _ringTail = 0;
  _probeMap.clear();
  _authMap.clear();
  _assocMap.clear();
  _eapolMap.clear();
  _scroll.resetScroll();

  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&WifiWatchcatScreen::_promiscuousCb);

#ifdef DEVICE_HAS_TOUCH_NAV
  Uni.Nav->setSuppressKeys(true);
#endif

  render();
}

void WifiWatchcatScreen::_enterView(View view)
{
#ifdef DEVICE_HAS_TOUCH_NAV
  Uni.Nav->setSuppressKeys(false);
#endif
  _holdCell  = -1;
  _view      = view;
  _itemCount = 0;
  _paused    = false;
  _probeSnap.clear();
  _pairSnap.clear();
  _scroll.setSelectable(false);
  _scroll.resetScroll();
  _renderView();
}

void WifiWatchcatScreen::onUpdate()
{
  if (_view == VIEW_OVERALL) {
    if (Uni.Nav->wasPressed()) {
      const auto dir = Uni.Nav->readDirection();

#ifdef DEVICE_HAS_TOUCH_NAV
      const int16_t tx = Uni.Nav->lastTouchX();
      const int16_t ty = Uni.Nav->lastTouchY();
      const int backW  = bodyW() / 6;
      if (dir == INavigation::DIR_BACK || (tx >= 0 && (int)tx < (int)bodyX() + backW)) {
        onBack();
        return;
      }
      if (tx >= 0) {
        const int gx  = (int)tx - (int)bodyX() - backW;
        const int gw  = bodyW() - backW;
        const int col = gx / (gw / 2);
        const int row = ((int)ty - (int)bodyY()) / (bodyH() / 2);
        if (col >= 0 && col < 2 && row >= 0 && row < 2) {
          static constexpr View kViewMap[] = {
            VIEW_PROBES, VIEW_AUTH, VIEW_ASSOC, VIEW_EAPOL
          };
          _enterView(kViewMap[row * 2 + col]);
          return;
        }
      }
#else
      if (dir == INavigation::DIR_BACK) {
        onBack();
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
        static constexpr View kViewMap[] = {
          VIEW_PROBES, VIEW_AUTH, VIEW_ASSOC, VIEW_EAPOL
        };
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
            newHold = 4;
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
    // Paused: arrows select, [Press] opens Options (Details first; there is
    // no deeper level to select, unlike Traffic Analyzer's AP list),
    // [Back] resumes. [Hold] opens Options too.
    if (_paused && Uni.Nav->isPressed() && Uni.Nav->currentDirection() == INavigation::DIR_PRESS &&
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
        if (_paused) _resume();
        else         _leaveList();
        return;
      }
      if (dir == INavigation::DIR_PRESS && duration < 700) {
        if (_paused) _openOptions();
        else         _pause();
        return;
      }
      if (dir == INavigation::DIR_UP   || dir == INavigation::DIR_DOWN ||
          dir == INavigation::DIR_LEFT || dir == INavigation::DIR_RIGHT) {
        _scroll.onNav(dir);
      }
    }
  }

  _drainRing();

  if (millis() - _lastUpdate >= 1000) {
    _lastUpdate = millis();
    _channel = (_channel % 13) + 1;
    esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);
    _prune();
    // A paused list keeps its snapshot; capture goes on in the background.
    if (!_paused) _renderView();
  }
}

void WifiWatchcatScreen::onRender()
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

void WifiWatchcatScreen::onBack()
{
#ifdef DEVICE_HAS_TOUCH_NAV
  Uni.Nav->setSuppressKeys(false);
#endif
  Screen.goBack();
}

// ── Event handling ────────────────────────────────────────────────────────────

void WifiWatchcatScreen::_drainRing()
{
  for (int i = 0; i < MAX_RING && _ringTail != _ringHead; i++) {
    const ActivityEvent ev = _ring[_ringTail];
    _ringTail = (_ringTail + 1) % MAX_RING;

    if (ev.kind == EVENT_PROBE) {
      auto it = _probeMap.find(ev.sta);
      if (it == _probeMap.end()) {
        if (_probeMap.size() < MAX_PROBE_ENTRIES) {
          ProbeEntry e{};
          e.timestamp = ev.timestamp;
          e.count     = 1;
          e.rssi      = ev.rssi;
          e.channel   = ev.channel;
          if (ev.ssid[0] != '\0') {
            memcpy(e.ssids[0], ev.ssid, 33);
            e.ssidCount = 1;
          }
          _probeMap.emplace(ev.sta, e);
        }
        if (Achievement.inc("wifi_probe_logged") == 1)
          Achievement.unlock("wifi_probe_logged");
      } else {
        if (it->second.count < 9999) ++it->second.count;
        it->second.timestamp = ev.timestamp;
        if (ev.rssi) it->second.rssi = ev.rssi;
        it->second.channel = ev.channel;
        if (ev.ssid[0] != '\0' && it->second.ssidCount < 3) {
          bool found = false;
          for (int j = 0; j < it->second.ssidCount; j++) {
            if (strcmp(it->second.ssids[j], ev.ssid) == 0) {
              found = true;
              break;
            }
          }
          if (!found)
            memcpy(it->second.ssids[it->second.ssidCount++], ev.ssid, 33);
        }
      }
      continue;
    }

    PairKey key{ev.sta, ev.ap};
    auto* map = &_authMap;
    if (ev.kind == EVENT_ASSOC) map = &_assocMap;
    else if (ev.kind == EVENT_EAPOL) map = &_eapolMap;

    auto it = map->find(key);
    if (it == map->end()) {
      if (map->size() < MAX_ACTIVITY_ENTRIES) {
        ActivityEntry e{};
        e.timestamp = ev.timestamp;
        e.count     = 1;
        e.rssi      = ev.rssi;
        e.channel   = ev.channel;
        if (ev.ssid[0] != '\0') memcpy(e.ssid, ev.ssid, 33);
        map->emplace(key, e);
      }
    } else {
      if (it->second.count < 9999) ++it->second.count;
      it->second.timestamp = ev.timestamp;
      if (ev.rssi) it->second.rssi = ev.rssi;
      it->second.channel = ev.channel;
      if (it->second.ssid[0] == '\0' && ev.ssid[0] != '\0')
        memcpy(it->second.ssid, ev.ssid, 33);
    }
  }
}

void WifiWatchcatScreen::_prune()
{
  const unsigned long now = millis();

  std::vector<MacAddr> probeErase;
  for (const auto& kv : _probeMap)
    if (now - kv.second.timestamp > WINDOW_MS) probeErase.push_back(kv.first);
  for (const auto& k : probeErase) _probeMap.erase(k);

  std::vector<PairKey> pairErase;
  for (const auto& kv : _authMap)
    if (now - kv.second.timestamp > WINDOW_MS) pairErase.push_back(kv.first);
  for (const auto& k : pairErase) _authMap.erase(k);

  pairErase.clear();
  for (const auto& kv : _assocMap)
    if (now - kv.second.timestamp > WINDOW_MS) pairErase.push_back(kv.first);
  for (const auto& k : pairErase) _assocMap.erase(k);

  pairErase.clear();
  for (const auto& kv : _eapolMap)
    if (now - kv.second.timestamp > WINDOW_MS) pairErase.push_back(kv.first);
  for (const auto& k : pairErase) _eapolMap.erase(k);
}

// ── Rendering ─────────────────────────────────────────────────────────────────

void WifiWatchcatScreen::_renderView()
{
  switch (_view) {
    case VIEW_OVERALL: _renderOverall(); break;
    case VIEW_PROBES:  _renderProbes(); break;
    case VIEW_AUTH:    _renderActivity(_authMap, false); break;
    case VIEW_ASSOC:   _renderActivity(_assocMap, true); break;
    case VIEW_EAPOL:   _renderActivity(_eapolMap, false); break;
  }
}

void WifiWatchcatScreen::_renderOverall()
{
  const int counts[4] = {
    (int)_probeMap.size(),
    (int)_authMap.size(),
    (int)_assocMap.size(),
    (int)_eapolMap.size()
  };
  const bool forceAll = (_prevGridSel < 0);

#ifdef DEVICE_HAS_TOUCH_NAV
  if (forceAll) _drawBackButton();
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

// ── Lists: one row per item ───────────────────────────────────────────────────

const std::unordered_map<WifiWatchcatScreen::PairKey, WifiWatchcatScreen::ActivityEntry,
                         WifiWatchcatScreen::PairHash, WifiWatchcatScreen::PairEqual>*
WifiWatchcatScreen::_pairMap() const
{
  if (_view == VIEW_AUTH)  return &_authMap;
  if (_view == VIEW_ASSOC) return &_assocMap;
  if (_view == VIEW_EAPOL) return &_eapolMap;
  return nullptr;
}

// Most recent first.
void WifiWatchcatScreen::_collectProbes(std::vector<ProbeItem>& out) const
{
  out.clear();
  out.reserve(_probeMap.size());
  for (const auto& kv : _probeMap) out.push_back(kv);
  std::sort(out.begin(), out.end(), [](const ProbeItem& a, const ProbeItem& b) {
    return a.second.timestamp > b.second.timestamp;
  });
  if (out.size() > (size_t)MAX_ROWS) out.resize(MAX_ROWS);
}

void WifiWatchcatScreen::_collectPairs(std::vector<PairItem>& out) const
{
  out.clear();
  const auto* map = _pairMap();
  if (!map) return;
  out.reserve(map->size());
  for (const auto& kv : *map) out.push_back(kv);
  std::sort(out.begin(), out.end(), [](const PairItem& a, const PairItem& b) {
    return a.second.timestamp > b.second.timestamp;
  });
  if (out.size() > (size_t)MAX_ROWS) out.resize(MAX_ROWS);
}

// Station MAC on the left; the first probed network (+N more) on the right.
// Everything else is in Details.
void WifiWatchcatScreen::_renderProbes()
{
  std::vector<ProbeItem> live;
  if (!_paused) _collectProbes(live);
  const std::vector<ProbeItem>& items = _paused ? _probeSnap : live;

  int n = 0;
  for (const auto& kv : items) {
    if (n >= MAX_ROWS) break;
    const MacAddr&    mac = kv.first;
    const ProbeEntry& e   = kv.second;
    snprintf(_labels[n], sizeof(_labels[n]), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    _rows[n].label = _labels[n];
    if (e.ssidCount == 0)     _rows[n].value = "(wildcard)";
    else if (e.ssidCount > 1) _rows[n].value = String(e.ssids[0]) + " +" + String(e.ssidCount - 1);
    else                      _rows[n].value = e.ssids[0];
    _rows[n].secondary = nullptr;
    n++;
  }
  _setListState(n);
}

// Station MAC on the left; the network (or the AP's short BSSID) on the right.
void WifiWatchcatScreen::_renderActivity(
  const std::unordered_map<PairKey, ActivityEntry, PairHash, PairEqual>& map,
  bool showSsid)
{
  (void)map;
  std::vector<PairItem> live;
  if (!_paused) _collectPairs(live);
  const std::vector<PairItem>& items = _paused ? _pairSnap : live;

  int n = 0;
  for (const auto& kv : items) {
    if (n >= MAX_ROWS) break;
    const PairKey&       key = kv.first;
    const ActivityEntry& e   = kv.second;
    snprintf(_labels[n], sizeof(_labels[n]), "%02X:%02X:%02X:%02X:%02X:%02X",
             key.sta[0], key.sta[1], key.sta[2], key.sta[3], key.sta[4], key.sta[5]);
    _rows[n].label = _labels[n];
    if (showSsid && e.ssid[0] != '\0') {
      _rows[n].value = e.ssid;
    } else {
      char ap[16];
      snprintf(ap, sizeof(ap), "AP ..%02X:%02X:%02X", key.ap[3], key.ap[4], key.ap[5]);
      _rows[n].value = ap;
    }
    _rows[n].secondary = nullptr;
    n++;
  }
  _setListState(n);
}

void WifiWatchcatScreen::_setListState(int newCount)
{
  _itemCount = newCount;
  _scroll.setRows(_rows, (uint8_t)_itemCount);
  render();
}

void WifiWatchcatScreen::_drawBackButton()
{
#ifdef DEVICE_HAS_TOUCH_NAV
  const int backW = bodyW() / 6;
  const bool held = (_holdCell == 4);
  Sprite back(&Uni.Lcd);
  back.createSprite(backW, bodyH());
  back.fillSprite(TFT_BLACK);
  back.drawRoundRect(2, 2, backW - 4, bodyH() - 4, 6,
                     held ? Config.getThemeColor() : 0x2104);
  back.setTextDatum(MC_DATUM);
  back.setTextColor(held ? TFT_WHITE : TFT_DARKGREY, TFT_BLACK);
  back.drawString("<", backW / 2, bodyH() / 2);
  back.pushSprite(bodyX(), bodyY());
  back.deleteSprite();
#endif
}

void WifiWatchcatScreen::_drawGridCell(int idx, int count)
{
  static constexpr const char* kNames[] = {
    "Probe Requests", "Authentication", "Association", "EAPOL"
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
  sp.drawRoundRect(2, 2, cellW - 4, cellH - 4, 4,
                   held ? Config.getThemeColor() : 0x2104);
  sp.setTextSize(1);
  sp.setTextDatum(TC_DATUM);
  sp.setTextColor(held ? TFT_WHITE : TFT_LIGHTGREY, TFT_BLACK);
  sp.drawString(kNames[idx], cellW / 2, 8);
  char buf[6];
  snprintf(buf, sizeof(buf), "%d", count);
  sp.setTextSize(2);
  sp.setTextDatum(MC_DATUM);
  sp.setTextColor(count > 0 ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
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
  sp.drawRoundRect(2, 2, cellW - 4, cellH - 4, 4,
                   sel ? Config.getThemeColor() : 0x2104);
  sp.setTextSize(1);
  sp.setTextDatum(TC_DATUM);
  sp.setTextColor(sel ? TFT_WHITE : TFT_LIGHTGREY, TFT_BLACK);
  sp.drawString(kNames[idx], cellW / 2, 8);
  char buf[6];
  snprintf(buf, sizeof(buf), "%d", count);
  sp.setTextSize(2);
  sp.setTextDatum(MC_DATUM);
  sp.setTextColor(count > 0 ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
  sp.drawString(buf, cellW / 2, cellH / 2 + 4);
  sp.setTextSize(1);
  sp.pushSprite(px, py);
  sp.deleteSprite();
#endif
}

// ── Promiscuous callback ──────────────────────────────────────────────────────

void WifiWatchcatScreen::_pushEvent(EventKind kind, const uint8_t* sta,
                                    const uint8_t* ap, const char* ssid,
                                    int8_t rssi, uint8_t channel)
{
  portENTER_CRITICAL_ISR(&_ringLock);
  const int next = (_ringHead + 1) % MAX_RING;
  if (next != _ringTail) {
    ActivityEvent& ev = _ring[_ringHead];
    ev.kind = kind;
    memcpy(ev.sta.data(), sta, 6);
    if (ap) memcpy(ev.ap.data(), ap, 6);
    else    ev.ap.fill(0);
    memset(ev.ssid, 0, sizeof(ev.ssid));
    if (ssid) strncpy(ev.ssid, ssid, sizeof(ev.ssid) - 1);
    ev.timestamp = millis();
    ev.rssi      = rssi;
    ev.channel   = channel;
    _ringHead = next;
  }
  portEXIT_CRITICAL_ISR(&_ringLock);
}

void WifiWatchcatScreen::_promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type)
{
  if (buf == nullptr) return;

  const auto     pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* pay = pkt->payload;
  const size_t   len = pkt->rx_ctrl.sig_len;
  if (len < 24) return;

  const int8_t  rssi    = pkt->rx_ctrl.rssi;
  const uint8_t channel = pkt->rx_ctrl.channel;
  // The signal only describes the station when it sent the frame (address 2).
  auto staRssi = [&](const uint8_t* sta) -> int8_t {
    return memcmp(pay + 10, sta, 6) == 0 ? rssi : 0;
  };

  const uint8_t fcSub  = (pay[0] >> 4) & 0x0F;
  const uint8_t fcType = (pay[0] >> 2) & 0x03;

  if (type == WIFI_PKT_MGMT && fcType == 0) {
    // Probe Request
    if (fcSub == 0x4) {
      char ssid[33] = {};
      size_t pos = 24;
      while (pos + 2 <= len) {
        const uint8_t id   = pay[pos];
        const uint8_t elen = pay[pos + 1];
        if (pos + 2 + elen > len) break;
        if (id == 0) {
          if (elen > 0 && elen <= 32) {
            memcpy(ssid, pay + pos + 2, elen);
            ssid[elen] = '\0';
          }
          break;
        }
        pos += 2 + elen;
      }
      _pushEvent(EVENT_PROBE, pay + 10, nullptr, ssid, rssi, channel);
      return;
    }

    // Authentication. Address 3 is the BSSID; infer the station from the
    // transmitter/receiver so request and response frames share one entry.
    if (fcSub == 0xB) {
      const uint8_t* bssid = pay + 16;
      const uint8_t* sta   = memcmp(pay + 10, bssid, 6) == 0 ? pay + 4 : pay + 10;
      _pushEvent(EVENT_AUTH, sta, bssid, nullptr, staRssi(sta), channel);
      return;
    }

    // Association / Reassociation request + response.
    if (fcSub <= 0x3) {
      const uint8_t* bssid = pay + 16;
      const uint8_t* sta   = memcmp(pay + 10, bssid, 6) == 0 ? pay + 4 : pay + 10;
      char ssid[33] = {};

      // Requests carry tagged parameters after their fixed fields.
      size_t pos = 0;
      if (fcSub == 0x0) pos = 28;       // Association Request
      else if (fcSub == 0x2) pos = 34;  // Reassociation Request
      if (pos > 0 && pos + 2 <= len) {
        while (pos + 2 <= len) {
          const uint8_t id   = pay[pos];
          const uint8_t elen = pay[pos + 1];
          if (pos + 2 + elen > len) break;
          if (id == 0 && elen > 0 && elen <= 32) {
            memcpy(ssid, pay + pos + 2, elen);
            ssid[elen] = '\0';
            break;
          }
          pos += 2 + elen;
        }
      }

      _pushEvent(EVENT_ASSOC, sta, bssid, ssid, staRssi(sta), channel);
      return;
    }

    return;
  }

  if (type != WIFI_PKT_DATA || fcType != 2) return;

  const bool toDS   = (pay[1] & 0x01) != 0;
  const bool fromDS = (pay[1] & 0x02) != 0;
  size_t hdr = 24;
  if (toDS && fromDS) {
    if (len < 30) return;
    hdr = 30;
  }
  if (fcSub & 0x08) hdr += 2; // QoS Control
  if (len < hdr + 8) return;

  if (!(pay[hdr]     == 0xAA && pay[hdr + 1] == 0xAA && pay[hdr + 2] == 0x03 &&
        pay[hdr + 6] == 0x88 && pay[hdr + 7] == 0x8E)) return;

  // Standard infrastructure frames provide an unambiguous STA/BSSID pair.
  const uint8_t* sta = nullptr;
  const uint8_t* ap  = nullptr;
  if (toDS && !fromDS) {
    ap  = pay + 4;
    sta = pay + 10;
  } else if (!toDS && fromDS) {
    sta = pay + 4;
    ap  = pay + 10;
  } else if (!toDS && !fromDS) {
    ap  = pay + 16;
    sta = memcmp(pay + 10, ap, 6) == 0 ? pay + 4 : pay + 10;
  } else {
    return; // WDS: no single infrastructure STA/AP pair to report.
  }

  _pushEvent(EVENT_EAPOL, sta, ap, nullptr, staRssi(sta), channel);
}

// ── Pause / select / actions ──────────────────────────────────────────────────

static constexpr int kFooterH = 27;

int WifiWatchcatScreen::_listHeight()
{
  return bodyH() - kFooterH;
}

// Hints follow Traffic Analyzer, with [Press] for Options.
void WifiWatchcatScreen::_renderFooter()
{
  auto& lcd = Uni.Lcd;
  const int x = bodyX(), y = bodyY(), w = bodyW(), h = bodyH();
  const int footerTop = y + h - kFooterH;
  lcd.fillRect(x, footerTop, w, kFooterH, TFT_BLACK);
  lcd.drawFastHLine(x, footerTop, w, TFT_DARKGREY);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.setTextDatum(BC_DATUM);
  if (_paused) {
    const char* hint = "[Back] Resume Scan";
    if (lcd.textWidth(hint) > w - 6) hint = "[Back] Resume";
    lcd.drawString(hint, x + w / 2, y + h - 14);
    lcd.drawString("[Press] Options", x + w / 2, y + h - 1);
  } else {
    lcd.drawString("[Press] Pause", x + w / 2, y + h - 1);
  }
  lcd.setTextDatum(TL_DATUM);
}

void WifiWatchcatScreen::_pause()
{
  _paused = true;
  if (_view == VIEW_PROBES) _collectProbes(_probeSnap);
  else                      _collectPairs(_pairSnap);
  _scroll.setSelectable(true);
  _scroll.setSelected(0);
  _renderView();
}

void WifiWatchcatScreen::_resume()
{
  _paused = false;
  _probeSnap.clear();
  _pairSnap.clear();
  _scroll.setSelectable(false);
  _renderView();
}

void WifiWatchcatScreen::_leaveList()
{
  _paused = false;
  _probeSnap.clear();
  _pairSnap.clear();
  _scroll.setSelectable(false);
  _view        = VIEW_OVERALL;
  _prevGridSel = -1;
#ifdef DEVICE_HAS_TOUCH_NAV
  Uni.Nav->drawOverlay();
  Uni.Nav->setSuppressKeys(true);
#endif
  Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  render();
}

bool WifiWatchcatScreen::_selectedTarget(WifiTarget& target) const
{
  target = WifiTarget{};
  const int index = _scroll.selected();
  if (_view == VIEW_PROBES) {
    if (index < 0 || index >= (int)_probeSnap.size()) return false;
    const auto& item = _probeSnap[index];
    target.client  = true;
    target.channel = item.second.channel;
    memcpy(target.mac, item.first.data(), 6);
    return true;
  }
  if (index < 0 || index >= (int)_pairSnap.size()) return false;
  const auto& item = _pairSnap[index];
  target.client  = true;
  target.hasAp   = true;
  target.channel = item.second.channel;
  memcpy(target.mac, item.first.sta.data(), 6);
  memcpy(target.bssid, item.first.ap.data(), 6);
  memcpy(target.ssid, item.second.ssid, sizeof(target.ssid));
  return true;
}

void WifiWatchcatScreen::_openSelectedDetails()
{
  const int index = _scroll.selected();
  auto* details = new WifiChannelMonitorDetailsScreen();
  if (_view == VIEW_PROBES) {
    if (index < 0 || index >= (int)_probeSnap.size()) { delete details; return; }
    const auto& item = _probeSnap[index];
    const ProbeEntry& e = item.second;
    details->configureProbe(item.first.data(), e.rssi, e.channel, e.count, e.ssids, e.ssidCount);
  } else {
    if (index < 0 || index >= (int)_pairSnap.size()) { delete details; return; }
    const auto& item = _pairSnap[index];
    const ActivityEntry& e = item.second;
    details->configureClient(item.first.sta.data(), e.rssi, e.channel, e.count,
                             item.first.ap.data(), e.ssid);
  }
  _releaseRadio();
  Screen.push(details);
}

// No Deauth here: Watchcat observes. Station/AP pairs can hunt either side.
void WifiWatchcatScreen::_openOptions()
{
  WifiTarget target;
  if (!_selectedTarget(target)) return;
  uint8_t allowed = WifiTargetActions::DETAILS | WifiTargetActions::MONITOR |
                    WifiTargetActions::SNIFFER | WifiTargetActions::FOX_HUNT;
  if (_view != VIEW_PROBES) allowed |= WifiTargetActions::FOX_HUNT_AP;

  const auto action = WifiTargetActions::choose(allowed);
  if (action == WifiTargetActions::NONE) { render(); return; }
  if (action == WifiTargetActions::DETAILS) { _openSelectedDetails(); return; }
  _releaseRadio();
  WifiTargetActions::open(action, target);
}

// Hand the radio over to the screen being opened; onRestore() takes it back.
void WifiWatchcatScreen::_releaseRadio()
{
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  delay(10);
  esp_wifi_set_promiscuous(false);
}

void WifiWatchcatScreen::onRestore()
{
  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(&WifiWatchcatScreen::_promiscuousCb);
  esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(true);
  _lastUpdate = millis();
  render();
}
