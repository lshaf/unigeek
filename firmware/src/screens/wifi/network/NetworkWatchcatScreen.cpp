#include "NetworkWatchcatScreen.h"
#include "core/ScreenManager.h"
#include "core/ConfigManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "screens/wifi/network/NetworkHostActions.h"
#include <algorithm>
#include <cstring>

NetworkWatchcatScreen* NetworkWatchcatScreen::_active = nullptr;
NetworkWatchcatScreen::Event NetworkWatchcatScreen::_ring[MAX_RING];
volatile int NetworkWatchcatScreen::_ringHead = 0;
volatile int NetworkWatchcatScreen::_ringTail = 0;
portMUX_TYPE NetworkWatchcatScreen::_ringLock = portMUX_INITIALIZER_UNLOCKED;

static constexpr int kFooterH = 27;

static bool netPayload(const uint8_t* pay, size_t len,
                       const uint8_t** out, size_t* outLen, uint16_t* et) {
  if (len < 32) return false;
  if (((pay[0] >> 2) & 0x03) != 2) return false;
  size_t hdr = 24;
  if ((pay[1] & 0x03) == 0x03) hdr = 30;
  if (pay[0] & 0x80) hdr += 2;
  if (len < hdr + 8) return false;
  if (pay[hdr] != 0xAA || pay[hdr + 1] != 0xAA || pay[hdr + 2] != 0x03) return false;
  *et = ((uint16_t)pay[hdr + 6] << 8) | pay[hdr + 7];
  *out = pay + hdr + 8;
  *outLen = len - hdr - 8;
  return true;
}

void NetworkWatchcatScreen::_promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_DATA || !buf || !_active) return;
  const auto* pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* pay = pkt->payload;
  const size_t len = pkt->rx_ctrl.sig_len;
  const uint8_t* pl = nullptr;
  size_t plLen = 0;
  uint16_t et = 0;
  if (!netPayload(pay, len, &pl, &plLen, &et)) return;

  Event ev{};
  if (et == 0x0806 && plLen >= 28) {
    const uint16_t op = ((uint16_t)pl[6] << 8) | pl[7];
    if (op != 2) return;
    ev.kind = 1;
    memcpy(ev.mac, pl + 8, 6);
    memcpy(&ev.ip, pl + 14, 4);
  } else if (et == 0x0800 && plLen >= 28) {
    if ((pl[0] >> 4) != 4) return;
    const uint8_t ihl = (pl[0] & 0x0F) * 4;
    if (pl[9] != 17 || plLen < ihl + 8) return;
    const uint8_t* udp = pl + ihl;
    const uint16_t srcPort = ((uint16_t)udp[0] << 8) | udp[1];
    const uint16_t dstPort = ((uint16_t)udp[2] << 8) | udp[3];
    const uint8_t* body = udp + 8;
    const size_t bodyLen = plLen - ihl - 8;
    if ((srcPort == 5353 || dstPort == 5353 || srcPort == 1900 || dstPort == 1900) && bodyLen > 8) {
      ev.kind = 2;
      memcpy(&ev.ip, pl + 12, 4);
      const char* tag = (srcPort == 1900 || dstPort == 1900) ? "SSDP" : "mDNS";
      snprintf(ev.name, sizeof(ev.name), "%s", tag);
    } else if (srcPort == 53 && bodyLen > 12 && (body[2] & 0x80)) {
      size_t p = 12;
      char name[28] = {};
      size_t ni = 0;
      while (p < bodyLen && body[p] && ni + 1 < sizeof(name)) {
        uint8_t lab = body[p++];
        if (lab > 63 || p + lab > bodyLen) break;
        if (ni) name[ni++] = '.';
        for (uint8_t k = 0; k < lab && ni + 1 < sizeof(name); ++k) name[ni++] = body[p++];
      }
      name[ni] = 0;
      while (p < bodyLen && body[p]) p += 1 + body[p];
      if (p < bodyLen) ++p;
      p += 4;
      if (p + 10 > bodyLen) return;
      const uint16_t rtype = ((uint16_t)body[p] << 8) | body[p + 1];
      const uint16_t rdlen = ((uint16_t)body[p + 8] << 8) | body[p + 9];
      if (rtype != 1 || rdlen != 4 || p + 10 + 4 > bodyLen) return;
      ev.kind = 3;
      memcpy(&ev.ip, body + p + 10, 4);
      strncpy(ev.name, name, sizeof(ev.name) - 1);
    } else {
      return;
    }
  } else {
    return;
  }

  portENTER_CRITICAL_ISR(&_ringLock);
  int next = (_ringHead + 1) % MAX_RING;
  if (next != _ringTail) {
    _ring[_ringHead] = ev;
    _ringHead = next;
  }
  portEXIT_CRITICAL_ISR(&_ringLock);
}

const char* NetworkWatchcatScreen::_ipStr(uint32_t ip, char* out, size_t n) {
  snprintf(out, n, "%u.%u.%u.%u",
           (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
           (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
  return out;
}

void NetworkWatchcatScreen::_pushHit(std::vector<Hit>& list, const Hit& h) {
  for (auto& e : list) {
    if (e.ip == h.ip && strcmp(e.value, h.value) == 0) { e.seen = h.seen; return; }
  }
  if ((int)list.size() >= MAX_LIST) list.erase(list.begin());
  list.push_back(h);
}

void NetworkWatchcatScreen::_noteArp(uint32_t ip, const uint8_t* mac) {
  if (!ip || ip == _selfIp) return;
  for (uint8_t i = 0; i < _arpCount; ++i) {
    if (_arp[i].ip != ip) continue;
    if (memcmp(_arp[i].mac, mac, 6) != 0) {
      Hit h; h.ip = ip; h.seen = millis();
      snprintf(h.value, sizeof(h.value), "MAC changed");
      _pushHit(_arpChanges, h);
      memcpy(_arp[i].mac, mac, 6);
    }
    return;
  }
  if (_arpCount >= MAX_ARP) return;
  _arp[_arpCount].ip = ip;
  memcpy(_arp[_arpCount].mac, mac, 6);
  ++_arpCount;
  Hit h; h.ip = ip; h.seen = millis();
  snprintf(h.value, sizeof(h.value), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  _pushHit(_hosts, h);
}

void NetworkWatchcatScreen::_noteService(const char* name, uint32_t ip) {
  if (!name || !ip) return;
  Hit h; h.ip = ip; h.seen = millis();
  snprintf(h.value, sizeof(h.value), "%s", name);
  _pushHit(_services, h);
}

void NetworkWatchcatScreen::_noteName(const char* name, uint32_t ip) {
  if (!name || !name[0] || !ip) return;
  Hit h; h.ip = ip; h.seen = millis();
  snprintf(h.value, sizeof(h.value), "%s", name);
  _pushHit(_names, h);
}

void NetworkWatchcatScreen::_drain() {
  for (;;) {
    Event ev;
    portENTER_CRITICAL(&_ringLock);
    if (_ringTail == _ringHead) { portEXIT_CRITICAL(&_ringLock); break; }
    ev = _ring[_ringTail];
    _ringTail = (_ringTail + 1) % MAX_RING;
    portEXIT_CRITICAL(&_ringLock);
    if (ev.kind == 1) _noteArp(ev.ip, ev.mac);
    else if (ev.kind == 2) _noteService(ev.name, ev.ip);
    else if (ev.kind == 3) _noteName(ev.name, ev.ip);
  }
}

void NetworkWatchcatScreen::_startRadio() {
  if (WiFi.getMode() != WIFI_STA) WiFi.mode(WIFI_STA);
  _active = this;
  _selfIp = (uint32_t)WiFi.localIP();
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(&_promiscuousCb);
  esp_wifi_set_promiscuous(true);
}

void NetworkWatchcatScreen::_stopRadio() {
  if (_active == this) _active = nullptr;
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  esp_wifi_set_promiscuous(false);
}

void NetworkWatchcatScreen::onInit() {
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Not connected", 1500);
    Screen.goBack();
    return;
  }
  _scroll.setResultStyle(true);  // Traffic Analyzer-like rows: white name
  _scroll.resetScroll();
  _view = VIEW_OVERALL;
  _gridSel = 0;
  _prevGridSel = -1;
  _holdCell = -1;
  for (int i = 0; i < 4; ++i) _prevCounts[i] = -1;
  _paused = false;
  _holdHandled = false;
  _snap.clear();
  _lastUpdate = millis();
  _ringHead = _ringTail = 0;
  _arpCount = 0;
  _hosts.clear(); _services.clear(); _arpChanges.clear(); _names.clear();
  _startRadio();
  render();
}

void NetworkWatchcatScreen::onRestore() { _startRadio(); _lastUpdate = millis(); render(); }

void NetworkWatchcatScreen::onBack() {
  _stopRadio();
  Screen.goBack();
}

void NetworkWatchcatScreen::_drawCell(int idx, int count) {
  static constexpr const char* kNames[] = {"New Hosts", "Services", "ARP Changes", "Names"};
#ifdef DEVICE_HAS_TOUCH_NAV
  const int backW = bodyW() / 6;
  const int gw    = bodyW() - backW;
  const int cellW = gw / 2;
  const int cellH = bodyH() / 2;
  const int px    = bodyX() + backW + (idx % 2) * cellW;
  const int py    = bodyY() + (idx / 2) * cellH;
  const bool held = (idx == _holdCell);
#else
  const int cellW = bodyW() / 2;
  const int cellH = bodyH() / 2;
  const int px    = bodyX() + (idx % 2) * cellW;
  const int py    = bodyY() + (idx / 2) * cellH;
  const bool held = (idx == (int)_gridSel);
#endif
  Sprite sp(&Uni.Lcd);
  sp.createSprite(cellW, cellH);
  sp.fillSprite(TFT_BLACK);
  sp.drawRoundRect(2, 2, cellW - 4, cellH - 4, 4, held ? Config.getThemeColor() : 0x2104);
  sp.setTextSize(1);
  sp.setTextDatum(TC_DATUM);
  sp.setTextColor(held ? TFT_WHITE : TFT_LIGHTGREY, TFT_BLACK);
  sp.drawString(kNames[idx], cellW / 2, 8);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", count);
  sp.setTextSize(2);
  sp.setTextDatum(MC_DATUM);
  sp.setTextColor(count > 0 ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
  sp.drawString(buf, cellW / 2, cellH / 2 + 4);
  sp.setTextSize(1);
  sp.pushSprite(px, py);
  sp.deleteSprite();
}

void NetworkWatchcatScreen::_drawBackButton() {
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

void NetworkWatchcatScreen::_renderOverall() {
  int counts[4] = {
    (int)_hosts.size(), (int)_services.size(),
    (int)_arpChanges.size(), (int)_names.size()
  };
  const bool force = _prevGridSel < 0;
  for (int i = 0; i < 4; ++i) {
    if (force || counts[i] != _prevCounts[i] || i == _gridSel || i == _prevGridSel)
      _drawCell(i, counts[i]);
    _prevCounts[i] = counts[i];
  }
  _prevGridSel = _gridSel;
}

const std::vector<NetworkWatchcatScreen::Hit>&
NetworkWatchcatScreen::_currentList() const {
  switch (_view) {
    case VIEW_SERVICES: return _services;
    case VIEW_ARP:      return _arpChanges;
    case VIEW_NAMES:    return _names;
    default:            return _hosts;   // VIEW_HOSTS
  }
}

void NetworkWatchcatScreen::_enter(View v) {
  _holdCell = -1;
  _view = v;
  _itemCount = 0;
  _paused = false;
  _snap.clear();
  _scroll.setSelectable(false);
  _scroll.resetScroll();
  static const char* titles[] = {"", "New Hosts", "Services", "ARP Changes", "Names"};
  _viewTitle = titles[v];
  _renderView();
}

// One row per entry. Rows copy their text into a stable buffer so the
// ScrollListView row pointers stay valid as the live lists change.
void NetworkWatchcatScreen::_renderView() {
  if (_view == VIEW_OVERALL) { _renderOverall(); return; }
  const std::vector<Hit>& items = _paused ? _snap : _currentList();
  int n = 0;
  for (const auto& h : items) {
    if (n >= MAX_LIST) break;
    _ipStr(h.ip, _labels[n], sizeof(_labels[n]));   // IP on the left
    _rows[n] = ScrollListView::Row(_labels[n], h.value);  // detail on the right
    ++n;
  }
  _setListState(n);
}

void NetworkWatchcatScreen::_setListState(int n) {
  _itemCount = n;
  _scroll.setRows(_rows, (uint8_t)_itemCount);
  render();
}

int NetworkWatchcatScreen::_listHeight() {
  return bodyH() - kFooterH;
}

// Hints follow the Wi-Fi Watchcat: Options on the upper line, Pause/Resume on
// the lower, below a separator at the top of a fixed-height footer.
void NetworkWatchcatScreen::_renderFooter() {
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
    lcd.drawString("[Press] Options", x + w / 2, y + h - 14);
    lcd.drawString(hint, x + w / 2, y + h - 1);
  } else {
    lcd.drawString("[Press] Pause", x + w / 2, y + h - 1);
  }
  lcd.setTextDatum(TL_DATUM);
}

void NetworkWatchcatScreen::_pause() {
  _paused = true;
  _snap = _currentList();
  _scroll.setSelectable(true);
  _scroll.setSelected(0);
  _renderView();
}

void NetworkWatchcatScreen::_resume() {
  _paused = false;
  _snap.clear();
  _scroll.setSelectable(false);
  _renderView();
}

void NetworkWatchcatScreen::_leaveList() {
  _paused = false;
  _snap.clear();
  _scroll.setSelectable(false);
  _view = VIEW_OVERALL;
  _prevGridSel = -1;
  Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  render();
}

// Build the host target from the selected (paused) row and open the shared
// Options menu; hand the radio over, onRestore() takes it back.
void NetworkWatchcatScreen::_openOptions() {
  const int index = _scroll.selected();
  if (index < 0 || index >= (int)_snap.size()) return;
  const uint32_t ip = _snap[index].ip;
  if (!ip || ip == _selfIp) { render(); return; }
  HostTarget t;
  t.ip = ip;  // no stored MAC here; the helper resolves it from the ARP cache
  if (!NetworkHostActions::openOptions(t, [this] { _stopRadio(); }))
    render();  // popup cancelled or guarded away: redraw the paused list
}

void NetworkWatchcatScreen::onUpdate() {
  if (_view == VIEW_OVERALL) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
#ifdef DEVICE_HAS_TOUCH_NAV
      const int16_t tx = Uni.Nav->lastTouchX();
      const int16_t ty = Uni.Nav->lastTouchY();
      const int backW = bodyW() / 6;
      if (dir == INavigation::DIR_BACK || (tx >= 0 && (int)tx < (int)bodyX() + backW)) {
        onBack(); return;
      }
      if (tx >= 0) {
        const int gx = (int)tx - (int)bodyX() - backW;
        const int gw = bodyW() - backW;
        const int col = gx / (gw / 2);
        const int row = ((int)ty - (int)bodyY()) / (bodyH() / 2);
        if (col >= 0 && col < 2 && row >= 0 && row < 2) {
          static const View map[] = {VIEW_HOSTS, VIEW_SERVICES, VIEW_ARP, VIEW_NAMES};
          _enter(map[row * 2 + col]);
          return;
        }
      }
#else
      if (dir == INavigation::DIR_UP || dir == INavigation::DIR_DOWN) _gridSel ^= 2;
      else if (dir == INavigation::DIR_LEFT || dir == INavigation::DIR_RIGHT) _gridSel ^= 1;
      else if (dir == INavigation::DIR_PRESS) {
        static const View map[] = {VIEW_HOSTS, VIEW_SERVICES, VIEW_ARP, VIEW_NAMES};
        _enter(map[_gridSel]);
        return;
      } else if (dir == INavigation::DIR_BACK) { onBack(); return; }
      _renderOverall();
#endif
    }
  } else {
    _scroll.tickMarquee();  // scroll an overlong value on the selected row
    // Like the Wi-Fi Watchcat. Live: [Press] pauses, [Back] returns to the
    // grid. Paused: arrows select, [Press] opens Options, [Back] resumes;
    // [Hold] opens Options too.
    if (_paused && Uni.Nav->isPressed() && Uni.Nav->currentDirection() == INavigation::DIR_PRESS &&
        Uni.Nav->heldDuration() >= 700 && !_holdHandled) {
      _holdHandled = true;
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

  _drain();

  if (millis() - _lastUpdate >= 1000) {
    _lastUpdate = millis();
    // A paused list keeps its snapshot; capture goes on in the background.
    if (!_paused) _renderView();
  }
}

void NetworkWatchcatScreen::onRender() {
  if (_view == VIEW_OVERALL) {
    Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    _prevGridSel = -1;
    _drawBackButton();
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
    Uni.Lcd.drawString(_paused ? "Paused - No results" : "Scanning...",
                       bodyX() + bodyW() / 2, bodyY() + listH / 2);
  }
  _renderFooter();
}
