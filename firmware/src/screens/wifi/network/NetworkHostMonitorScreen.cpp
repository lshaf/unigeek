#include "NetworkHostMonitorScreen.h"

#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/ConfigManager.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "screens/wifi/network/IPScannerDetailsScreen.h"
#include "screens/wifi/network/PortScannerScreen.h"
#include "screens/wifi/WifiFoxHuntScreen.h"

#include <WiFi.h>
#include <lwip/etharp.h>
#include <lwip/icmp.h>
#include <lwip/raw.h>
#include <lwip/ip_addr.h>
#include <lwip/inet_chksum.h>
#include <lwip/netif.h>

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
}

void NetworkHostMonitorScreen::onInit() {
  _state = STATE_RUNNING;
  _count = 0;
  _selected = 0;
  _probeOctet = 1;
  _gateway = (uint32_t)WiFi.gatewayIP();
  render();
}

void NetworkHostMonitorScreen::onItemSelected(uint8_t) {}

void NetworkHostMonitorScreen::onBack() {
  if (_state == STATE_PAUSED) { _state = STATE_RUNNING; _dirty = true; _drawList(); return; }
  Screen.goBack();
}

void NetworkHostMonitorScreen::onUpdate() {
  if (Uni.Nav && Uni.Nav->wasPressed()) {
    const auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) { onBack(); return; }
    if (_state == STATE_RUNNING && dir == INavigation::DIR_PRESS) {
      _state = STATE_PAUSED;
      if (_selected >= _count) _selected = _count ? _count - 1 : 0;
      _dirty = true;
      _drawList();
      return;
    }
    if (_state == STATE_PAUSED) {
      if (dir == INavigation::DIR_UP && _count) {
        _selected = _selected == 0 ? _count - 1 : _selected - 1;
        _drawList();
        return;
      }
      if (dir == INavigation::DIR_DOWN && _count) {
        _selected = (_selected + 1) % _count;
        _drawList();
        return;
      }
      if (dir == INavigation::DIR_PRESS && _count) { _openOptions(); return; }
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
  const int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextSize(1);
  const int fh = lcd.fontHeight();
  const int lineH = fh + 3;
  const int footerH = fh + 6;
  const int slots = (bh - footerH) / lineH;
  const int listH = slots > 0 ? slots * lineH : 0;

  int first = 0;
  if (_state == STATE_PAUSED && _count > slots)
    first = (int)_selected - slots / 2;
  if (first < 0) first = 0;
  if (_count > slots && first > (int)_count - slots) first = _count - slots;

  for (int row = 0; row < slots; ++row) {
    const int idx = first + row;
    if (idx >= _count) break;
    const Host& h = _hosts[idx];
    const bool selected = _state == STATE_PAUSED && idx == (int)_selected;
    const int y = by + row * lineH;
    const uint16_t bg = selected ? Config.getThemeColor() : TFT_BLACK;
    if (selected) lcd.fillRect(bx, y, bw, lineH, bg);

    char ip[16], right[8];
    fmtIp(h.ip, ip, sizeof(ip));
    snprintf(right, sizeof(right), "%s", h.gateway ? "gw" : ageText(h.lastSeen, millis()));

    lcd.setTextColor(TFT_WHITE, bg);
    lcd.setTextDatum(TL_DATUM);
    lcd.drawString(ip, bx + 2, y + 1);
    lcd.setTextDatum(TR_DATUM);
    lcd.setTextColor(selected || h.gateway ? TFT_WHITE : TFT_LIGHTGREY, bg);
    lcd.drawString(right, bx + bw - 2, y + 1);
  }

  if (!_count) {
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.drawString("Scanning...", bx + bw / 2, by + listH / 2);
  }

  const int footerTop = by + bh - footerH;
  lcd.drawFastHLine(bx, footerTop, bw, TFT_DARKGREY);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  if (_state == STATE_RUNNING) {
    lcd.setTextDatum(TC_DATUM);
    lcd.drawString("[Press] Pause", bx + bw / 2, footerTop + 3);
  } else {
    lcd.setTextDatum(TL_DATUM);
    lcd.drawString("[Press] Options", bx + 2, footerTop + 3);
    lcd.setTextDatum(TR_DATUM);
    lcd.drawString("[Back] Resume", bx + bw - 2, footerTop + 3);
  }
  _dirty = false;
  _lastDrawMs = millis();
}

void NetworkHostMonitorScreen::_openOptions() {
  if (_selected >= _count) return;
  const Host& h = _hosts[_selected];
  char ip[16];
  fmtIp(h.ip, ip, sizeof(ip));

  static const InputSelectAction::Option opts[] = {
    {"Details", "details"},
    {"Port Scan", "ports"},
    {"Fox Hunt", "fox"},
  };
  const char* choice = InputSelectAction::popup("Options", opts, 3);
  if (!choice) { _drawList(); return; }

  if (strcmp(choice, "details") == 0) {
    auto* details = new IPScannerDetailsScreen();
    details->configure(ip, nullptr, h.hasMac ? h.mac : nullptr);
    Screen.push(details);
    return;
  }
  if (strcmp(choice, "ports") == 0) {
    auto* scanner = new PortScannerScreen();
    scanner->configureTarget(ip);
    Screen.push(scanner);
    return;
  }
  if (strcmp(choice, "fox") == 0) {
    if (!h.hasMac) { ShowStatusAction::show("MAC unavailable"); return; }
    const uint8_t channel = WiFi.channel();
    if (channel < 1 || channel > 14) { ShowStatusAction::show("Channel unavailable"); return; }
    auto* hunt = new WifiFoxHuntScreen();
    const uint8_t* ap = WiFi.BSSID();
    const bool validAp = ap && memcmp(ap, "\0\0\0\0\0\0", 6) != 0;
    if (validAp && (h.gateway || memcmp(h.mac, ap, 6) == 0))
      hunt->configureTarget(false, ap, channel, ip);
    else
      hunt->configureTransmitterTarget(h.mac, channel, ip);
    Screen.push(hunt);
  }
}

