#include "NetworkWatchdogScreen.h"
#include "core/ScreenManager.h"
#include "core/ConfigManager.h"
#include "ui/actions/ShowStatusAction.h"
#include <algorithm>
#include <cstring>

NetworkWatchdogScreen* NetworkWatchdogScreen::_active = nullptr;
NetworkWatchdogScreen::Event NetworkWatchdogScreen::_ring[MAX_RING];
volatile int NetworkWatchdogScreen::_ringHead = 0;
volatile int NetworkWatchdogScreen::_ringTail = 0;
portMUX_TYPE NetworkWatchdogScreen::_ringLock = portMUX_INITIALIZER_UNLOCKED;

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

void NetworkWatchdogScreen::_promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type) {
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
    if ((srcPort == 67 || dstPort == 68) && bodyLen > 240 &&
        body[236] == 0x63 && body[237] == 0x82 && body[238] == 0x53 && body[239] == 0x63) {
      uint8_t msg = 0;
      for (size_t i = 240; i + 1 < bodyLen; ) {
        const uint8_t opt = body[i];
        if (opt == 255) break;
        if (opt == 0) { ++i; continue; }
        const uint8_t olen = body[i + 1];
        if (i + 2 + olen > bodyLen) break;
        if (opt == 53 && olen >= 1) msg = body[i + 2];
        if (opt == 54 && olen >= 4) memcpy(&ev.ip, body + i + 2, 4);
        i += 2 + olen;
      }
      if (msg != 2 && msg != 5) return;
      if (!ev.ip) memcpy(&ev.ip, pl + 12, 4);
      ev.kind = 2;
    } else if (srcPort == 53 && bodyLen > 12 && (body[2] & 0x80)) {
      const uint16_t ancount = ((uint16_t)body[6] << 8) | body[7];
      if (!ancount) return;
      size_t p = 12;
      char name[24] = {};
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

const char* NetworkWatchdogScreen::_ipStr(uint32_t ip, char* out, size_t n) {
  snprintf(out, n, "%u.%u.%u.%u",
           (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
           (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
  return out;
}

void NetworkWatchdogScreen::_pushHit(std::vector<Hit>& list, const Hit& h) {
  for (auto& e : list) {
    if (e.ip == h.ip && strcmp(e.text, h.text) == 0) { e.seen = h.seen; return; }
  }
  if ((int)list.size() >= MAX_LIST) list.erase(list.begin());
  list.push_back(h);
}

void NetworkWatchdogScreen::_noteArp(uint32_t ip, const uint8_t* mac) {
  if (!ip || ip == _selfIp) return;
  for (uint8_t i = 0; i < _arpCount; ++i) {
    if (_arp[i].ip != ip) continue;
    if (memcmp(_arp[i].mac, mac, 6) == 0) return;
    Hit h;
    h.ip = ip; memcpy(h.mac, mac, 6); h.seen = millis();
    char a[20], b[20];
    snprintf(h.text, sizeof(h.text), "%s  %02X:%02X:%02X",
             _ipStr(ip, a, sizeof(a)), mac[0], mac[1], mac[2]);
    if (ip == _gwIp) _pushHit(_gwHits, h);
    else _pushHit(_arpHits, h);
    memcpy(_arp[i].mac, mac, 6);
    return;
  }
  if (_arpCount >= MAX_ARP) return;
  _arp[_arpCount].ip = ip;
  memcpy(_arp[_arpCount].mac, mac, 6);
  ++_arpCount;
  if (ip == _gwIp && !_gwMacKnown) {
    memcpy(_gwMac, mac, 6);
    _gwMacKnown = true;
  }
}

void NetworkWatchdogScreen::_noteDhcp(uint32_t serverIp) {
  if (!serverIp || serverIp == _gwIp || serverIp == _selfIp) return;
  Hit h; h.ip = serverIp; h.seen = millis();
  char a[20];
  snprintf(h.text, sizeof(h.text), "Offer from %s", _ipStr(serverIp, a, sizeof(a)));
  _pushHit(_dhcpHits, h);
}

void NetworkWatchdogScreen::_noteDns(const char* name, uint32_t ip) {
  if (!name || !name[0] || !ip) return;
  for (const auto& e : _dnsBase) {
    if (strncmp(e.text, name, 16) == 0) {
      if (e.ip != ip) {
        Hit h; h.ip = ip; h.seen = millis();
        char a[20];
        snprintf(h.text, sizeof(h.text), "%s -> %s", name, _ipStr(ip, a, sizeof(a)));
        _pushHit(_dnsHits, h);
      }
      return;
    }
  }
  Hit base; base.ip = ip; base.seen = millis();
  snprintf(base.text, sizeof(base.text), "%s", name);
  if ((int)_dnsBase.size() >= MAX_LIST) _dnsBase.erase(_dnsBase.begin());
  _dnsBase.push_back(base);
}

void NetworkWatchdogScreen::_drain() {
  for (;;) {
    Event ev;
    portENTER_CRITICAL(&_ringLock);
    if (_ringTail == _ringHead) { portEXIT_CRITICAL(&_ringLock); break; }
    ev = _ring[_ringTail];
    _ringTail = (_ringTail + 1) % MAX_RING;
    portEXIT_CRITICAL(&_ringLock);
    if (ev.kind == 1) _noteArp(ev.ip, ev.mac);
    else if (ev.kind == 2) _noteDhcp(ev.ip);
    else if (ev.kind == 3) _noteDns(ev.name, ev.ip);
  }
  const unsigned long now = millis();
  auto prune = [&](std::vector<Hit>& list) {
    list.erase(std::remove_if(list.begin(), list.end(),
      [&](const Hit& h) { return now - h.seen > WINDOW_MS; }), list.end());
  };
  prune(_arpHits); prune(_dhcpHits); prune(_gwHits); prune(_dnsHits);
}

void NetworkWatchdogScreen::_startRadio() {
  _active = this;
  _selfIp = (uint32_t)WiFi.localIP();
  _gwIp = (uint32_t)WiFi.gatewayIP();
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(&_promiscuousCb);
  esp_wifi_set_promiscuous(true);
}

void NetworkWatchdogScreen::_stopRadio() {
  if (_active == this) _active = nullptr;
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  esp_wifi_set_promiscuous(false);
}

void NetworkWatchdogScreen::onInit() {
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Not connected", 1500);
    Screen.goBack();
    return;
  }
  _view = VIEW_OVERALL;
  _gridSel = 0;
  _prevGridSel = -1;
  for (int i = 0; i < 4; ++i) _prevCounts[i] = -1;
  _ringHead = _ringTail = 0;
  _arpCount = 0;
  _arpHits.clear(); _dhcpHits.clear(); _gwHits.clear(); _dnsHits.clear(); _dnsBase.clear();
  _startRadio();
  render();
}

void NetworkWatchdogScreen::onRestore() { _startRadio(); render(); }

void NetworkWatchdogScreen::onBack() {
  if (_view != VIEW_OVERALL) {
    _view = VIEW_OVERALL;
    _prevGridSel = -1;
    render();
    return;
  }
  _stopRadio();
  Screen.goBack();
}

void NetworkWatchdogScreen::_drawCell(int idx, int count) {
  static constexpr const char* kNames[] = {"ARP Poison", "DHCP Rogue", "GW Hijack", "DNS Spoof"};
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
  sp.setTextColor(count > 0 ? TFT_RED : TFT_DARKGREY, TFT_BLACK);
  sp.drawString(buf, cellW / 2, cellH / 2 + 4);
  sp.setTextSize(1);
  sp.pushSprite(px, py);
  sp.deleteSprite();
}

void NetworkWatchdogScreen::_drawBackButton() {
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

void NetworkWatchdogScreen::_renderOverall() {
  int counts[4] = {
    (int)_arpHits.size(), (int)_dhcpHits.size(),
    (int)_gwHits.size(), (int)_dnsHits.size()
  };
  const bool force = _prevGridSel < 0;
  for (int i = 0; i < 4; ++i) {
    if (force || counts[i] != _prevCounts[i] || i == _gridSel || i == _prevGridSel)
      _drawCell(i, counts[i]);
    _prevCounts[i] = counts[i];
  }
  _prevGridSel = _gridSel;
}

void NetworkWatchdogScreen::_renderList(const std::vector<Hit>& list) {
  const int n = std::min((int)list.size(), MAX_LIST);
  for (int i = 0; i < n; ++i) {
    _rowLabels[i] = list[i].text;
    _rows[i] = {_rowLabels[i].c_str(), ""};
  }
  _scroll.setRows(_rows, n);
  _scroll.render(bodyX(), bodyY(), bodyW(), bodyH());
  if (n == 0) {
    Uni.Lcd.setTextDatum(MC_DATUM);
    Uni.Lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    Uni.Lcd.drawString("Monitoring...", bodyX() + bodyW() / 2, bodyY() + bodyH() / 2);
  }
}

void NetworkWatchdogScreen::_enter(View v) {
  _view = v;
  _scroll.resetScroll();
  static const char* titles[] = {"", "ARP Poison", "DHCP Rogue", "GW Hijack", "DNS Spoof"};
  _viewTitle = titles[v];
  render();
}

void NetworkWatchdogScreen::onUpdate() {
  _drain();
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
          static const View map[] = {VIEW_ARP, VIEW_DHCP, VIEW_GW, VIEW_DNS};
          _enter(map[row * 2 + col]);
          return;
        }
      }
#else
      if (dir == INavigation::DIR_UP || dir == INavigation::DIR_DOWN) _gridSel ^= 2;
      else if (dir == INavigation::DIR_LEFT || dir == INavigation::DIR_RIGHT) _gridSel ^= 1;
      else if (dir == INavigation::DIR_PRESS) {
        static const View map[] = {VIEW_ARP, VIEW_DHCP, VIEW_GW, VIEW_DNS};
        _enter(map[_gridSel]);
        return;
      } else if (dir == INavigation::DIR_BACK) { onBack(); return; }
      _renderOverall();
#endif
    } else {
      static unsigned long last = 0;
      if (millis() - last > 800) { last = millis(); _renderOverall(); }
    }
    return;
  }
  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) { onBack(); return; }
    _scroll.onNav(dir);
    onRender();
  }
}

void NetworkWatchdogScreen::onRender() {
  if (_view == VIEW_OVERALL) {
    Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    _prevGridSel = -1;
    _drawBackButton();
    _renderOverall();
    return;
  }
  Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  if (_view == VIEW_ARP) _renderList(_arpHits);
  else if (_view == VIEW_DHCP) _renderList(_dhcpHits);
  else if (_view == VIEW_GW) _renderList(_gwHits);
  else _renderList(_dnsHits);
}
