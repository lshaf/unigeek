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
    STATE_NDEF_WRITE_MENU,
    STATE_NDEF_RESULT,
    STATE_NDEF_FILE_SELECT,
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

  ListItem _mainItems[5] = {
    {"Scan ISO14443A"},
    {"MIFARE Classic"},
    {"Ultralight / NTAG"},
    {"Magic Card"},
    {"Firmware Info"},
  };

  ListItem _mfItems[7] = {
    {"Read NDEF"},
    {"Write NDEF"},
    {"Erase NDEF"},
    {"Authenticate"},
    {"Dump Memory"},
    {"Discovered Keys"},
    {"Dictionary Attack"},
  };

  ListItem _ulItems[5] = {
    {"Read NDEF"},
    {"Write NDEF"},
    {"Erase NDEF"},
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

  ListItem _ndefWriteItems[6] = {
    {"Text"},
    {"URL"},
    {"Phone"},
    {"Email"},
    {"vCard"},
    {"Load from File"},
  };


  // MIFARE dump image — filled by _doDumpMemory(), saved by _doSaveDump()
  static constexpr const char* _nfcPath  = "/unigeek/nfc";
  static constexpr const char* _ndefPath = "/unigeek/nfc/ndefs";
  static constexpr const char* _dumpPath = "/unigeek/nfc/dumps";
  uint8_t  _dumpImg[4096] = {};
  size_t   _dumpLen = 0;
  bool     _hasDump = false;

  enum NdefTarget_e {
    NDEF_TARGET_ULTRALIGHT,
    NDEF_TARGET_MIFARE_CLASSIC,
  };
  NdefTarget_e _ndefTarget = NDEF_TARGET_ULTRALIGHT;

  // Raw NDEF message retained after Read NDEF (without the tag-specific TLV wrapper).
  static constexpr size_t MAX_NDEF_BYTES = 254;
  uint8_t  _ndefBuf[MAX_NDEF_BYTES] = {};
  size_t   _ndefLen = 0;
  size_t   _ndefCapacity = 0;
  bool     _hasNdef = false;
  bool     _ndefWritePreview = false;
  bool     _ndefWritePreviewFromFile = false;
  String   _ndefPickDir;

  static constexpr const char* _dictPath = "/unigeek/nfc/dictionaries";
  BrowseFileView _browser;
  String         _dictPickDir;   // current dir in the dict picker

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
  void _doReadNdef();
  void _doReadClassicNdef();
  void _showNdefResult(const uint8_t* uid, uint8_t uidLen,
                       const uint8_t* ndef, size_t ndefLen);
  void _goNdefWrite();
  void _goNdefParent();
  void _doWriteNdefText();
  void _doWriteNdefUrl();
  void _doWriteNdefEmail();
  void _doWriteNdefPhone();
  void _doWriteNdefVcard();
  void _doWriteNdefFromFile();
  void _doWriteNdefFileSelected(uint8_t fileIndex);
  void _showNdefWritePreview(const uint8_t* ndef, size_t ndefLen, bool fromFile);
  void _showNdefActions();
  void _doSaveNdef();
  void _doWriteCurrentNdef();
  void _doEraseNdef();
  void _doEraseClassicNdef();
  bool _writeNdefRecord(const uint8_t* ndef, size_t ndefLen);
  bool _writeUltralightNdefRecord(const uint8_t* ndef, size_t ndefLen);
  bool _writeClassicNdefRecord(const uint8_t* ndef, size_t ndefLen);
  bool _classicNdefSectors(uint8_t* sectors, size_t maxSectors, size_t& count);
  bool _classicAuthSector(uint8_t sector, const uint8_t key[6]);
  bool _classicReadNdefArea(const uint8_t* sectors, size_t sectorCount,
                            uint8_t*& area, size_t& areaLen);
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
  const char* _inferType2Variant();
  bool _scanCardOrShow(uint32_t timeoutMs);
  void _pushRow(const String& label, const String& value);
  void _pushWrappedRow(const String& label, const String& value);
  void _resetRows();
};
