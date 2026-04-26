#pragma once
#include "ui/templates/ListScreen.h"
#include "ui/views/ScrollListView.h"
#include "ui/views/LogView.h"

class ChameleonMfcDictScreen : public ListScreen {
public:
  const char* title() override { return "MF Dict Attack"; }
  bool inhibitPowerOff() override { return _running; }

  void onInit()                      override;
  void onUpdate()                    override;
  void onRender()                    override;
  void onItemSelected(uint8_t index) override;
  void onBack()                      override;

private:
  enum State { STATE_SELECT, STATE_RUNNING, STATE_LOG_DONE, STATE_RESULT };

  State    _state   = STATE_SELECT;
  bool     _running = false;

  // ── File picker ──
  static constexpr uint8_t MAX_DICT_FILES = 12;
  ListItem _items[1 + MAX_DICT_FILES];          // "Built-in" + file list
  String   _fileNames[MAX_DICT_FILES];
  uint8_t  _fileCount   = 0;

  // ── Attack state ──
  uint8_t  _uid[7]    = {};
  uint8_t  _uidLen    = 0;
  uint8_t  _sectors   = 16;
  uint8_t  _keysA[40][6] = {};
  uint8_t  _keysB[40][6] = {};
  bool     _foundA[40]   = {};
  bool     _foundB[40]   = {};
  int      _recovered    = 0;

  // Active key set
  static constexpr uint16_t MAX_KEYS = 256;
  uint8_t  _keys[MAX_KEYS][6] = {};
  uint16_t _keyCount = 0;

  // ── Run log view ──
  LogView _runLog;
  char    _runStatus[48] = {};
  int     _runPct = 0;
  static void _runStatusBarCb(Sprite& sp, int barY, int width, void* userData);

  // ── Result view ──
  ScrollListView      _scrollView;
  static constexpr int MAX_RESULT_ROWS = 80;
  ScrollListView::Row _rows[MAX_RESULT_ROWS];
  String              _rowLabels[MAX_RESULT_ROWS];
  String              _rowValues[MAX_RESULT_ROWS];
  uint16_t            _rowCount = 0;

  void _loadFilePicker();
  bool _loadBuiltinKeys();
  bool _loadFileKeys(const char* path);
  void _runAttack(const char* sourceLabel);
  void _buildResultRows();
  void _saveKeys();
  uint8_t _trailerBlock(uint8_t sector);
};
