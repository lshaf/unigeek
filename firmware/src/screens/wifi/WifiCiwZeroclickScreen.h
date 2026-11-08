#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/ScrollListView.h"
#include <WiFi.h>
#include <vector>

class WifiCiwZeroclickScreen : public ListScreen
{
public:
  const char* title() override { return "CIW Zeroclick"; }
  bool inhibitPowerOff() override { return _state == STATE_BROADCASTING; }

  ~WifiCiwZeroclickScreen() override;

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onUpdate() override;
  void onRender() override;
  void onBack() override;

private:
  // Menu rows, in display order. The menu is rebuilt with the row to keep
  // highlighted, so an edit doesn't send the highlight back to the top.
  enum MenuRow : uint8_t { ROW_CATEGORIES, ROW_ROTATION, ROW_DEVICES, ROW_ALERTS, ROW_START, MENU_ROWS };

  enum State { STATE_MENU, STATE_BROADCASTING, STATE_DEVICES, STATE_ALERTS };

  enum CatIdx : uint8_t {
    CAT_CMD=0, CAT_OVERFLOW, CAT_FMT, CAT_PROBE, CAT_ESC, CAT_SERIAL,
    CAT_ENC, CAT_CHAIN, CAT_HEAP, CAT_XSS, CAT_PATH, CAT_CRLF,
    CAT_JNDI, CAT_NOSQL, CAT_COUNT
  };

  struct Payload { const char* ssid; uint8_t cat; };
  struct TrackedDevice { uint8_t mac[6]; unsigned long connectTime; int payloadIdx; };
  struct Alert { uint8_t mac[6]; char ssid[33]; unsigned long durationMs; };

  State _state = STATE_MENU;
  uint16_t _catMask = 0x3FFF;
  unsigned long _rotationMs = 5000;
  int _currentIdx = 0;
  unsigned long _lastRotation = 0;
  uint32_t _lastDrawMs = 0;
  bool _chromeDrawn = false;

  std::vector<Payload> _active;
  TrackedDevice _devices[30];
  int _deviceCount = 0;
  Alert _alerts[30];
  int _alertCount = 0;
  int _alertHead = 0;

  wifi_event_id_t _wifiEventId = 0;

  String _catSub;
  String _rotSub;
  String _devSub;
  String _alertSub;
  ListItem _menuItems[6];

  ScrollListView _scrollView;
  ScrollListView::Row _scrollRows[30];
  String _scrollLabels[30];

  void _refreshMenu(uint8_t row);
  void _loadPayloads();
  void _startBroadcast();
  void _stopBroadcast();
  void _tickBroadcast();
  void _drawBroadcasting();
  void _selectCategories();
  void _showDevices();
  void _showAlerts();

  static void _onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
  static WifiCiwZeroclickScreen* _instance;

  static const char* const _catNames[];
  static const Payload _allPayloads[];
  static const int _allPayloadCount;
};
