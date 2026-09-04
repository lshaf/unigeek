#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"

class WifiBeaconFloodScreen : public ListScreen
{
public:
  const char* title() override { return "Beacon Flood"; }
  bool inhibitPowerOff() override { return _state == STATE_ATTACKING; }

  ~WifiBeaconFloodScreen() override;

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onUpdate() override;
  void onRender() override;
  void onBack() override;

private:
  enum State {
    STATE_MENU,
    STATE_SCAN,
    STATE_SELECT_AP,
    STATE_FILE_PICK,
    STATE_ATTACKING
  };
  enum Mode       { MODE_SPAM, MODE_FLOOD };
  enum SpamTarget { SPAM_BUILTIN, SPAM_FILE };

  static constexpr int MAX_AP          = 20;
  static constexpr int MAX_FILE_SSIDS  = 200;
  static constexpr const char* DICT_DIR = "/unigeek/wifi/beacon_dicts";

  struct ApEntry {
    char    ssid[33];
    uint8_t bssid[6];
    uint8_t channel;
    int16_t rssi;
  };

  State      _state      = STATE_MENU;
  Mode       _mode       = MODE_SPAM;
  SpamTarget _spamTarget = SPAM_BUILTIN;

  ApEntry  _apList[MAX_AP]          = {};
  char     _apSubLabels[MAX_AP][18] = {};
  int      _apCount     = 0;
  int      _floodTarget = -1;

  class WifiAttackUtil* _attacker = nullptr;

  int      _ssidIdx     = 0;
  int      _rounds      = 0;
  int      _sentThisSec = 0;
  int      _ratePerSec  = 0;
  uint32_t _lastRateTick = 0;
  uint32_t _lastSendMs   = 0;
  bool     _chromeDrawn  = false;

  // ── File-loaded SSIDs (SPAM_FILE) ────────────────────────────────────────
  char     _fileSsids[MAX_FILE_SSIDS][33] = {};
  int      _fileSsidCount = 0;
  String   _fileLabel;                              // basename for sublabel

  // ── File picker (BrowseFileView + virtual "Built In" entry at index 0) ─
  BrowseFileView _browser;
  ListItem       _pickerItems[BrowseFileView::kCap + 1] = {};
  String         _pickerDir;   // current directory in the file picker

  String   _modeSub;
  String   _targetSub;
  String   _startSub;
  ListItem _menuItems[3] = { {"Mode"}, {"Target"}, {"Start"} };
  ListItem _apItems[MAX_AP + 1];
  bool     _scanValid = false;

  void _updateMenuValues();
  void _showFilePicker();
  bool _loadDictFile(const String& path);
  void _startScan(bool forceScan = false);
  void _showScanResults();
  void _startAttack();
  void _stop();
  void _broadcastNext();
  void _drawAttacking();
};
