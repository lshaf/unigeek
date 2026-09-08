#include "WifiWatchcatScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "core/ConfigManager.h"

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
    "WiFi Watchcat", "Probe Requests", "Authentication", "Association", "EAPOL"
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
    if (Uni.Nav->wasPressed()) {
      const auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        _view        = VIEW_OVERALL;
        _prevGridSel = -1;
#ifdef DEVICE_HAS_TOUCH_NAV
        Uni.Nav->drawOverlay();
        Uni.Nav->setSuppressKeys(true);
#endif
        Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
        render();
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
    _renderView();
  }
}

void WifiWatchcatScreen::onRender()
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
        if (ev.ssid[0] != '\0') memcpy(e.ssid, ev.ssid, 33);
        map->emplace(key, e);
      }
    } else {
      if (it->second.count < 9999) ++it->second.count;
      it->second.timestamp = ev.timestamp;
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

void WifiWatchcatScreen::_renderProbes()
{
  using ProbeItem = std::pair<MacAddr, ProbeEntry>;
  std::vector<ProbeItem> items;
  items.reserve(_probeMap.size());
  for (const auto& kv : _probeMap) items.push_back(kv);
  std::sort(items.begin(), items.end(), [](const ProbeItem& a, const ProbeItem& b) {
    return a.second.timestamp > b.second.timestamp;
  });

  int n = 0;
  int macCount = 0;
  for (const auto& kv : items) {
    if (macCount >= MAX_ITEMS || n >= MAX_ROWS) break;

    const MacAddr&    mac = kv.first;
    const ProbeEntry& e   = kv.second;

    snprintf(_labels[n], sizeof(_labels[n]),
             "STA %02X:%02X:%02X:%02X:%02X:%02X (x%d)",
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

void WifiWatchcatScreen::_renderActivity(
  const std::unordered_map<PairKey, ActivityEntry, PairHash, PairEqual>& map,
  bool showSsid)
{
  using ActivityItem = std::pair<PairKey, ActivityEntry>;
  std::vector<ActivityItem> items;
  items.reserve(map.size());
  for (const auto& kv : map) items.push_back(kv);
  std::sort(items.begin(), items.end(), [](const ActivityItem& a, const ActivityItem& b) {
    return a.second.timestamp > b.second.timestamp;
  });

  int n = 0;
  int pairCount = 0;
  for (const auto& kv : items) {
    if (pairCount >= MAX_ITEMS || n >= MAX_ROWS) break;

    const PairKey&       key = kv.first;
    const ActivityEntry& e   = kv.second;

    snprintf(_labels[n], sizeof(_labels[n]),
             "STA %02X:%02X:%02X:%02X:%02X:%02X (x%d)",
             key.sta[0], key.sta[1], key.sta[2], key.sta[3], key.sta[4], key.sta[5], e.count);
    _rows[n].label = _labels[n];
    _rows[n].value = "";
    n++;

    if (n < MAX_ROWS) {
      snprintf(_labels[n], sizeof(_labels[n]),
               "  BSSID %02X:%02X:%02X:%02X:%02X:%02X",
               key.ap[0], key.ap[1], key.ap[2], key.ap[3], key.ap[4], key.ap[5]);
      _rows[n].label = _labels[n];
      _rows[n].value = "";
      n++;
    }

    if (showSsid && e.ssid[0] != '\0' && n < MAX_ROWS) {
      snprintf(_labels[n], sizeof(_labels[n]), "  - %s", e.ssid);
      _rows[n].label = _labels[n];
      _rows[n].value = "";
      n++;
    }

    pairCount++;
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
                                    const uint8_t* ap, const char* ssid)
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
      _pushEvent(EVENT_PROBE, pay + 10, nullptr, ssid);
      return;
    }

    // Authentication. Address 3 is the BSSID; infer the station from the
    // transmitter/receiver so request and response frames share one entry.
    if (fcSub == 0xB) {
      const uint8_t* bssid = pay + 16;
      const uint8_t* sta   = memcmp(pay + 10, bssid, 6) == 0 ? pay + 4 : pay + 10;
      _pushEvent(EVENT_AUTH, sta, bssid);
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

      _pushEvent(EVENT_ASSOC, sta, bssid, ssid);
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

  _pushEvent(EVENT_EAPOL, sta, ap);
}
