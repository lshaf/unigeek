#pragma once

#include <array>
#include "ui/templates/ListScreen.h"
#include "ui/views/ScrollListView.h"
#include "utils/nfc/NFCUtility.h"
#include "utils/nfc/pn532/PN532HSU.h"
#include "utils/nfc/pn532/PN532.h"

class PN532UartScreen : public ListScreen
{
public:
  const char* title() override;
  bool inhibitPowerOff() override { return true; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  enum State_e {
    STATE_MAIN_MENU,
    STATE_INFO,
    STATE_SCAN_RESULT,
    STATE_SCAN_14A,
    STATE_SCAN_15,
    STATE_SCAN_LF,
    STATE_MIFARE_MENU,
    STATE_MIFARE_DUMP,
    STATE_MIFARE_KEYS,
    STATE_DICT_SELECT,
    STATE_ULTRALIGHT_MENU,
    STATE_MAGIC_MENU,
    STATE_RAW_RESULT,
    STATE_EMULATE,
  };

  State_e _state = STATE_MAIN_MENU;

  PN532HSU* _hsu = nullptr;
  PN532*    _pn  = nullptr;
  bool      _ready = false;
  bool      _isKiller = false;
  uint8_t   _killerCode = 0;
  PN532::FirmwareInfo _fw;

  // Last 14A target (for Mifare flow)
  PN532::Target14A _card{};
  bool _hasCard = false;
  std::array<std::pair<NFCUtility::MIFARE_Key, NFCUtility::MIFARE_Key>, 40> _mfKeys;

  // Mifare card sizing: sectors, blocks
  std::pair<size_t, size_t> _mfDims(uint8_t sak) const;

  // Scroll view for info / dump / keys / raw
  ScrollListView _scrollView;
  static constexpr size_t MAX_ROWS = 520;
  ScrollListView::Row _rows[MAX_ROWS];
  String _rowLabels[MAX_ROWS];
  String _rowValues[MAX_ROWS];
  uint16_t _rowCount = 0;

  // Menus
  ListItem _mainItems[7] = {
    {"Scan ISO14443A"},
    {"Scan ISO15693"},
    {"Scan EM4100 (LF)"},
    {"MIFARE Classic"},
    {"MIFARE Ultralight"},
    {"Magic Card"},
    // {"Emulate Card"},  // hidden until full APDU emulation is ready
    {"Firmware Info"},
  };

  ListItem _mfItems[4] = {
    {"Authenticate"},
    {"Dump Memory"},
    {"Discovered Keys"},
    {"Dictionary Attack"},
  };

  ListItem _ulItems[2] = {
    {"Read All Pages"},
    {"Write Page"},
  };

  ListItem _magicItems[3] = {
    {"Detect Gen1a"},
    {"Gen3 Set UID"},
    {"Gen3 Lock UID"},
  };

  // Dictionary file picker
  static constexpr const char* _dictPath = "/unigeek/nfc/dictionaries";
  static constexpr uint8_t MAX_DICT_FILES = 16;
  ListItem _dictItems[MAX_DICT_FILES];
  String   _dictFileNames[MAX_DICT_FILES];
  uint8_t  _dictFileCount = 0;

  // Lifecycle / nav helpers
  bool _initModule();
  void _cleanup();
  void _goMain();
  void _goMifare();
  void _goUltralight();
  void _goMagic();

  // Action handlers
  void _showFirmwareInfo();
  void _doScan14A();
  void _doScan15();
  void _doScanLF();
  void _doAuthenticate();
  void _doDumpMemory();
  void _doShowKeys();
  void _doDictionaryPicker();
  void _doDictionaryAttackWithFile(uint8_t fileIndex);
  void _doUltralightDump();
  void _doUltralightWrite();
  void _doDetectGen1a();
  void _doGen3SetUid();
  void _doGen3LockUid();
  void _doEmulate();

  // Helpers
  String _hexUid(const uint8_t* uid, uint8_t len) const;
  String _hexBlock(const uint8_t* data, uint8_t len) const;
  const char* _inferType(uint8_t sak, uint16_t atqa) const;
  bool _scanCardOrShow(uint32_t timeoutMs);
  void _pushRow(const String& label, const String& value);
  void _resetRows();
};
