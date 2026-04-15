//
// Created by L Shaf on 2026-03-26.
//

#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/ScrollListView.h"
#include "ui/views/LogView.h"
#include "utils/gps/GPSModule.h"
#include "utils/gps/WigleUtil.h"

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

  ListItem _menuItems[8] = {
    {"View GPS Info"},
    {"Scan Mode"},
    {"Wardrive Mode"},
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
  ScrollListView::Row _statsRows[WigleUtil::MAX_STAT_ROWS];

  // Wigle upload
  ListItem _uploadItems[WigleUtil::MAX_FILES];
  String _fileNames[WigleUtil::MAX_FILES];
  String _fileLabels[WigleUtil::MAX_FILES];
  bool _fileUploaded[WigleUtil::MAX_FILES];
  uint8_t _fileCount = 0;

  String _wigleTokenSub;
  String _internetSub;
  String _scanModeSub;
  String _wardModeSub;
  GPSModule::ScanMode _scanMode = GPSModule::SCAN_WIFI_BLE;
  GPSModule::WardriveMode _wardMode = GPSModule::MODE_DRIVING;

  void _showMenu();
  void _selectScanMode();
  void _selectWardriveMode();
  void _connectInternet();
  void _editWigleToken();
  void _showWigleStats();
  void _showUploadMenu();
  void _uploadFile(uint8_t fileIndex);
  void _renderInfo();
  void _renderWardriver();
  static void _wardStatusCb(Sprite& sp, int barY, int width, void* userData);
  LogView _wardLog;

  void _enableGnssPower();
  void _disableGnssPower();
};
