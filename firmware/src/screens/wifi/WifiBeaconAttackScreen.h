#pragma once

#include "ui/templates/ListScreen.h"

class WifiBeaconAttackScreen : public ListScreen
{
public:
  const char* title() override { return "Beacon Attack"; }
  bool inhibitPowerOff() override { return _state == STATE_ATTACKING; }

  ~WifiBeaconAttackScreen() override;

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onUpdate() override;
  void onRender() override;
  void onBack() override;

private:
  enum State      { STATE_MENU, STATE_SCAN, STATE_SELECT_AP, STATE_ATTACKING };
  enum Mode       { MODE_SPAM, MODE_FLOOD };
  enum SpamTarget { SPAM_DICTIONARY, SPAM_RANDOM };

  static constexpr int MAX_AP = 20;

  struct ApEntry {
    char    ssid[33];
    uint8_t bssid[6];
    uint8_t channel;
  };

  State      _state      = STATE_MENU;
  Mode       _mode       = MODE_SPAM;
  SpamTarget _spamTarget = SPAM_DICTIONARY;

  ApEntry  _apList[MAX_AP]          = {};
  char     _apSubLabels[MAX_AP][16] = {};
  int      _apCount     = 0;
  int      _floodTarget = -1;

  class WifiAttackUtil* _attacker = nullptr;

  int      _ssidIdx     = 0;
  int      _rounds      = 0;
  int      _sentThisSec = 0;
  int      _ratePerSec  = 0;
  uint32_t _lastRateTick = 0;
  bool     _chromeDrawn  = false;
  char     _randomSsid[33] = {};

  String   _modeSub;
  String   _targetSub;
  String   _startSub;
  ListItem _menuItems[3];
  ListItem _apItems[MAX_AP];

  void _updateMenuValues();
  void _startScan();
  void _startAttack();
  void _stop();
  void _broadcastNext();
  void _drawAttacking();
  void _makeRandomSsid();
};
