#pragma once

#include <MFRC522_I2C.h>
#include <array>
#include <unordered_map>
#include "ui/templates/ListScreen.h"
#include "ui/views/ScrollListView.h"
#include "ui/views/LogView.h"
#include "utils/nfc/NFCUtility.h"

class MFRC522Screen : public ListScreen
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
    STATE_SCAN_UID,
    STATE_AUTHENTICATE,
    STATE_MIFARE_CLASSIC,
    STATE_SHOW_KEY,
    STATE_MEMORY_READER,
    STATE_DICT_SELECT,
    STATE_NESTED,
    STATE_STATIC_NESTED,
  };

  State_e _state = STATE_MAIN_MENU;
  MFRC522_I2C* _module = nullptr;
  bool _moduleReady = false;
  TwoWire* _activeBus = nullptr;  // which I2C bus is in use (ExI2C or InI2C)

  MFRC522_I2C::Uid _currentCard = {};
  std::array<std::pair<NFCUtility::MIFARE_Key, NFCUtility::MIFARE_Key>, 40> _mf1AuthKeys;
  std::unordered_map<uint8_t, std::pair<size_t, size_t>> _mf1CardDetails = {
    { MFRC522_I2C::PICC_TYPE_MIFARE_MINI, { 5, 20 } },
    { MFRC522_I2C::PICC_TYPE_MIFARE_1K,   { 16, 64 } },
    { MFRC522_I2C::PICC_TYPE_MIFARE_4K,   { 40, 256 } }
  };

  // Main menu
  ListItem _mainItems[3] = {
    {"Scan UID"},
    {"MIFARE Classic"},
    {"Darkside Attack"},
  };

  // MIFARE Classic submenu
  ListItem _mfItems[5] = {
    {"Discovered Keys"},
    {"Dump Memory"},
    {"Dictionary Attack"},
    {"Static Nested"},
    {"Nested Attack"},
  };

  // ScrollListView for keys/memory display
  ScrollListView _scrollView;
  static constexpr size_t MAX_ROWS = 520;
  ScrollListView::Row _rows[MAX_ROWS];
  String _rowLabels[MAX_ROWS];
  String _rowValues[MAX_ROWS];
  uint16_t _rowCount = 0;

  // Authenticate log view
  LogView _authLog;
  char _authStatus[48] = {};
  int _authPct = 0;
  static void _authStatusBarCb(Sprite& sp, int barY, int width, void* userData);

  // Nested attack log view
  LogView _nestedLog;
  char _nestedStatus[48] = {};
  int _nestedPct = 0;
  static void _nestedStatusBarCb(Sprite& sp, int barY, int width, void* userData);

  void _initModule();
  void _cleanup();
  void _goMainMenu();
  void _goMifareClassic();
  void _goShowDiscoveredKeys();
  void _callScanUid();
  void _callAuthenticate();
  void _callMemoryReader();
  void _callDictionaryAttack();
  void _callDictAttackWithFile(uint8_t fileIndex);
  void _callStaticNested();
  void _callNestedAttack();
  void _callDarksideAttack();
  bool _resetCardState();

  // Dictionary attack file selection
  static constexpr const char* _dictPath = "/unigeek/nfc/dictionaries";
  static constexpr uint8_t MAX_DICT_FILES = 16;
  ListItem _dictItems[MAX_DICT_FILES];
  String _dictFileNames[MAX_DICT_FILES];
  uint8_t _dictFileCount = 0;

  std::string _uidToString(byte* uid, byte len) {
    std::string s;
    for (byte i = 0; i < len; i++) {
      char buf[3];
      sprintf(buf, "%02X", uid[i]);
      s += buf;
    }
    return s;
  }
};