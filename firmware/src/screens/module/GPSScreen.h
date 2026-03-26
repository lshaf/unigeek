//
// Created by L Shaf on 2026-03-26.
//

#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/components/ScrollListView.h"
#include "utils/GPSModule.h"

class GPSScreen : public ListScreen
{
public:
  const char* title() override { return "GPS"; }
  bool inhibitPowerOff() override { return _state == STATE_WARDRIVING; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  enum State {
    STATE_LOADING,
    STATE_MENU,
    STATE_INFO,
    STATE_WARDRIVING,
    STATE_STATS,
    STATE_UPLOAD,
  } _state = STATE_LOADING;

  GPSModule _gps;
  unsigned long _lastRender = 0;
  unsigned long _initTime = 0;
  bool _infoInitialized = false;

  // GPS pin config (set in onInit)
  int8_t _txPin = -1;
  int8_t _rxPin = -1;
  uint32_t _baudRate = 9600;

  ListItem _menuItems[6] = {
    {"View GPS Info"},
    {"Wardriver"},
    {"Internet"},
    {"Wigle Token"},
    {"Wardrive Stat"},
    {"Upload Wardrive"},
  };

  ScrollListView _infoView;
  ScrollListView::Row _infoRows[8];

  // Wigle stats
  ScrollListView _statsView;
  static constexpr uint8_t MAX_STAT_ROWS = 12;
  ScrollListView::Row _statsRows[MAX_STAT_ROWS];
  bool _statsInitialized = false;

  // Wigle upload
  static constexpr const char* _wigleTokenPath = "/unigeek/wigle_token";
  static constexpr const char* _wardrivePath = "/unigeek/gps/wardriver";
  static constexpr uint8_t MAX_FILES = 20;
  ListItem _uploadItems[MAX_FILES];
  String _wigleTokenSub;
  String _internetSub;
  String _fileNames[MAX_FILES];
  uint8_t _fileCount = 0;

  void _showMenu();
  void _connectInternet();
  void _editWigleToken();
  void _showWigleStats();
  void _showUploadMenu();
  void _uploadFile(uint8_t fileIndex);
  void _renderInfo();
  void _renderWardriver();
  void _enableGnssPower();
  void _disableGnssPower();
};
