#pragma once
#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"
#include "ui/views/LogView.h"
#include "ui/views/ScrollListView.h"

class ChameleonMfcScreen : public ListScreen {
public:
  enum StartAction {
    ACTION_READ_TAG,
    ACTION_SHOW_KEYS,
    ACTION_DICTIONARY,
    ACTION_STATIC_NESTED,
    ACTION_NESTED,
  };

  explicit ChameleonMfcScreen(StartAction action = ACTION_READ_TAG) : _startAction(action) {}
  const char* title() override;
  bool inhibitPowerOff() override { return _running; }

  void onInit()                      override;
  void onUpdate()                    override;
  void onRender()                    override;
  void onItemSelected(uint8_t index) override;
  void onBack()                      override;

private:
  enum State {
    STATE_AUTH,
    STATE_MF_MENU,
    STATE_SHOW_KEYS,
    STATE_DUMP,
    STATE_DUMP_RESULT,
    STATE_DICT_SEL,
    STATE_DICT_RUN,
    STATE_DICT_LOG,
    STATE_STATIC_NESTED,
    STATE_STATIC_NESTED_LOG,
    STATE_NESTED,
    STATE_NESTED_LOG,
  };

  State _state   = STATE_AUTH;
  StartAction _startAction = ACTION_READ_TAG;
  bool _resumeReadAfterAttack = false;
  bool  _running = false;

  // Card info
  uint8_t _uid[7]  = {};
  uint8_t _uidLen  = 0;
  uint8_t _sak     = 0;
  uint8_t _atqa[2] = {};
  uint8_t _sectors = 16;

  // Current MIFARE Classic dump retained in RAM until the user saves it.
  uint8_t* _dump       = nullptr;
  uint16_t _dumpLen    = 0;
  uint16_t _dumpBlocks = 0;

  // Discovered keys
  uint8_t _keysA[40][6] = {};
  uint8_t _keysB[40][6] = {};
  bool    _foundA[40]   = {};
  bool    _foundB[40]   = {};
  int     _recovered    = 0;

  // MF submenu
  ListItem _mfItems[5] = {
    {"Discovered Keys"},
    {"Dump Memory"},
    {"Dictionary Attack"},
    {"Static Nested"},
    {"Nested Attack"},
  };

  // Auth log
  LogView _authLog;
  char    _authStatus[48] = {};
  int     _authPct = 0;
  static void _authStatusBarCb(Sprite& sp, int barY, int width, void* userData);

  // Action log (dump + dict)
  LogView _actionLog;
  char    _actionStatus[48] = {};
  int     _actionPct = 0;
  static void _actionStatusBarCb(Sprite& sp, int barY, int width, void* userData);

  // Keys result view
  ScrollListView _scrollView;
  static constexpr int MAX_ROWS = 520;
  ScrollListView::Row _rows[MAX_ROWS];
  String _rowLabels[MAX_ROWS];
  String _rowValues[MAX_ROWS];
  uint16_t _rowCount = 0;

  // Dict file picker
  static constexpr const char* _kDictDir = "/unigeek/nfc/dictionaries";
  BrowseFileView _browser;
  ListItem _dictItems[1 + BrowseFileView::kCap];
  uint8_t  _dictFileCount = 0;
  String   _dictPickDir;        // current directory in the dict picker
  static constexpr uint16_t MAX_DICT_KEYS = 256;
  uint8_t  _dictKeys[MAX_DICT_KEYS][6] = {};
  uint16_t _dictKeyCount = 0;

  uint8_t  _trailerBlock(uint8_t sector);
  uint16_t _totalBlocks();
  void _goMfMenu();
  void _dispatchStartAction();
  void _continueRead();
  void _callAuth();
  void _showDiscoveredKeys();
  void _callDump();
  void _buildDumpPreview();
  void _buildDumpHex();
  void _showDumpActions();
  void _loadDumpToSlot();
  void _saveDump();
  void _freeDump();
  bool _extractDumpNdef(uint8_t** ndef, size_t* ndefLen) const;
  void _loadDictPicker();
  bool _loadDictFile(const char* path);
  void _runDictAttack();
  void _buildKeyRows();
  void _loadKeys();
  void _saveKeys();

  // Nested attack UI helpers (nonce collection now done firmware-side).
  void _log(const char* line, uint16_t color);
  void _callStaticNested();
  void _callNestedAttack();
};
