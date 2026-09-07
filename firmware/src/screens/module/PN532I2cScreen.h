#pragma once

#include <array>
#include <Adafruit_PN532.h>
#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"
#include "ui/views/ScrollListView.h"
#include "utils/nfc/NFCUtility.h"
#include "utils/nfc/MagicCard.h"

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
    STATE_MIFARE_TAG_MENU,
    STATE_MIFARE_NDEF_MENU,
    STATE_MIFARE_ATTACKS_MENU,
    STATE_MIFARE_KEYS_MENU,
    STATE_MIFARE_KEY_DB_SELECT,
    STATE_MIFARE_KEY_DB_VIEW,
    STATE_MIFARE_DUMP,
    STATE_MIFARE_DUMP_HEX,
    STATE_MIFARE_KEYS,
    STATE_MIFARE_DUMP_SELECT,
    STATE_DICT_SELECT,
    STATE_ULTRALIGHT_MENU,
    STATE_ULTRALIGHT_TAG_MENU,
    STATE_ULTRALIGHT_NDEF_MENU,
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
    {"Scan Tag"},
    {"MIFARE Classic"},
    {"Ultralight / NTAG"},
    {"Magic Card"},
    {"Firmware Info"},
  };

  ListItem _mfItems[4] = {
    {"Tag Operations"},
    {"NDEF Operations"},
    {"Attacks"},
    {"Keys"},
  };

  ListItem _mfAttackItems[1] = {
    {"Dictionary Attack"},
  };

  ListItem _mfKeysItems[2] = {
    {"Discovered Keys"},
    {"Key Databases"},
  };

  ListItem _mfTagItems[3] = {
    {"Read Tag"},
    {"Write to Tag"},
    {"Erase Tag"},
  };

  ListItem _mfNdefItems[4] = {
    {"Read NDEF"},
    {"Write NDEF"},
    {"Erase NDEF"},
    {"Format NDEF"},
  };

  ListItem _ulItems[2] = {
    {"Tag Operations"},
    {"NDEF Operations"},
  };

  ListItem _ulTagItems[2] = {
    {"Read Pages"},
    {"Write Page"},
  };

  ListItem _ulNdefItems[3] = {
    {"Read NDEF"},
    {"Write NDEF"},
    {"Erase NDEF"},
  };

  ListItem _magicItems[3] = {
    {"Detect Magic"},
    {"Set UID (Gen3)"},
    {"Lock UID (Gen3)"},
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
  bool     _dumpComplete = false;
  bool     _resumeReadAfterDict = false;
  String   _dumpPickDir;

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
  String         _keyDbPickDir;  // current dir in the key database browser
  String         _keyDbViewTitle;

  bool _initModule();
  void _cleanup();
  void _goMain();
  void _goMifare();
  void _goMifareTag();
  void _goMifareNdef();
  void _goMifareAttacks();
  void _goMifareKeys();
  void _goScan14A();
  void _openKeyDatabases();
  void _openKeyDatabase(uint8_t index);
  void _goUltralight();
  void _goUltralightTag();
  void _goUltralightNdef();
  void _goMagic();
  void _doNtagMenu();

  void _showFirmwareInfo();
  void _doScan14A();
  void _doAuthenticate();
  bool _discoverDefaultKeys(bool checkingProgress = false);
  void _loadSavedKeys();
  void _saveKeys();
  bool _hasReadableKeyForEverySector() const;
  void _doReadTag();
  void _doDumpMemory();
  void _showTagDetails();
  void _appendDumpNdefDetails();
  void _showDumpHex();
  void _showDumpActions();
  void _doWriteDumpToTag(const uint8_t* dump, size_t len,
                         const uint8_t* sourceUid = nullptr, uint8_t sourceUidLen = 0);
  bool _tryWriteMifareBlock(uint16_t block, const uint8_t data[16],
                            const uint8_t key[6], bool useKeyB);
  void _doWriteDumpFromFilePicker();
  void _doWriteDumpFileSelected(uint8_t fileIndex);
  void _doEraseTag();
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
  bool _formatClassic1kNdef();
  bool _classicNdefSectors(uint8_t* sectors, size_t maxSectors, size_t& count);
  bool _classicAuthSector(uint8_t sector, const uint8_t key[6]);
  bool _classicReadNdefArea(const uint8_t* sectors, size_t sectorCount,
                            uint8_t*& area, size_t& areaLen);
  MagicCardType _detectMagicType();
  bool _writeMagicUid(MagicCardType type, const uint8_t* sourceUid,
                      uint8_t sourceUidLen, const uint8_t block0[16]);
  bool _resetAndReselect();
  void _doDetectMagic();
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
