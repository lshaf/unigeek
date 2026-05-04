#pragma once

#include <array>
#include <Adafruit_PN532.h>
#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"
#include "ui/views/ScrollListView.h"
#include "utils/nfc/NFCUtility.h"

class PN532I2cScreen : public ListScreen
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
    STATE_MIFARE_MENU,
    STATE_MIFARE_DUMP,
    STATE_MIFARE_KEYS,
    STATE_DICT_SELECT,
    STATE_ULTRALIGHT_MENU,
    STATE_MAGIC_MENU,
    STATE_RAW_RESULT,
    STATE_EMULATE,
    STATE_NTAG_MENU,
  };

  State_e      _state    = STATE_MAIN_MENU;
  Adafruit_PN532* _nfc   = nullptr;
  TwoWire*     _wire     = nullptr;
  const char*  _busName  = nullptr;
  bool         _ready    = false;

  // Last scanned 14A card
  uint8_t  _uid[7] = {};
  uint8_t  _uidLen = 0;
  uint16_t _atqa   = 0;
  uint8_t  _sak    = 0;
  bool     _hasCard = false;
  std::array<std::pair<NFCUtility::MIFARE_Key, NFCUtility::MIFARE_Key>, 40> _mfKeys;

  // Firmware info
  uint8_t _fwIc = 0, _fwVer = 0, _fwRev = 0, _fwSup = 0;

  // Card type helpers
  std::pair<size_t, size_t> _mfDims(uint8_t sak) const;

  // Scroll view for info / dump / keys / raw
  ScrollListView _scrollView;
  static constexpr size_t MAX_ROWS = 520;
  ScrollListView::Row _rows[MAX_ROWS];
  String _rowLabels[MAX_ROWS];
  String _rowValues[MAX_ROWS];
  uint16_t _rowCount = 0;

  ListItem _mainItems[6] = {
    {"Scan ISO14443A"},
    {"MIFARE Classic"},
    {"MIFARE Ultralight"},
    {"Magic Card"},
    {"Firmware Info"},
    {"NTAG Emulate"},
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

  ListItem _ntagItems[2] = {
    {"Text Record"},
    {"URL Record"},
  };

  // MIFARE dump image — filled by _doDumpMemory(), saved by _doSaveDump()
  static constexpr const char* _dumpPath = "/unigeek/nfc/dumps";
  uint8_t  _dumpImg[1024] = {};
  bool     _hasDump = false;

  static constexpr const char* _dictPath = "/unigeek/nfc/dictionaries";
  BrowseFileView _browser;

  bool _initModule();
  void _cleanup();
  void _goMain();
  void _goMifare();
  void _goUltralight();
  void _goMagic();
  void _doNtagMenu();

  void _showFirmwareInfo();
  void _doScan14A();
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
  void _doSaveDump();
  void _doNtagText();
  void _doNtagUrl();
  void _emulateLoop(const uint8_t* nfcid1, const uint8_t* ndef, uint16_t ndefLen);

  String _hexUid(const uint8_t* uid, uint8_t len) const;
  String _hexBlock(const uint8_t* data, uint8_t len) const;
  const char* _inferType(uint8_t sak, uint16_t atqa) const;
  bool _scanCardOrShow(uint32_t timeoutMs);
  void _pushRow(const String& label, const String& value);
  void _resetRows();
};
