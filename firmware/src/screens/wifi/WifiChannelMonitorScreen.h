#pragma once

#include <WiFi.h>
#include <esp_wifi.h>
#include <atomic>

#include "ui/templates/BaseScreen.h"

class WifiChannelMonitorScreen : public BaseScreen
{
public:
  const char* title() override {
    if (_view == DETAILS || _view == AP_PAUSED) {
      snprintf(_headerTitle, sizeof(_headerTitle), "Channel %u", _detailChannel);
      return _headerTitle;
    }
    if (_view == CLIENTS || _view == CLIENT_PAUSED) {
      if (_selectedSsid[0]) {
        snprintf(_headerTitle, sizeof(_headerTitle), "%.18s", _selectedSsid);
      } else {
        snprintf(_headerTitle, sizeof(_headerTitle), "AP %02X:%02X:%02X", _selectedBssid[3], _selectedBssid[4], _selectedBssid[5]);
      }
      return _headerTitle;
    }
    return "Traffic Monitor";
  }
  bool inhibitPowerSave() override { return true; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onRestore() override;

private:
  static constexpr uint8_t  _NUM_CH = 13;
  static constexpr uint8_t  _VISIBLE = 5;
  static constexpr uint32_t _HOP_MS = 2000 / _NUM_CH;
  static constexpr uint32_t _GRAPH_MAX_PPS = 100;

  enum ViewState : uint8_t { LIVE, GRAPH, PAUSED, DETAILS, AP_PAUSED, CLIENTS, CLIENT_PAUSED };
  static constexpr uint8_t MAX_APS = 48;
  static constexpr uint8_t MAX_KNOWN_APS = 96;
  struct KnownAp { uint8_t bssid[6]; uint8_t channel; };
  KnownAp _knownAps[MAX_KNOWN_APS] = {};
  uint8_t _knownApCount = 0;
  struct ApEntry { uint8_t bssid[6]; char ssid[33]; uint32_t packets; int8_t rssi; uint8_t security; int8_t wps; };
  static WifiChannelMonitorScreen* _active;
  static void _captureCb(void* buf, wifi_promiscuous_pkt_type_t type);
  portMUX_TYPE _apMux = portMUX_INITIALIZER_UNLOCKED;
  ApEntry _aps[MAX_APS] = {};
  ApEntry _frozenAps[MAX_APS] = {};
  uint8_t _apCount = 0;
  uint8_t _detailChannel = 1;
  uint8_t _detailScroll = 0;
  uint8_t _apSelected = 0;
  uint8_t _apOrder[MAX_APS] = {};
  uint8_t _frozenApCount = 0;
  static constexpr uint8_t MAX_CLIENTS = 64;
  struct ClientEntry { uint8_t mac[6]; uint32_t packets; int8_t rssi; };
  ClientEntry _clients[MAX_CLIENTS] = {};
  ClientEntry _frozenClients[MAX_CLIENTS] = {};
  uint8_t _clientCount = 0;
  uint8_t _clientScroll = 0;
  uint8_t _clientSelected = 0;
  uint8_t _frozenClientCount = 0;
  uint8_t _clientOrder[MAX_CLIENTS] = {};
  uint8_t _selectedBssid[6] = {};
  char _selectedSsid[33] = {};

  char _headerTitle[24] = {};
  uint8_t _lastPaintedView = 255;
  uint8_t _selected = 0;
  ViewState _view = LIVE;
  uint32_t _lastDetailRender = 0;
  uint8_t  _sweepCh = 1;
  uint8_t  _scroll = 0;
  uint8_t  _rank[_NUM_CH] = {};
  uint32_t _lastHop = 0;
  bool     _quitting = false;
  bool     _holdHandled = false;
  bool     _firstSweep = true;

  uint32_t _chanAccum[_NUM_CH + 1] = {};
  uint32_t _displayCounts[_NUM_CH + 1] = {};
  uint32_t _activity[_NUM_CH + 1] = {};
  uint32_t _channelTotals[_NUM_CH + 1] = {};

  static constexpr uint32_t _GRAPH_ANIM_MS = 1500;
  static constexpr uint32_t _GRAPH_FRAME_MS = 50;
  uint16_t _graphLastH[_NUM_CH + 1] = {};
  uint16_t _graphStartH[_NUM_CH + 1] = {};
  uint32_t _graphAnimStart = 0;
  uint32_t _graphLastFrame = 0;
  bool _graphAnimating = false;
  bool _graphChromeDrawn = false;
  void _renderGraph();
  void _renderDetails();
  void _renderClients();
  void _renderHints(int x, int y, int w, int h, bool paused);
  void _openOptions();
  void _openSelectedDetails();
  void _openFoxHunt();
  void _openPacketMonitor();
  void _openPacketSniffer();
  void _freezeApOrder();
  void _hop();
  void _quit();
};
