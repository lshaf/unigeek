#include "WifiChannelMonitorScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/ConfigManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/WifiMenuScreen.h"
#include "screens/wifi/WifiPacketMonitorScreen.h"
#include "screens/wifi/WifiPacketSnifferScreen.h"
#include "screens/wifi/WifiFoxHuntScreen.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowStatusAction.h"

static std::atomic<uint32_t> _pktCounter{0};

// Match the scrollbar used by ScrollListView (3px track and theme thumb).
static void drawMonitorScrollBar(int x, int y, int height, int total, int visible, int offset) {
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


WifiChannelMonitorScreen* WifiChannelMonitorScreen::_active = nullptr;

void WifiChannelMonitorScreen::_captureCb(void* buf, wifi_promiscuous_pkt_type_t type)
{
  if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;
  if (!buf) return;
  auto* self = _active;
  if (!self || self->_view == PAUSED || self->_view == AP_PAUSED ||
      self->_view == CLIENT_PAUSED) return;
  const auto* pkt = static_cast<const wifi_promiscuous_pkt_t*>(buf);
  // The radio may deliver a frame from the previous channel just after a hop.
  // Do not attribute such a frame to the currently selected AP/channel.
  const uint8_t receivedChannel = pkt->rx_ctrl.channel;
  if (self->_view == DETAILS || self->_view == AP_PAUSED ||
      self->_view == CLIENTS || self->_view == CLIENT_PAUSED) {
    if (receivedChannel != self->_detailChannel) return;
  } else if (receivedChannel != self->_sweepCh) {
    return;
  }
  const bool channelView = self->_view == LIVE || self->_view == GRAPH;
  const uint8_t* f = pkt->payload;
  const size_t len = pkt->rx_ctrl.sig_len;
  if (len < 24 || (f[0] & 3) != 0) return;
  const uint8_t* bssid = nullptr;
  const uint8_t subtype = (f[0] >> 4) & 15;
  if (type == WIFI_PKT_MGMT) bssid = f + 16;
  else {
    const bool toDS = (f[1] & 1) != 0, fromDS = (f[1] & 2) != 0;
    if (toDS && fromDS) return;
    bssid = toDS ? f + 4 : fromDS ? f + 10 : f + 16;
  }
  if (bssid[0] & 1) return;
  char name[33] = {};
  // Beacon/probe-response fixed parameters occupy bytes 24..35.  The DS
  // Parameter Set (IE 3) advertises the AP's primary channel; HT Operation
  // (IE 61) provides the primary channel when DS is absent.
  const bool apAnnouncement = type == WIFI_PKT_MGMT &&
                              (subtype == 8 || subtype == 5) && len >= 36;
  uint8_t announcedChannel = 0;
  if (apAnnouncement) {
    size_t pos = 36;
    while (pos + 2 <= len) {
      const uint8_t id = f[pos], n = f[pos + 1];
      pos += 2;
      if (n > len - pos) break;
      if (id == 0 && n > 0 && n <= 32) {
        for (uint8_t i = 0; i < n; ++i)
          name[i] = (f[pos + i] >= 32 && f[pos + i] < 127) ? (char)f[pos + i] : '_';
      } else if (id == 3 && n == 1) {
        announcedChannel = f[pos];
      } else if (id == 61 && n >= 1 && announcedChannel == 0) {
        announcedChannel = f[pos];
      }
      pos += n;
    }
    // A frame received on an overlapping adjacent channel is not evidence
    // that the AP actually operates on the channel currently being scanned.
    if (announcedChannel != 0 && announcedChannel != receivedChannel) return;
  }
  if (channelView) {
    // Only traffic belonging to an AP whose primary channel has been
    // established by a beacon/probe response contributes to the graph.
    // This excludes adjacent-channel frames and unidentified stations.
    bool confirmed = false;
    portENTER_CRITICAL(&self->_apMux);
    int known = -1;
    for (uint8_t i = 0; i < self->_knownApCount; ++i) {
      if (!memcmp(self->_knownAps[i].bssid, bssid, 6)) {
        known = i;
        break;
      }
    }
    if (apAnnouncement && announcedChannel == receivedChannel) {
      if (known < 0 && self->_knownApCount < MAX_KNOWN_APS) {
        known = self->_knownApCount++;
        memcpy(self->_knownAps[known].bssid, bssid, 6);
      }
      if (known >= 0) self->_knownAps[known].channel = receivedChannel;
    }
    if (known >= 0 && self->_knownAps[known].channel == receivedChannel)
      confirmed = true;
    portEXIT_CRITICAL(&self->_apMux);
    if (confirmed) _pktCounter.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  _pktCounter.fetch_add(1, std::memory_order_relaxed);
  // In client view, only count frames with an unambiguous AP/client pair.
  if (self->_view == CLIENTS || self->_view == CLIENT_PAUSED) {
    const uint8_t* client = nullptr;
    if (type == WIFI_PKT_DATA) {
      const bool toDS = (f[1] & 1) != 0, fromDS = (f[1] & 2) != 0;
      if (toDS && !fromDS) client = f + 10;       // transmitter station
      else if (!toDS && fromDS) client = f + 4;  // receiver station
      // Non-DS and WDS frames are not reliably attributable to a client.
    }
    if (!client || (client[0] & 1) || memcmp(bssid, self->_selectedBssid, 6)) return;
    portENTER_CRITICAL(&self->_apMux);
    int ci = -1;
    for (uint8_t i = 0; i < self->_clientCount; ++i)
      if (!memcmp(self->_clients[i].mac, client, 6)) { ci = i; break; }
    if (ci < 0 && self->_clientCount < MAX_CLIENTS) {
      ci = self->_clientCount++;
      memcpy(self->_clients[ci].mac, client, 6);
    }
    if (ci >= 0) { ++self->_clients[ci].packets; self->_clients[ci].rssi = pkt->rx_ctrl.rssi; }
    portEXIT_CRITICAL(&self->_apMux);
    return;
  }
  portENTER_CRITICAL(&self->_apMux);
  int index = -1;
  for (uint8_t i = 0; i < self->_apCount; ++i)
    if (memcmp(self->_aps[i].bssid, bssid, 6) == 0) { index = i; break; }
  // Only an AP announcement with a verified primary channel may create a
  // new entry. Data/control traffic alone cannot establish an AP's channel.
  if (index < 0 && (!apAnnouncement || announcedChannel == 0)) {
    portEXIT_CRITICAL(&self->_apMux);
    return;
  }
  if (index < 0 && self->_apCount < MAX_APS) {
    index = self->_apCount++;
    memcpy(self->_aps[index].bssid, bssid, 6);
  }
  if (index >= 0) {
    auto& ap = self->_aps[index];
    ++ap.packets;
    ap.rssi = pkt->rx_ctrl.rssi;
    if (name[0]) memcpy(ap.ssid, name, sizeof(name));
  }
  portEXIT_CRITICAL(&self->_apMux);
}

void WifiChannelMonitorScreen::onInit()
{
  memset(_chanAccum, 0, sizeof(_chanAccum));
  memset(_displayCounts, 0, sizeof(_displayCounts));
  memset(_activity, 0, sizeof(_activity));
  memset(_channelTotals, 0, sizeof(_channelTotals));
  for (uint8_t i = 0; i < _NUM_CH; ++i) _rank[i] = i + 1;
  _scroll = 0;
  _selected = 0;
  _view = GRAPH;
  _graphChromeDrawn = false;
  _graphAnimating = false;
  memset(_graphLastH, 0, sizeof(_graphLastH));
  _apCount = 0;
  _knownApCount = 0;
  memset(_knownAps, 0, sizeof(_knownAps));
  _detailScroll = 0;
  _clientCount = 0;
  _sweepCh = 1;
  _firstSweep = true;
  _quitting = false;
  _holdHandled = false;
  _pktCounter.store(0, std::memory_order_relaxed);
  _lastPaintedView = 255;

  int n = Achievement.inc("wifi_packet_monitor_first");
  if (n == 1) Achievement.unlock("wifi_packet_monitor_first");

  WiFi.mode(WIFI_MODE_STA);
  _active = this;
  esp_wifi_set_promiscuous_rx_cb(&_captureCb);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(_sweepCh, WIFI_SECOND_CHAN_NONE);
  _lastHop = millis();
  render(); // Keep the scanning overlay visible until the first sweep completes.
}

void WifiChannelMonitorScreen::onUpdate()
{
  if (_quitting) return;
  // Trigger while held, rather than waiting for the release event.
  if (Uni.Nav->isPressed() && Uni.Nav->currentDirection() == INavigation::DIR_PRESS &&
      Uni.Nav->heldDuration() >= 700 && !_holdHandled &&
      (_view == PAUSED || _view == AP_PAUSED || _view == CLIENT_PAUSED)) {
    _holdHandled = true;
    // Match PN532 Scan Tag: fire at the hold threshold and suppress the
    // release so the synchronous popup cannot interpret it as a selection.
    Uni.Nav->suppressCurrentPress();
    _openOptions();
    return;
  }
  if (_holdHandled && !Uni.Nav->isPressed()) _holdHandled = false;
  if (Uni.Nav->wasPressed()) {
    const auto dir = Uni.Nav->readDirection();
    const uint32_t duration = Uni.Nav->pressDuration();
    // The classic graph is the entry view; Press opens the channel list.
    if (_view == GRAPH) {
      if (dir == INavigation::DIR_BACK) _quit();
      else if (dir == INavigation::DIR_PRESS && duration < 700) {
        _view = LIVE;
        _scroll = 0;
        render();
      }
      return;
    }
    if (_view == CLIENTS || _view == CLIENT_PAUSED) {
      if (dir == INavigation::DIR_BACK) {
        if (_view == CLIENT_PAUSED) {
          _view = CLIENTS;
          esp_wifi_set_promiscuous(true);
        } else {
          _view = AP_PAUSED;
          esp_wifi_set_promiscuous(false);
        }
      } else if (dir == INavigation::DIR_PRESS) {
        if (_view == CLIENTS) {
          esp_wifi_set_promiscuous(false);
          _view = CLIENT_PAUSED;
          _clientSelected = _clientScroll;
          ClientEntry snapshot[MAX_CLIENTS];
          portENTER_CRITICAL(&_apMux);
          _frozenClientCount = _clientCount;
          memcpy(snapshot, _clients, _frozenClientCount * sizeof(ClientEntry));
          portEXIT_CRITICAL(&_apMux);
          memcpy(_frozenClients, snapshot, _frozenClientCount * sizeof(ClientEntry));
          for (uint8_t i = 0; i < _frozenClientCount; ++i) _clientOrder[i] = i;
          for (uint8_t i = 1; i < _frozenClientCount; ++i) {
            const uint8_t item = _clientOrder[i];
            int j = i;
            while (j > 0 && snapshot[_clientOrder[j-1]].packets < snapshot[item].packets) {
              _clientOrder[j] = _clientOrder[j-1]; --j;
            }
            _clientOrder[j] = item;
          }
        } else {
          // Short press intentionally has no action; Hold opens Options.
        }
      } else if (dir == INavigation::DIR_DOWN || dir == INavigation::DIR_RIGHT) {
        if (_view == CLIENT_PAUSED) {
          if (_frozenClientCount) _clientSelected = (_clientSelected + 1) % _frozenClientCount;
        } else {
          const int visible = std::max(1, (bodyH() - 2 - 27) / 19);
          const int maxScroll = std::max(0, (int)_clientCount - visible);
          if (_clientScroll < maxScroll) ++_clientScroll;
        }
      } else if (dir == INavigation::DIR_UP || dir == INavigation::DIR_LEFT) {
        if (_view == CLIENT_PAUSED) {
          if (_frozenClientCount) _clientSelected = (_clientSelected + _frozenClientCount - 1) % _frozenClientCount;
        } else if (_clientScroll) --_clientScroll;
      }
      render();
      return;
    }
    if (_view == DETAILS || _view == AP_PAUSED) {
      if (dir == INavigation::DIR_BACK) {
        if (_view == AP_PAUSED) {
          _view = DETAILS;
          esp_wifi_set_promiscuous(true);
        } else {
          _view = PAUSED;
          esp_wifi_set_promiscuous(false);
          _pktCounter.store(0, std::memory_order_relaxed);
        }
        _lastHop = millis();
      } else if (dir == INavigation::DIR_PRESS) {
        if (_view == DETAILS) {
          esp_wifi_set_promiscuous(false);
          _view = AP_PAUSED;
          _apSelected = _detailScroll;
          _freezeApOrder();
        } else if (_apSelected < _frozenApCount) {
          portENTER_CRITICAL(&_apMux);
          const auto& ap = _frozenAps[_apOrder[_apSelected]];
          memcpy(_selectedBssid, ap.bssid, 6);
          memcpy(_selectedSsid, ap.ssid, sizeof(_selectedSsid));
          _clientCount = 0;
          memset(_clients, 0, sizeof(_clients));
          portEXIT_CRITICAL(&_apMux);
          _view = CLIENTS;
          esp_wifi_set_promiscuous(true);
          _clientScroll = 0;
        }
      } else if (_view == AP_PAUSED && (dir == INavigation::DIR_UP || dir == INavigation::DIR_LEFT)) {
        if (_frozenApCount) _apSelected = (_apSelected + _frozenApCount - 1) % _frozenApCount;
      } else if (_view == AP_PAUSED && (dir == INavigation::DIR_DOWN || dir == INavigation::DIR_RIGHT)) {
        if (_frozenApCount) _apSelected = (_apSelected + 1) % _frozenApCount;
      } else if (_view == DETAILS && (dir == INavigation::DIR_DOWN || dir == INavigation::DIR_RIGHT)) {
        const int visible = std::max(1, (bodyH() - 2 - 27) / 19);
        const int maxScroll = std::max(0, (int)_apCount - visible);
        if (_detailScroll < maxScroll) ++_detailScroll;
      } else if (_view == DETAILS && (dir == INavigation::DIR_UP || dir == INavigation::DIR_LEFT)) {
        if (_detailScroll) --_detailScroll;
      }
      render();
      return;
    }
    if (dir == INavigation::DIR_BACK) {
      if (_view == LIVE) { _view = GRAPH; _graphChromeDrawn = false; render(); }
      else {
        _view = LIVE;
        // Resume with a fresh, complete sweep rather than a partial window.
        memset(_chanAccum, 0, sizeof(_chanAccum));
        _sweepCh = 1;
        // The radio was locked to _detailChannel in the AP/client views.
        // Discard that channel's pending packets before resuming the sweep.
        _pktCounter.exchange(0, std::memory_order_relaxed);
        esp_wifi_set_channel(_sweepCh, WIFI_SECOND_CHAN_NONE);
        esp_wifi_set_promiscuous(true);
        _lastHop = millis();
        render();
      }
      return;
    }
    if (dir == INavigation::DIR_PRESS) {
      if (_view == LIVE) {
        esp_wifi_set_promiscuous(false);
        _pktCounter.store(0, std::memory_order_relaxed);
        memset(_chanAccum, 0, sizeof(_chanAccum));
        _view = PAUSED; _selected = _scroll;
      }
      else {
        _detailChannel = _selected + 1;
        _view = DETAILS;
        _detailScroll = 0;
        portENTER_CRITICAL(&_apMux);
        _apCount = 0;
        memset(_aps, 0, sizeof(_aps));
        portEXIT_CRITICAL(&_apMux);
        esp_wifi_set_channel(_detailChannel, WIFI_SECOND_CHAN_NONE);
        esp_wifi_set_promiscuous(true);
      }
      render();
      return;
    }
    if (dir == INavigation::DIR_UP || dir == INavigation::DIR_LEFT) {
      if (_view == PAUSED) {
        _selected = (_selected + _NUM_CH - 1) % _NUM_CH;
        if (_selected < _scroll) _scroll = _selected;
        if (_selected >= _scroll + _VISIBLE) _scroll = _selected - _VISIBLE + 1;
        render();
      } else if (_view == LIVE && _scroll > 0) { --_scroll; render(); }
    } else if (dir == INavigation::DIR_DOWN || dir == INavigation::DIR_RIGHT) {
      if (_view == PAUSED) {
        _selected = (_selected + 1) % _NUM_CH;
        if (_selected >= _scroll + _VISIBLE) _scroll = _selected - _VISIBLE + 1;
        render();
      } else if (_view == LIVE && _scroll < _NUM_CH - _VISIBLE) { ++_scroll; render(); }
    }
  }
  if (_view == DETAILS || _view == AP_PAUSED || _view == CLIENTS || _view == CLIENT_PAUSED) {
    if ((_view == DETAILS || _view == CLIENTS) && millis() - _lastDetailRender >= 350) {
      _lastDetailRender = millis();
      onRender();
    }
  } else if (_view == LIVE || _view == GRAPH) {
    if (millis() - _lastHop >= _HOP_MS) _hop();
    if (_view == GRAPH && _graphAnimating &&
        millis() - _graphLastFrame >= _GRAPH_FRAME_MS) {
      _graphLastFrame = millis();
      _renderGraph();
    }
  }
}

void WifiChannelMonitorScreen::onRender()
{
  if (_quitting) return;
  if (_view == GRAPH) { _renderGraph(); return; }
  if (_view == CLIENTS || _view == CLIENT_PAUSED) { _renderClients(); return; }
  if (_view == DETAILS || _view == AP_PAUSED) { _renderDetails(); return; }
  auto& lcd = Uni.Lcd;
  const int x = bodyX(), y = bodyY(), w = bodyW(), h = bodyH();
  if (_lastPaintedView != _view) { lcd.fillRect(x, y, w, h, TFT_BLACK); _lastPaintedView = _view; }
  lcd.setTextSize(1);
  lcd.setTextDatum(TL_DATUM);

  if (_firstSweep) {
    ShowStatusAction::show("Scanning...", 0);
    return;
  }

  // Channel list: fixed channel order, verified AP count and total packets.
  uint8_t apCounts[_NUM_CH + 1] = {};
  portENTER_CRITICAL(&_apMux);
  for (uint8_t i = 0; i < _knownApCount; ++i) {
    const uint8_t ch = _knownAps[i].channel;
    if (ch >= 1 && ch <= _NUM_CH && apCounts[ch] < 255) ++apCounts[ch];
  }
  portEXIT_CRITICAL(&_apMux);

  const int footerH = 27;
  const int rowH = (h - footerH) / _VISIBLE;
  const int contentRight = x + w - 7;
  if (rowH < 6 || contentRight - x < 75) return;
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  for (uint8_t i = 0; i < _VISIBLE; ++i) {
    const uint8_t ch = _scroll + i + 1;
    if (ch > _NUM_CH) break;
    const int top = y + i * rowH;
    lcd.fillRect(x, top, contentRight - x + 1, rowH, TFT_BLACK);
    lcd.setTextDatum(ML_DATUM);
    const bool selected = _view == PAUSED && ch == _selected + 1;
    const uint16_t bg = selected ? Config.getThemeColor() : TFT_BLACK;
    if (selected) lcd.fillRoundRect(x, top, contentRight - x - 1, rowH, 3, bg);
    lcd.setTextColor(TFT_WHITE, bg);
    lcd.drawString("Channel " + String(ch), x + 3, top + rowH / 2);
    lcd.setTextDatum(MR_DATUM);
    // Reserve right gutter for the scrollbar and selection border.
    // Keep separate compact columns for APs and packets without reaching the channel label.
    const int packetsRight = contentRight - 1;
    const int apCountRight = packetsRight - 70;
    lcd.setTextColor(selected ? TFT_WHITE : TFT_DARKGREY, bg);
    lcd.drawString(String(apCounts[ch]) + " APs", apCountRight, top + rowH / 2);
    lcd.setTextColor(selected ? TFT_WHITE : TFT_DARKGREY, bg);
    String packetLabel = String(_channelTotals[ch]) + " pkts";
    while (packetLabel.length() > 1 && lcd.textWidth(packetLabel) > 72)
      packetLabel.remove(0, 1);
    lcd.drawString(packetLabel, packetsRight, top + rowH / 2);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  }
  drawMonitorScrollBar(x + w - 3, y, h - footerH, _NUM_CH, _VISIBLE, _scroll);
  _renderHints(x, y, w, h, _view == PAUSED);
  lcd.setTextDatum(TL_DATUM);
}

void WifiChannelMonitorScreen::_renderGraph()
{
  auto& lcd = Uni.Lcd;
  const int x = bodyX(), y = bodyY(), w = bodyW(), h = bodyH();
  if (w <= 0 || h <= 32) return;
  lcd.setTextSize(1);
  const int labelH = lcd.fontHeight() + 3;
  const int hintH = 14;
  const int plotH = h - labelH - 8 - hintH;
  const int stepX = w / _NUM_CH;
  if (plotH <= 6 || stepX <= 0) return;
  const int baseline = y + plotH + 4;
  const int maxBarH = plotH - 6;

  if (!_graphChromeDrawn || _lastPaintedView != GRAPH) {
    lcd.fillRect(x, y, w, h, TFT_BLACK);
    lcd.setTextDatum(TC_DATUM);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    for (uint8_t ch = 1; ch <= _NUM_CH; ++ch)
      lcd.drawString(String(ch), x + (ch - 1) * stepX + stepX / 2, baseline + 3);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.setTextDatum(BC_DATUM);
    lcd.drawString("[Press] Details", x + w / 2, y + h - 1);
    lcd.setTextDatum(TL_DATUM);
    _graphChromeDrawn = true;
    _lastPaintedView = GRAPH;
    // Previous lines were cleared; repaint the new curve from a zero baseline.
    memset(_graphLastH, 0, sizeof(_graphLastH));
    memset(_graphStartH, 0, sizeof(_graphStartH));
    _graphAnimStart = millis();
    _graphAnimating = !_firstSweep;
  }
  if (_firstSweep) return;

  uint32_t maxActivity = 1;
  for (uint8_t ch = 1; ch <= _NUM_CH; ++ch)
    maxActivity = std::max(maxActivity, _activity[ch]);

  const uint32_t elapsed = millis() - _graphAnimStart;
  const bool done = !_graphAnimating || elapsed >= _GRAPH_ANIM_MS;
  float t = done ? 1.0f : (float)elapsed / (float)_GRAPH_ANIM_MS;
  t = 1.0f - (1.0f - t) * (1.0f - t);
  uint16_t nextH[_NUM_CH + 1] = {};
  bool changed = false;
  for (uint8_t ch = 1; ch <= _NUM_CH; ++ch) {
    const uint16_t target = (uint16_t)std::min<uint64_t>(maxBarH,
        (uint64_t)_activity[ch] * maxBarH / maxActivity);
    const int32_t delta = (int32_t)target - (int32_t)_graphStartH[ch];
    nextH[ch] = done ? target : (uint16_t)std::max<int32_t>(0,
        (int32_t)_graphStartH[ch] + (int32_t)(delta * t));
    if (nextH[ch] != _graphLastH[ch]) changed = true;
  }
  if (!changed) { if (done) _graphAnimating = false; return; }

  int cx[_NUM_CH + 1], oldY[_NUM_CH + 1], newY[_NUM_CH + 1];
  for (uint8_t ch = 1; ch <= _NUM_CH; ++ch) {
    cx[ch] = x + (ch - 1) * stepX + stepX / 2;
    oldY[ch] = baseline - _graphLastH[ch];
    newY[ch] = baseline - nextH[ch];
  }
  for (uint8_t ch = 1; ch < _NUM_CH; ++ch) {
    for (int d = -1; d <= 1; ++d)
      lcd.drawLine(cx[ch], oldY[ch] + d, cx[ch + 1], oldY[ch + 1] + d, TFT_BLACK);
  }
  for (uint8_t ch = 1; ch <= _NUM_CH; ++ch)
    lcd.fillCircle(cx[ch], oldY[ch], 3, TFT_BLACK);

  const uint16_t color = Config.getThemeColor();
  const uint16_t glow = (uint16_t)((((color >> 11) & 31) / 3 << 11) |
                                    (((color >> 5) & 63) / 3 << 5) |
                                    ((color & 31) / 3));
  for (uint8_t ch = 1; ch < _NUM_CH; ++ch) {
    lcd.drawLine(cx[ch], newY[ch] - 1, cx[ch + 1], newY[ch + 1] - 1, glow);
    lcd.drawLine(cx[ch], newY[ch] + 1, cx[ch + 1], newY[ch + 1] + 1, glow);
    lcd.drawLine(cx[ch], newY[ch], cx[ch + 1], newY[ch + 1], color);
  }
  for (uint8_t ch = 1; ch <= _NUM_CH; ++ch) {
    lcd.fillCircle(cx[ch], newY[ch], 2, color);
    lcd.fillCircle(cx[ch], newY[ch], 1, TFT_WHITE);
  }
  memcpy(_graphLastH, nextH, sizeof(nextH));
  if (done) _graphAnimating = false;
}

void WifiChannelMonitorScreen::_hop()
{
  const uint32_t now = millis();
  const uint32_t elapsedMs = std::max<uint32_t>(1, now - _lastHop);
  // Count per actual dwell time, not per full 13-channel sweep.
  const uint32_t packets = _pktCounter.exchange(0, std::memory_order_relaxed);
  _channelTotals[_sweepCh] = (uint32_t)std::min<uint64_t>(UINT32_MAX,
      (uint64_t)_channelTotals[_sweepCh] + packets);
  _chanAccum[_sweepCh] = (uint32_t)std::min<uint64_t>(UINT32_MAX,
      (uint64_t)packets * 1000U * 256U / elapsedMs);
  if (_sweepCh >= _NUM_CH) {
    _sweepCh = 1;
    memcpy(_displayCounts, _chanAccum, sizeof(_chanAccum));
    memset(_chanAccum, 0, sizeof(_chanAccum));
    // Integer EMA of packets/second in Q8: 25% new, 75% history.
    for (uint8_t ch = 1; ch <= _NUM_CH; ++ch) {
      const uint32_t sample = _displayCounts[ch];
      _activity[ch] = _firstSweep ? sample :
                      (uint32_t)(((uint64_t)_activity[ch] * 3U + sample) / 4U);
    }
    // Only LIVE calls _hop(); paused views never collect or update rankings.
    {
    // Stable insertion sort; channel number breaks equal-activity ties.
    for (uint8_t i = 0; i < _NUM_CH; ++i) _rank[i] = i + 1;
    for (uint8_t i = 1; i < _NUM_CH; ++i) {
      const uint8_t ch = _rank[i];
      int j = i;
      while (j > 0 && (_activity[_rank[j - 1]] < _activity[ch] ||
             (_activity[_rank[j - 1]] == _activity[ch] && _rank[j - 1] > ch))) {
        _rank[j] = _rank[j - 1];
        --j;
      }
      _rank[j] = ch;
    }
    }
    _firstSweep = false;
    esp_wifi_set_channel(_sweepCh, WIFI_SECOND_CHAN_NONE);
    _lastHop = millis();
    if (_view == GRAPH) {
      memcpy(_graphStartH, _graphLastH, sizeof(_graphLastH));
      _graphAnimStart = millis();
      _graphLastFrame = millis() - _GRAPH_FRAME_MS;
      _graphAnimating = true;
      _renderGraph();
    } else if (_view == LIVE) onRender();
  } else {
    ++_sweepCh;
    esp_wifi_set_channel(_sweepCh, WIFI_SECOND_CHAN_NONE);
    _lastHop = millis();
  }
}

void WifiChannelMonitorScreen::_quit()
{
  _quitting = true;
  _active = nullptr;
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  delay(10);
  esp_wifi_set_promiscuous(false);
  Screen.goBack();
}

void WifiChannelMonitorScreen::_renderDetails()
{
  auto& lcd = Uni.Lcd;
  const int x = bodyX(), y = bodyY(), w = bodyW(), h = bodyH();
  if (_lastPaintedView != _view) { lcd.fillRect(x, y, w, h, TFT_BLACK); _lastPaintedView = _view; }
  ApEntry snapshot[MAX_APS];
  uint8_t count;
  portENTER_CRITICAL(&_apMux);
  count = _apCount;
  memcpy(snapshot, _aps, count * sizeof(ApEntry));
  portEXIT_CRITICAL(&_apMux);
  if (_view == DETAILS) for (uint8_t i = 1; i < count; ++i) {
    ApEntry current = snapshot[i];
    int j = i;
    while (j > 0 && snapshot[j-1].packets < current.packets) {
      snapshot[j] = snapshot[j-1]; --j;
    }
    snapshot[j] = current;
  }
  lcd.setTextSize(1);
  lcd.setTextDatum(TL_DATUM);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  const int top = y + 2, footerH = 27, rowH = 19;
  const int visible = (h - 2 - footerH) / rowH;
  if (count == 0) {
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.drawString("Listening for AP traffic...", x + 3, top + 5);
  }
  if (visible > 0) {
    const uint8_t rows = _view == AP_PAUSED ? _frozenApCount : count;
    if (_view == AP_PAUSED) {
      if (_apSelected < _detailScroll) _detailScroll = _apSelected;
      if (_apSelected >= _detailScroll + visible) _detailScroll = _apSelected - visible + 1;
    }
    if (_detailScroll > (rows > (uint8_t)visible ? rows - visible : 0))
      _detailScroll = rows > (uint8_t)visible ? rows - visible : 0;
    const bool hasScrollbar = rows > visible;
    const int rightEdge = x + w - (hasScrollbar ? 7 : 3);
    for (int i = 0; i < visible && i + _detailScroll < rows; ++i) {
      const uint8_t position = i + _detailScroll;
      const auto& ap = _view == AP_PAUSED ? _frozenAps[_apOrder[position]] : snapshot[position];
      const int yy = top + i * rowH;
      lcd.fillRect(x, yy - 2, w - 3, rowH, TFT_BLACK);
      const bool selected = _view == AP_PAUSED && position == _apSelected;
      const uint16_t bg = selected ? Config.getThemeColor() : TFT_BLACK;
      if (selected) lcd.fillRoundRect(x, yy - 2, rightEdge - x, rowH, 3, bg);
      lcd.setTextColor(TFT_WHITE, bg);
      String name = ap.ssid[0] ? String(ap.ssid) : String("<hidden>");
      while (name.length() && lcd.textWidth(name) > rightEdge - x - 108) name.remove(name.length()-1);
      lcd.drawString(name, x + 3, yy);
      lcd.setTextDatum(TR_DATUM);
      lcd.setTextColor(selected ? TFT_WHITE : TFT_DARKGREY, bg);
      lcd.drawString(String(ap.packets) + " pkts", rightEdge - 3, yy);
      lcd.setTextDatum(TL_DATUM);
    }
  }
  if (visible > 0) {
    const int rows = _view == AP_PAUSED ? _frozenApCount : count;
    const int drawn = std::min(rows - (int)_detailScroll, visible);
    if (drawn < visible) lcd.fillRect(x, top + std::max(0, drawn) * rowH, w - 3, (visible - std::max(0, drawn)) * rowH, TFT_BLACK);
    drawMonitorScrollBar(x + w - 3, top, visible * rowH, rows, visible, _detailScroll);
  }
  _renderHints(x, y, w, h, _view == AP_PAUSED);
  lcd.setTextDatum(TL_DATUM);
}

void WifiChannelMonitorScreen::_freezeApOrder()
{
  ApEntry snapshot[MAX_APS];
  portENTER_CRITICAL(&_apMux);
  _frozenApCount = _apCount;
  memcpy(snapshot, _aps, _frozenApCount * sizeof(ApEntry));
  portEXIT_CRITICAL(&_apMux);
  memcpy(_frozenAps, snapshot, _frozenApCount * sizeof(ApEntry));
  for (uint8_t i = 0; i < _frozenApCount; ++i) _apOrder[i] = i;
  for (uint8_t i = 1; i < _frozenApCount; ++i) {
    const uint8_t current = _apOrder[i];
    int j = i;
    while (j > 0 && snapshot[_apOrder[j - 1]].packets < snapshot[current].packets) {
      _apOrder[j] = _apOrder[j - 1]; --j;
    }
    _apOrder[j] = current;
  }
}

void WifiChannelMonitorScreen::_renderClients()
{
  auto& lcd = Uni.Lcd;
  const int x = bodyX(), y = bodyY(), w = bodyW(), h = bodyH();
  if (_lastPaintedView != _view) { lcd.fillRect(x, y, w, h, TFT_BLACK); _lastPaintedView = _view; }
  ClientEntry snapshot[MAX_CLIENTS];
  uint8_t count;
  portENTER_CRITICAL(&_apMux);
  count = _clientCount;
  memcpy(snapshot, _clients, count * sizeof(ClientEntry));
  portEXIT_CRITICAL(&_apMux);
  if (_view == CLIENTS) {
    for (uint8_t i = 1; i < count; ++i) {
      ClientEntry current = snapshot[i];
      int j = i;
      while (j > 0 && snapshot[j-1].packets < current.packets) {
        snapshot[j] = snapshot[j-1]; --j;
      }
      snapshot[j] = current;
    }
  }
  lcd.setTextSize(1);
  lcd.setTextDatum(TL_DATUM);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  const int top = y + 2, footerH = 27, rowH = 19;
  const int visible = (h - 2 - footerH) / rowH;

  const bool paused = _view == CLIENT_PAUSED;
  const uint8_t rows = paused ? _frozenClientCount : count;
  if (visible > 0) {
    if (paused) {
      if (_clientSelected < _clientScroll) _clientScroll = _clientSelected;
      if (_clientSelected >= _clientScroll + visible) _clientScroll = _clientSelected - visible + 1;
    }
    const uint8_t maxScroll = rows > visible ? rows - visible : 0;
    if (_clientScroll > maxScroll) _clientScroll = maxScroll;
    const bool hasScrollbar = rows > visible;
    const int rightEdge = x + w - (hasScrollbar ? 7 : 3);
    for (int i = 0; i < visible && i + _clientScroll < rows; ++i) {
      const uint8_t pos = i + _clientScroll;
      const int yy = top + i * rowH;
      lcd.fillRect(x, yy - 2, w - 3, rowH, TFT_BLACK);
      const bool selected = paused && pos == _clientSelected;
      const uint16_t bg = selected ? Config.getThemeColor() : TFT_BLACK;
      if (selected) lcd.fillRoundRect(x, yy - 2, rightEdge - x, rowH, 3, bg);
      const auto& client = paused ? _frozenClients[_clientOrder[pos]] : snapshot[pos];
      char mac[18];
      snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
               client.mac[0],client.mac[1],client.mac[2],client.mac[3],client.mac[4],client.mac[5]);
      lcd.setTextColor(TFT_WHITE, bg);
      lcd.drawString(mac, x + 3, yy);
      lcd.setTextDatum(TR_DATUM);
      lcd.setTextColor(selected ? TFT_WHITE : TFT_DARKGREY, bg);
      lcd.drawString(String(client.packets) + " pkts", rightEdge - 3, yy);
      lcd.setTextDatum(TL_DATUM);
    }
  }
  if (visible > 0) {
    const int drawn = std::min((int)rows - (int)_clientScroll, visible);
    if (drawn < visible) lcd.fillRect(x, top + std::max(0, drawn) * rowH, w - 3, (visible - std::max(0, drawn)) * rowH, TFT_BLACK);
    drawMonitorScrollBar(x + w - 3, top, visible * rowH, rows, visible, _clientScroll);
  }
  _renderHints(x, y, w, h, paused);
  lcd.setTextDatum(TL_DATUM);
}

void WifiChannelMonitorScreen::_openOptions()
{
  if (_view != PAUSED && _view != AP_PAUSED && _view != CLIENT_PAUSED) return;
  static const InputSelectAction::Option channelOptions[] = {
    {"Packet Monitor", "monitor", nullptr, 0, false},
    {"Packet Sniffer", "sniffer", nullptr, 0, false},
  };
  static const InputSelectAction::Option targetOptions[] = {
    {"Packet Monitor", "monitor", nullptr, 0, false},
    {"Packet Sniffer", "sniffer", nullptr, 0, false},
    {"Fox Hunt", "foxhunt", nullptr, 0, false},
  };
  const bool target = _view != PAUSED;
  const char* choice = InputSelectAction::popup("Options", target ? targetOptions : channelOptions,
                                                target ? 3 : 2);
  if (choice == nullptr) { render(); return; }
  if (strcmp(choice, "monitor") == 0) _openPacketMonitor();
  else if (strcmp(choice, "sniffer") == 0) _openPacketSniffer();
  else if (strcmp(choice, "foxhunt") == 0) _openFoxHunt();
}

void WifiChannelMonitorScreen::_openPacketSniffer()
{
  if (_view != PAUSED && _view != AP_PAUSED && _view != CLIENT_PAUSED) return;
  uint8_t mac[6];
  uint8_t bssid[6];
  const uint8_t* apFilter = nullptr;
  const uint8_t* client = nullptr;
  const uint8_t channel = _view == PAUSED ? _selected + 1 : _detailChannel;
  if (_view == AP_PAUSED) {
    if (_apSelected >= _frozenApCount) return;
    memcpy(bssid, _frozenAps[_apOrder[_apSelected]].bssid, 6);
    apFilter = bssid;
  } else if (_view == CLIENT_PAUSED) {
    if (_clientSelected >= _frozenClientCount) return;
    memcpy(bssid, _selectedBssid, 6);
    apFilter = bssid;
    memcpy(mac, _frozenClients[_clientOrder[_clientSelected]].mac, 6);
    client = mac;
  }
  auto* sniffer = new WifiPacketSnifferScreen();
  sniffer->configureFromChannel(channel, apFilter, client);
  _active = nullptr;
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  delay(10);
  esp_wifi_set_promiscuous(false);
  Screen.push(sniffer);
}

void WifiChannelMonitorScreen::_openFoxHunt()
{
  if (_view != AP_PAUSED && _view != CLIENT_PAUSED) return;
  uint8_t mac[6];
  const bool client = _view == CLIENT_PAUSED;
  const char* ssid = nullptr;
  if (client) {
    if (_clientSelected >= _frozenClientCount) return;
    memcpy(mac, _frozenClients[_clientOrder[_clientSelected]].mac, 6);
  } else {
    if (_apSelected >= _frozenApCount) return;
    const auto& ap = _frozenAps[_apOrder[_apSelected]];
    memcpy(mac, ap.bssid, 6);
    ssid = ap.ssid;
  }
  auto* hunt = new WifiFoxHuntScreen();
  hunt->configureTarget(client, mac, _detailChannel, ssid);
  _active = nullptr;
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  delay(10);
  esp_wifi_set_promiscuous(false);
  Screen.push(hunt);
}

void WifiChannelMonitorScreen::_openPacketMonitor()
{
  if (_view != PAUSED && _view != AP_PAUSED && _view != CLIENT_PAUSED) return;
  uint8_t mac[6];
  uint8_t bssid[6];
  const uint8_t* apFilter = nullptr;
  const char* apSsid = nullptr;
  const uint8_t* client = nullptr;
  const uint8_t channel = _view == PAUSED ? _selected + 1 : _detailChannel;
  if (_view == AP_PAUSED) {
    if (_apSelected >= _frozenApCount) return;
    memcpy(bssid, _frozenAps[_apOrder[_apSelected]].bssid, 6);
    apFilter = bssid;
    apSsid = _frozenAps[_apOrder[_apSelected]].ssid;
  } else if (_view == CLIENT_PAUSED) {
    memcpy(bssid, _selectedBssid, 6);
    apFilter = bssid;
    apSsid = _selectedSsid;
    if (_clientSelected < _frozenClientCount) {
      memcpy(mac, _frozenClients[_clientOrder[_clientSelected]].mac, 6);
      client = mac;
    }
  }
  auto* monitor = new WifiPacketMonitorScreen();
  monitor->configureFromChannel(channel, apFilter, client, apSsid);
  _active = nullptr;
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  delay(10);
  esp_wifi_set_promiscuous(false);
  Screen.push(monitor);
}

void WifiChannelMonitorScreen::onRestore()
{
  WiFi.mode(WIFI_STA);
  _active = this;
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(&_captureCb);
  // Restore the radio to the channel appropriate for the current view.
  // In particular, LIVE must resume the hopping sequence, not the AP channel.
  _pktCounter.exchange(0, std::memory_order_relaxed);
  esp_wifi_set_channel((_view == LIVE || _view == GRAPH || _view == PAUSED) ? _sweepCh : _detailChannel,
                       WIFI_SECOND_CHAN_NONE);
  // A paused screen must not restart capture when returning from Packet Monitor.
  const bool capturing = _view == LIVE || _view == GRAPH || _view == DETAILS || _view == CLIENTS;
  esp_wifi_set_promiscuous(capturing);
  if (_view == LIVE || _view == GRAPH) {
    memset(_chanAccum, 0, sizeof(_chanAccum));
    _sweepCh = 1;
    esp_wifi_set_channel(_sweepCh, WIFI_SECOND_CHAN_NONE);
  }
  _lastHop = millis();
  _lastDetailRender = 0;
  _lastPaintedView = 255;
  render();
}

void WifiChannelMonitorScreen::_renderHints(int x, int y, int w, int h, bool paused)
{
  auto& lcd = Uni.Lcd;
  const int footerTop = y + h - 27;
  lcd.fillRect(x, footerTop, w, 27, TFT_BLACK);
  lcd.drawFastHLine(x, footerTop, w, TFT_DARKGREY);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.setTextDatum(BC_DATUM);
  if (paused) {
    lcd.drawString(_view == CLIENT_PAUSED ? "[Back] Resume" : "[Press] Select [Back] Resume", x + w / 2, y + h - 14);
    lcd.drawString("[Hold] Options", x + w / 2, y + h - 1);
  } else {
    // Reserve the upper hint line to prevent vertical jumps on pause.
    if (_view == LIVE) {
      lcd.drawString("[Press] Pause", x + w / 2, y + h - 1);
    } else {
      lcd.drawString("[Press] Pause", x + w / 2, y + h - 1);
    }
  }
  lcd.setTextDatum(TL_DATUM);
}
