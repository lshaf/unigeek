#pragma once
#include "ui/templates/ListScreen.h"
#include "utils/TotpUtil.h"

class TotpScreen : public ListScreen {
public:
  const char* title()                        override;
  bool inhibitPowerSave()                    override { return _state == STATE_VIEW; }
  void onInit()                              override;
  void onUpdate()                            override;
  void onRender()                            override;
  void onBack()                              override;
  void onItemSelected(uint8_t index)         override;

private:
  enum State { STATE_MENU, STATE_ADD, STATE_VIEW };
  State _state = STATE_MENU;

  // ── menu ──────────────────────────────────────────────────
  static constexpr uint8_t     MAX_ACCOUNTS = 16;
  static constexpr const char* DIR          = "/unigeek/utility/totp";

  struct Account { char name[32]; char secret[64]; uint8_t digits; uint8_t period; TotpAlgo algo; };

  Account  _accounts[MAX_ACCOUNTS];
  uint8_t  _accountCount = 0;
  ListItem _items[MAX_ACCOUNTS + 1];

  // ── add-account form ──────────────────────────────────────
  String   _pendingName;
  String   _pendingSecret;
  uint8_t  _pendingDigits = 6;
  uint8_t  _pendingPeriod = 30;
  TotpAlgo _pendingAlgo   = TotpAlgo::SHA1;
  char     _addNameLabel[16]  = {};
  char     _addSecretLabel[6] = {};
  char     _addDigitsLabel[3] = {};
  char     _addPeriodLabel[4] = {};
  char     _addAlgoLabel[7]   = {};
  ListItem _addItems[6];

  // ── view ──────────────────────────────────────────────────
  char     _viewName[32]   = {};
  char     _viewSecret[64] = {};
  uint8_t  _viewDigits     = 6;
  uint8_t  _viewPeriod     = 30;
  TotpAlgo _viewAlgo       = TotpAlgo::SHA1;
  uint32_t _code           = 0;
  uint8_t  _secsLeft       = 30;
  bool     _timeValid      = false;
  uint32_t _lastRefMs      = 0;
  bool     _viewFirstRender = false;

  bool    _holdFired = false;

  void _loadAccounts();
  void _reloadMenu();
  void _addNew();
  void _enterAdd();
  void _updateAddLabels();
  void _saveNew();
  void _showAccountMenu(uint8_t index);
  void _deleteAccount(uint8_t index);
  void _enterView(uint8_t index);
  void _refreshCode();
  void _renderView();
};
