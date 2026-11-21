#include "NetworkHostMonitorScreen.h"

#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/ConfigManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "screens/wifi/network/NetworkHostActions.h"

#include <WiFi.h>
#include <lwip/etharp.h>
#include <lwip/icmp.h>
#include <lwip/raw.h>
#include <lwip/ip_addr.h>
#include <lwip/inet_chksum.h>
#include <lwip/netif.h>
#include <algorithm>

namespace {
volatile bool gPingHit = false;

uint8_t onPing(void*, struct raw_pcb*, struct pbuf* p, const ip_addr_t*) {
  gPingHit = true;
  pbuf_free(p);
  return 1;
}

void icmpOnce(uint32_t ipBe, uint32_t timeoutMs) {
  struct raw_pcb* pcb = raw_new(IP_PROTO_ICMP);
  if (!pcb) return;
  gPingHit = false;
  raw_recv(pcb, onPing, nullptr);
  struct pbuf* p = pbuf_alloc(PBUF_IP, sizeof(struct icmp_echo_hdr), PBUF_RAM);
  if (!p) { raw_remove(pcb); return; }
  auto* iecho = (struct icmp_echo_hdr*)p->payload;
  ICMPH_TYPE_SET(iecho, ICMP_ECHO);
  ICMPH_CODE_SET(iecho, 0);
  iecho->chksum = 0;
  iecho->id = htons(0x5547);
  iecho->seqno = htons(1);
  iecho->chksum = inet_chksum(iecho, sizeof(struct icmp_echo_hdr));
  ip_addr_t dest;
  IP_ADDR4(&dest, ipBe & 0xFF, (ipBe >> 8) & 0xFF, (ipBe >> 16) & 0xFF, (ipBe >> 24) & 0xFF);
  raw_sendto(pcb, p, &dest);
  pbuf_free(p);
  const uint32_t start = millis();
  while (!gPingHit && millis() - start < timeoutMs) delay(1);
  raw_remove(pcb);
}

void fmtIp(uint32_t ip, char* out, size_t n) {
  snprintf(out, n, "%u.%u.%u.%u",
           (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
           (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
}

const char* ageText(uint32_t lastSeen, uint32_t now) {
  static char buf[8];
  const uint32_t s = (now - lastSeen) / 1000U;
  if (s < 60) snprintf(buf, sizeof(buf), "%lus", (unsigned long)s);
  else snprintf(buf, sizeof(buf), "%lum", (unsigned long)(s / 60U));
  return buf;
}

// Match the scrollbar used by ScrollListView and Traffic Analyzer (3px track and theme thumb).
void drawMonitorScrollBar(int x, int y, int height, int total, int visible, int offset) {
  if (height <= 0) return;
  auto& lcd = Uni.Lcd;
  if (total <= visible || visible <= 0) {
    lcd.fillRect(x, y, 3, height, TFT_BLACK);
    return;
  }
  lcd.fillRect(x, y, 3, height, 0x2104);
  const int thumb = std::min(height, std::max(4, height * visible / total));
  const int top = y + (height - thumb) * offset / (total - visible);
  lcd.fillRect(x, top, 3, thumb, Config.getThemeColor());
}
}

void NetworkHostMonitorScreen::onInit() {
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Not connected", 1500);
    Screen.goBack();
    return;
  }
  _state = STATE_RUNNING;
  _count = 0;
  _selected = 0;
  _scroll = 0;
  _probeOctet = 1;
  _holdHandled = false;
  _gateway = (uint32_t)WiFi.gatewayIP();
  render();
}

void NetworkHostMonitorScreen::onRestore() {
  if (WiFi.getMode() != WIFI_STA) WiFi.mode(WIFI_STA);
  _drawList();
}

void NetworkHostMonitorScreen::onBack() {
  if (_state == STATE_PAUSED) {
    _state = STATE_RUNNING;
    _dirty = true;
    _drawList();
    return;
  }
  Screen.goBack();
}

void NetworkHostMonitorScreen::onUpdate() {
  if (_state == STATE_PAUSED && Uni.Nav && Uni.Nav->isPressed() &&
      Uni.Nav->currentDirection() == INavigation::DIR_PRESS &&
      Uni.Nav->heldDuration() >= 700 && !_holdHandled && _count) {
    _holdHandled = true;
    Uni.Nav->suppressCurrentPress();
    _openOptions();
    return;
  }
  if (_holdHandled && Uni.Nav && !Uni.Nav->isPressed()) _holdHandled = false;

  if (Uni.Nav && Uni.Nav->wasPressed()) {
    const auto dir = Uni.Nav->readDirection();
    const uint32_t duration = Uni.Nav->pressDuration();
    if (dir == INavigation::DIR_BACK) { onBack(); return; }

    const int footerH = Uni.Lcd.fontHeight() + 6;
    constexpr int rowH = 19;
    const int visible = std::max(1, (bodyH() - 2 - footerH) / rowH);

    if (_state == STATE_RUNNING) {
      if (dir == INavigation::DIR_PRESS && duration < 700) {
        _state = STATE_PAUSED;
        _selected = _scroll;
        if (_selected >= _count) _selected = _count ? _count - 1 : 0;
        _dirty = true;
        _drawList();
        return;
      }
      if ((dir == INavigation::DIR_DOWN || dir == INavigation::DIR_RIGHT) && _count > visible) {
        if (_scroll + visible < _count) { ++_scroll; _drawList(); }
        return;
      }
      if ((dir == INavigation::DIR_UP || dir == INavigation::DIR_LEFT) && _scroll > 0) {
        --_scroll;
        _drawList();
        return;
      }
    } else {
      if (dir == INavigation::DIR_UP || dir == INavigation::DIR_LEFT) {
        if (_count) {
          _selected = _selected == 0 ? _count - 1 : _selected - 1;
          _drawList();
        }
        return;
      }
      if (dir == INavigation::DIR_DOWN || dir == INavigation::DIR_RIGHT) {
        if (_count) {
          _selected = (_selected + 1) % _count;
          _drawList();
        }
        return;
      }
      if (dir == INavigation::DIR_PRESS && duration < 700 && _count) {
        _openOptions();
        return;
      }
    }
  }

  if (_state != STATE_RUNNING) return;
  if (millis() - _lastProbeMs >= 40) {
    _lastProbeMs = millis();
    _probeNext();
  }
  if (_dirty || millis() - _lastDrawMs >= 1000) _drawList();
}

void NetworkHostMonitorScreen::_probeNext() {
  IPAddress local = WiFi.localIP();
  if (local == IPAddress(0, 0, 0, 0)) return;
  const uint32_t base = (uint32_t)local & 0x00FFFFFF;
  icmpOnce(base | ((uint32_t)_probeOctet << 24), 15);

  for (int i = 0; i < ARP_TABLE_SIZE; ++i) {
    ip4_addr_t* aip = nullptr;
    struct netif* netif = nullptr;
    struct eth_addr* mac = nullptr;
    if (!etharp_get_entry(i, &aip, &netif, &mac) || !aip || !mac) continue;
    _note(aip->addr, mac->addr);
  }
  _probeOctet = _probeOctet >= 254 ? 1 : _probeOctet + 1;
}

void NetworkHostMonitorScreen::_note(uint32_t ip, const uint8_t* mac) {
  if (!ip || !mac) return;
  const uint32_t now = millis();
  for (uint8_t i = 0; i < _count; ++i) {
    if (_hosts[i].ip == ip) {
      if (!_hosts[i].hasMac || memcmp(_hosts[i].mac, mac, 6) != 0) _dirty = true;
      memcpy(_hosts[i].mac, mac, 6);
      _hosts[i].hasMac = true;
      _hosts[i].lastSeen = now;
      return;
    }
  }
  if (_count >= MAX_HOSTS) return;
  Host& h = _hosts[_count++];
  h.ip = ip;
  memcpy(h.mac, mac, 6);
  h.hasMac = true;
  h.gateway = ip == _gateway;
  h.firstSeen = h.lastSeen = now;
  _dirty = true;
  _sort();
}

void NetworkHostMonitorScreen::_sort() {
  for (uint8_t i = 1; i < _count; ++i) {
    Host key = _hosts[i];
    int j = (int)i - 1;
    while (j >= 0 && _hosts[j].ip > key.ip) { _hosts[j + 1] = _hosts[j]; --j; }
    _hosts[j + 1] = key;
  }
}

void NetworkHostMonitorScreen::onRender() {
  _drawList();
}

void NetworkHostMonitorScreen::_drawList() {
  auto& lcd = Uni.Lcd;
  const int x = bodyX(), y = bodyY(), w = bodyW(), h = bodyH();
  lcd.fillRect(x, y, w, h, TFT_BLACK);
  lcd.setTextSize(1);
  lcd.setTextDatum(TL_DATUM);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);

  const int top = y + 2, footerH = lcd.fontHeight() + 6, rowH = 19;
  const int visible = (h - 2 - footerH) / rowH;
  const bool paused = (_state == STATE_PAUSED);

  if (!_count) {
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.drawString(paused ? "Paused - No results" : "Scanning...",
                   x + w / 2, y + (h - footerH) / 2);
    lcd.setTextDatum(TL_DATUM);
  }

  if (visible > 0) {
    if (paused) {
      if (_selected < _scroll) _scroll = _selected;
      if (_selected >= _scroll + visible) _scroll = _selected - visible + 1;
    }
    const uint8_t maxScroll = _count > (uint8_t)visible ? _count - visible : 0;
    if (_scroll > maxScroll) _scroll = maxScroll;

    const bool hasScrollbar = _count > visible;
    const int rightEdge = x + w - (hasScrollbar ? 7 : 3);
    const uint32_t now = millis();

    for (int i = 0; i < visible && i + _scroll < _count; ++i) {
      const uint8_t idx = i + _scroll;
      const Host& host = _hosts[idx];
      const int yy = top + i * rowH;
      lcd.fillRect(x, yy - 2, w - 3, rowH, TFT_BLACK);

      const bool selected = paused && idx == _selected;
      const uint16_t bg = selected ? Config.getThemeColor() : TFT_BLACK;
      if (selected) lcd.fillRoundRect(x, yy - 2, rightEdge - x, rowH, 3, bg);

      char ip[16], right[8];
      fmtIp(host.ip, ip, sizeof(ip));
      snprintf(right, sizeof(right), "%s", host.gateway ? "gw" : ageText(host.lastSeen, now));

      lcd.setTextColor(TFT_WHITE, bg);
      lcd.drawString(ip, x + 3, yy);
      lcd.setTextDatum(TR_DATUM);
      lcd.setTextColor(selected ? TFT_WHITE : TFT_DARKGREY, bg);
      lcd.drawString(right, rightEdge - 3, yy);
      lcd.setTextDatum(TL_DATUM);
    }

    const int drawn = std::min((int)_count - (int)_scroll, visible);
    if (drawn < visible) {
      lcd.fillRect(x, top + std::max(0, drawn) * rowH, w - 3,
                   (visible - std::max(0, drawn)) * rowH, TFT_BLACK);
    }
    drawMonitorScrollBar(x + w - 3, top, visible * rowH, _count, visible, _scroll);
  }

  _renderHints(x, y, w, h, paused);
  lcd.setTextDatum(TL_DATUM);
  _dirty = false;
  _lastDrawMs = millis();
}

void NetworkHostMonitorScreen::_renderHints(int x, int y, int w, int h, bool paused) {
  auto& lcd = Uni.Lcd;
  const int footerH = lcd.fontHeight() + 6;
  const int footerTop = y + h - footerH;
  lcd.fillRect(x, footerTop, w, footerH, TFT_BLACK);
  lcd.drawFastHLine(x, footerTop, w, TFT_DARKGREY);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.setTextDatum(BC_DATUM);
  if (paused) {
    const char* hint = "[Press] Options  [Back] Resume";
    if (lcd.textWidth(hint) > w - 4) hint = "[Press] Options [Back] Resume";
    lcd.drawString(hint, x + w / 2, y + h - 2);
  } else {
    lcd.drawString("[Press] Pause", x + w / 2, y + h - 2);
  }
  lcd.setTextDatum(TL_DATUM);
}

void NetworkHostMonitorScreen::_openOptions() {
  if (_selected >= _count) return;
  const Host& h = _hosts[_selected];

  HostTarget t;
  t.ip     = h.ip;
  t.hasMac = h.hasMac;
  if (h.hasMac) memcpy(t.mac, h.mac, 6);
  // Monitor Hosts runs an active (STA) discovery, so there is no capture to
  // pause before launching a child action.
  if (!NetworkHostActions::openOptions(t)) _drawList();
}
