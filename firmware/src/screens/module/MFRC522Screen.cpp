#include "MFRC522Screen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/PinConfigManager.h"
#include "core/AchievementManager.h"
#include "screens/module/ModuleMenuScreen.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"
#include "utils/nfc/StaticNestedAttack.h"
#include "utils/nfc/DarksideAttack.h"

static constexpr uint8_t I2C_ADDRESS = 0x28;

const char* MFRC522Screen::title() {
  switch (_state) {
    case STATE_MAIN_MENU:     return "RC522";
    case STATE_SCAN_UID:      return "Scan UID";
    case STATE_AUTHENTICATE:  return "Authenticate";
    case STATE_MIFARE_CLASSIC: return "MIFARE Classic";
    case STATE_SHOW_KEY:      return "Discovered Keys";
    case STATE_MEMORY_READER: return "Memory Reader";
    case STATE_DICT_SELECT:   return "Dictionary Attack";
  }
  return "M5 RFID 2";
}

void MFRC522Screen::onInit() {
  _initModule();
}

void MFRC522Screen::onUpdate() {
  if (_state == STATE_SCAN_UID) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_PRESS) {
        _callScanUid();
      } else if (dir == INavigation::DIR_BACK) {
        _goMainMenu();
      }
    }
    return;
  }

  if (_state == STATE_SHOW_KEY || _state == STATE_MEMORY_READER) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        _goMifareClassic();
      } else {
        _scrollView.onNav(dir);
      }
    }
    return;
  }

  ListScreen::onUpdate();
}

void MFRC522Screen::onRender() {
  if (_state == STATE_SCAN_UID) return;
  if (_state == STATE_SHOW_KEY || _state == STATE_MEMORY_READER) {
    _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  ListScreen::onRender();
}

void MFRC522Screen::onItemSelected(uint8_t index) {
  if (_state == STATE_MAIN_MENU) {
    switch (index) {
      case 0: _callScanUid();         break;
      case 1: _callAuthenticate();     break;
      case 2: _callDarksideAttack();   break;
    }
  } else if (_state == STATE_MIFARE_CLASSIC) {
    switch (index) {
      case 0: _goShowDiscoveredKeys(); break;
      case 1: _callMemoryReader();     break;
      case 2: _callDictionaryAttack(); break;
      case 3: _callStaticNested();     break;
    }
  } else if (_state == STATE_DICT_SELECT) {
    _callDictAttackWithFile(index);
  }
}

void MFRC522Screen::_cleanup() {
  if (_module) {
    _module->PICC_HaltA();
    _module->PCD_StopCrypto1();
    delete _module;
    _module = nullptr;
  }
  if (_moduleReady && _activeBus && _activeBus != Uni.InI2C) {
    _activeBus->end();
  }
  _activeBus = nullptr;
  _moduleReady = false;
}

void MFRC522Screen::onBack() {
  if (_state == STATE_MAIN_MENU) {
    _cleanup();
    Screen.setScreen(new ModuleMenuScreen());
  } else if (_state == STATE_MIFARE_CLASSIC) {
    _currentCard = {};
    _mf1AuthKeys.fill({});
    _goMainMenu();
  } else if (_state == STATE_SHOW_KEY || _state == STATE_MEMORY_READER || _state == STATE_DICT_SELECT) {
    _goMifareClassic();
  }
}

void MFRC522Screen::_initModule() {
  if (!Uni.ExI2C && !Uni.InI2C) {
    ShowStatusAction::show("No I2C bus available!");
    Screen.setScreen(new ModuleMenuScreen());
    return;
  }

  _activeBus = nullptr;

  // Try external I2C first
  if (Uni.ExI2C) {
    int sda = PinConfig.getInt(PIN_CONFIG_EXT_SDA, PIN_CONFIG_EXT_SDA_DEFAULT);
    int scl = PinConfig.getInt(PIN_CONFIG_EXT_SCL, PIN_CONFIG_EXT_SCL_DEFAULT);

    ProgressView::init();
    ProgressView::progress("Scanning external I2C...", 10);
    Uni.ExI2C->begin(sda, scl);
    Uni.ExI2C->setTimeOut(50);
    delay(100);

    for (int attempt = 0; attempt < 3; attempt++) {
      Uni.ExI2C->beginTransmission(I2C_ADDRESS);
      if (Uni.ExI2C->endTransmission() == 0) {
        _activeBus = Uni.ExI2C;
        break;
      }
      delay(100);
    }

    if (!_activeBus) Uni.ExI2C->end();
  }

  // Fall back to internal I2C
  if (!_activeBus && Uni.InI2C) {
    ProgressView::progress("Scanning internal I2C...", 30);
    Uni.InI2C->setTimeOut(50);
    delay(100);

    for (int attempt = 0; attempt < 3; attempt++) {
      Uni.InI2C->beginTransmission(I2C_ADDRESS);
      if (Uni.InI2C->endTransmission() == 0) {
        _activeBus = Uni.InI2C;
        break;
      }
      delay(100);
    }
  }

  if (!_activeBus) {
    ShowStatusAction::show("Module not found!");
    Screen.setScreen(new ModuleMenuScreen());
    return;
  }

  ProgressView::progress("Starting RC522...", 70);
  if (_module) delete _module;
  _module = new MFRC522_I2C(I2C_ADDRESS, (byte)-1, _activeBus);
  _module->PCD_Init();
  _moduleReady = true;
  delay(200);

  _goMainMenu();
}

void MFRC522Screen::_goMainMenu() {
  _state = STATE_MAIN_MENU;
  setItems(_mainItems);
  render();
}

void MFRC522Screen::_goMifareClassic() {
  _state = STATE_MIFARE_CLASSIC;
  setItems(_mfItems);
  render();
}

void MFRC522Screen::_goShowDiscoveredKeys() {
  _state = STATE_SHOW_KEY;
  _rowCount = 0;

  auto piccType = static_cast<MFRC522_I2C::PICC_Type>(_module->PICC_GetType(_currentCard.sak));
  auto piccName = _module->PICC_GetTypeName(piccType);
  char sakStr[10];
  sprintf(sakStr, "%02X", _currentCard.sak);

  _rowLabels[_rowCount] = "Type";
  _rowValues[_rowCount] = (const char*)piccName;
  _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
  _rowCount++;

  _rowLabels[_rowCount] = "UID";
  _rowValues[_rowCount] = _uidToString(_currentCard.uidByte, _currentCard.size).c_str();
  _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
  _rowCount++;

  _rowLabels[_rowCount] = "SAK";
  _rowValues[_rowCount] = sakStr;
  _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
  _rowCount++;

  auto it = _mf1CardDetails.find(piccType);
  if (it != _mf1CardDetails.end()) {
    for (size_t sector = 0; sector < it->second.first && _rowCount < MAX_ROWS - 1; sector++) {
      _rowLabels[_rowCount] = "S" + String((int)sector) + " A";
      _rowValues[_rowCount] = _mf1AuthKeys[sector].first.c_str().c_str();
      _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
      _rowCount++;

      _rowLabels[_rowCount] = "S" + String((int)sector) + " B";
      _rowValues[_rowCount] = _mf1AuthKeys[sector].second.c_str().c_str();
      _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
      _rowCount++;
    }
  }

  _scrollView.setRows(_rows, _rowCount);
  render();
}

void MFRC522Screen::_callScanUid() {
  _state = STATE_SCAN_UID;

  auto& lcd = Uni.Lcd;
  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.drawString("Scanning ISO14443...", bodyX() + bodyW() / 2, bodyY() + bodyH() / 2);

  bool isFound = false;
  bool cancelled = false;
  const auto start = millis();
  while (millis() - start < 5000) {
    Uni.update();
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) { cancelled = true; break; }
    }
    if (_module->PICC_IsNewCardPresent() && _module->PICC_ReadCardSerial()) {
      isFound = true;
      break;
    }
    delay(50);
  }

  if (cancelled) { _goMainMenu(); return; }

  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);

  if (isFound) {
    int n = Achievement.inc("nfc_uid_first");
    if (n == 1)  Achievement.unlock("nfc_uid_first");
    if (n == 10) Achievement.unlock("nfc_uid_10");

    uint8_t piccType = _module->PICC_GetType(_module->uid.sak);
    const char* piccName = (const char*)_module->PICC_GetTypeName(piccType);
    std::string uid = _uidToString(_module->uid.uidByte, _module->uid.size);

    lcd.setTextSize(1);
    lcd.drawString(piccName, bodyX() + bodyW() / 2, bodyY() + bodyH() / 2 - 24);
    lcd.setTextSize(2);
    lcd.drawString("ISO14443", bodyX() + bodyW() / 2, bodyY() + bodyH() / 2 - 8);
    lcd.drawString(uid.c_str(), bodyX() + bodyW() / 2, bodyY() + bodyH() / 2 + 10);
  } else {
    lcd.setTextSize(2);
    lcd.drawString("No Tag Found", bodyX() + bodyW() / 2, bodyY() + bodyH() / 2 - 4);
  }

  lcd.setTextSize(1);
  #ifdef DEVICE_HAS_KEYBOARD
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.drawString("ENTER: Scan  BACK: Menu", bodyX() + bodyW() / 2, bodyY() + bodyH() - 10);
  #else
    lcd.fillRect(bodyX(), bodyY() + bodyH() - 16, bodyW(), 16, Config.getThemeColor());
    lcd.setTextColor(TFT_WHITE, Config.getThemeColor());
    lcd.drawString("PRESS: Scan", bodyX() + bodyW() / 2, bodyY() + bodyH() - 8);
  #endif
}

bool MFRC522Screen::_resetCardState() {
  byte bufSize = 2;
  byte buf[2];
  _module->PCD_StopCrypto1();
  _module->PICC_HaltA();
  delay(100);
  _module->PICC_WakeupA(buf, &bufSize);
  delay(100);
  return static_cast<MFRC522_I2C::StatusCode>(
    _module->PICC_Select(&(_module->uid), 0)
  ) != MFRC522_I2C::STATUS_TIMEOUT;
}

void MFRC522Screen::_callAuthenticate() {
  _state = STATE_AUTHENTICATE;

  ShowStatusAction::show("Scanning MIFARE Classic...", 0);

  const auto start = millis();
  while (true) {
    Uni.update();
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        _goMainMenu();
        return;
      }
    }
    if (millis() - start > 5000) {
      ShowStatusAction::show("No card found");
      _goMainMenu();
      return;
    }
    if (_module->PICC_IsNewCardPresent() && _module->PICC_ReadCardSerial()) {
      _currentCard.sak = _module->uid.sak;
      _currentCard.size = _module->uid.size;
      memcpy(_currentCard.uidByte, _module->uid.uidByte, _currentCard.size);
      break;
    }
    delay(50);
  }

  auto piccType = static_cast<MFRC522_I2C::PICC_Type>(_module->PICC_GetType(_currentCard.sak));
  auto it = _mf1CardDetails.find(piccType);
  if (it == _mf1CardDetails.end()) {
    ShowStatusAction::show("Unsupported tag");
    _goMainMenu();
    return;
  }

  size_t totalSectors = it->second.first;
  const MFRC522_I2C::PICC_Command keyTypes[] = {
    MFRC522_I2C::PICC_CMD_MF_AUTH_KEY_A,
    MFRC522_I2C::PICC_CMD_MF_AUTH_KEY_B
  };

  _mf1AuthKeys.fill({});

  ProgressView::init();
  bool _keyFoundFired = false;
  for (size_t sector = 0; sector < totalSectors; sector++) {
    for (const auto& keyType : keyTypes) {
      String progress = String((int)sector) + "/" + String((int)(totalSectors - 1));
      String msg = "Auth sector " + progress;
      msg += (keyType == MFRC522_I2C::PICC_CMD_MF_AUTH_KEY_A) ? " key A..." : " key B...";
      int pct = (int)((sector * 2 + (keyType == MFRC522_I2C::PICC_CMD_MF_AUTH_KEY_B ? 1 : 0)) * 100 / (totalSectors * 2));
      ProgressView::progress(msg.c_str(), pct);

      for (const auto& key : NFCUtility::getDefaultKeys()) {
        const auto kv = key.value();
        MFRC522_I2C::MIFARE_Key currentKey = {kv[0], kv[1], kv[2], kv[3], kv[4], kv[5]};
        int blockIndex = (sector < 32) ? (sector * 4 + 3) : (128 + (sector - 32) * 16 + 15);
        auto response = static_cast<MFRC522_I2C::StatusCode>(
          _module->PCD_Authenticate(keyType, blockIndex, &currentKey, &_currentCard));

        if (response == MFRC522_I2C::STATUS_OK) {
          if (keyType == MFRC522_I2C::PICC_CMD_MF_AUTH_KEY_A)
            _mf1AuthKeys[sector].first = key;
          else
            _mf1AuthKeys[sector].second = key;
          if (!_keyFoundFired) {
            _keyFoundFired = true;
            int n = Achievement.inc("nfc_key_found");
            if (n == 1) Achievement.unlock("nfc_key_found");
          }
          break;
        }

        int counter = 0;
        do {
          counter++;
          delay(500);
          if (counter > 5) {
            ShowStatusAction::show("Failed to reset card");
            _goMainMenu();
            return;
          }
        } while (!_resetCardState());
      }
    }
  }

  _goMifareClassic();
}

void MFRC522Screen::_callMemoryReader() {
  _state = STATE_MEMORY_READER;
  _rowCount = 0;

  auto piccType = static_cast<MFRC522_I2C::PICC_Type>(_module->PICC_GetType(_currentCard.sak));
  auto piccName = _module->PICC_GetTypeName(piccType);
  char sakStr[10];
  sprintf(sakStr, "%02X", _currentCard.sak);

  auto it = _mf1CardDetails.find(piccType);
  if (it == _mf1CardDetails.end()) {
    ShowStatusAction::show("Unsupported tag");
    _goMifareClassic();
    return;
  }

  _rowLabels[_rowCount] = "Type";
  _rowValues[_rowCount] = (const char*)piccName;
  _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
  _rowCount++;

  _rowLabels[_rowCount] = "UID";
  _rowValues[_rowCount] = _uidToString(_currentCard.uidByte, _currentCard.size).c_str();
  _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
  _rowCount++;

  _rowLabels[_rowCount] = "SAK";
  _rowValues[_rowCount] = sakStr;
  _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
  _rowCount++;

  size_t totalBlocks = it->second.second;
  int lastValidatedSector = -1;

  ProgressView::init();
  for (size_t block = 0; block < totalBlocks && _rowCount < MAX_ROWS - 1; block++) {
    int pct = (int)(block * 100 / totalBlocks);
    String msg = "Reading block " + String((int)block) + "/" + String((int)(totalBlocks - 1));
    ProgressView::progress(msg.c_str(), pct);

    int currentSector = (block < 128) ? (block / 4) : ((block - 128) / 16 + 32);
    String blockLabel = "Blk " + String((int)block);

    if (!_mf1AuthKeys[currentSector].first && !_mf1AuthKeys[currentSector].second) {
      _rowLabels[_rowCount] = blockLabel + "a";
      _rowValues[_rowCount] = "??:??:??:??:??:??:??:??";
      _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
      _rowCount++;
      if (_rowCount < MAX_ROWS) {
        _rowLabels[_rowCount] = blockLabel + "b";
        _rowValues[_rowCount] = "??:??:??:??:??:??:??:??";
        _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
        _rowCount++;
      }
      continue;
    }

    if (lastValidatedSector != currentSector) {
      lastValidatedSector = currentSector;
      int trailBlock = (currentSector < 32) ? (currentSector * 4 + 3) : (128 + (currentSector - 32) * 16 + 15);
      MFRC522_I2C::MIFARE_Key activeKey = {};
      MFRC522_I2C::PICC_Command activeCmd;
      if (_mf1AuthKeys[currentSector].first) {
        activeCmd = MFRC522_I2C::PICC_CMD_MF_AUTH_KEY_A;
        auto kv = _mf1AuthKeys[currentSector].first.value();
        activeKey = {kv[0], kv[1], kv[2], kv[3], kv[4], kv[5]};
      } else {
        activeCmd = MFRC522_I2C::PICC_CMD_MF_AUTH_KEY_B;
        auto kv = _mf1AuthKeys[currentSector].second.value();
        activeKey = {kv[0], kv[1], kv[2], kv[3], kv[4], kv[5]};
      }

      MFRC522_I2C::StatusCode status;
      int counter = 0;
      do {
        status = static_cast<MFRC522_I2C::StatusCode>(
          _module->PCD_Authenticate(activeCmd, trailBlock, &activeKey, &_currentCard));
        if (status != MFRC522_I2C::STATUS_OK) _resetCardState();
        counter++;
        if (counter > 5) break;
      } while (status != MFRC522_I2C::STATUS_OK);

      if (status != MFRC522_I2C::STATUS_OK) {
        lastValidatedSector = -1;
        _rowLabels[_rowCount] = blockLabel + "a";
        _rowValues[_rowCount] = "??:??:??:??:??:??:??:??";
        _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
        _rowCount++;
        if (_rowCount < MAX_ROWS) {
          _rowLabels[_rowCount] = blockLabel + "b";
          _rowValues[_rowCount] = "??:??:??:??:??:??:??:??";
          _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
          _rowCount++;
        }
        continue;
      }
    }

    byte blockData[18];
    byte blockSize = sizeof(blockData);
    auto readStatus = static_cast<MFRC522_I2C::StatusCode>(
      _module->MIFARE_Read(block, blockData, &blockSize));

    if (readStatus != MFRC522_I2C::STATUS_OK) {
      _rowLabels[_rowCount] = blockLabel + "a";
      _rowValues[_rowCount] = "??:??:??:??:??:??:??:??";
      _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
      _rowCount++;
      if (_rowCount < MAX_ROWS) {
        _rowLabels[_rowCount] = blockLabel + "b";
        _rowValues[_rowCount] = "??:??:??:??:??:??:??:??";
        _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
        _rowCount++;
      }
    } else {
      char buf[26];
      sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
        blockData[0], blockData[1], blockData[2], blockData[3],
        blockData[4], blockData[5], blockData[6], blockData[7]);
      _rowLabels[_rowCount] = blockLabel + "a";
      _rowValues[_rowCount] = buf;
      _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
      _rowCount++;

      if (_rowCount < MAX_ROWS) {
        sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
          blockData[8], blockData[9], blockData[10], blockData[11],
          blockData[12], blockData[13], blockData[14], blockData[15]);
        _rowLabels[_rowCount] = blockLabel + "b";
        _rowValues[_rowCount] = buf;
        _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
        _rowCount++;
      }
    }
  }

  _scrollView.setRows(_rows, _rowCount);
  int nd = Achievement.inc("nfc_dump_memory");
  if (nd == 1) Achievement.unlock("nfc_dump_memory");
  render();
}

void MFRC522Screen::_callDictionaryAttack() {
  _state = STATE_DICT_SELECT;
  _dictFileCount = 0;

  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage not available");
    _goMifareClassic();
    return;
  }

  IStorage::DirEntry entries[MAX_DICT_FILES];
  uint8_t count = Uni.Storage->listDir(_dictPath, entries, MAX_DICT_FILES);
  for (uint8_t i = 0; i < count && _dictFileCount < MAX_DICT_FILES; i++) {
    if (!entries[i].isDir && entries[i].name.endsWith(".txt")) {
      _dictFileNames[_dictFileCount] = entries[i].name;
      _dictItems[_dictFileCount] = {_dictFileNames[_dictFileCount].c_str()};
      _dictFileCount++;
    }
  }

  if (_dictFileCount == 0) {
    ShowStatusAction::show("No dictionary files in nfc/dictionaries/");
    _goMifareClassic();
    return;
  }

  setItems(_dictItems, _dictFileCount);
}

static bool _parseHexKey(const String& line, uint8_t* out) {
  // Parse "FF:FF:FF:FF:FF:FF" or "FFFFFFFFFFFF"
  String s = line;
  s.trim();
  if (s.length() == 0 || s.startsWith("#")) return false;

  // Remove colons
  s.replace(":", "");
  if (s.length() != 12) return false;

  for (int i = 0; i < 6; i++) {
    char hex[3] = { s[i * 2], s[i * 2 + 1], 0 };
    char* end;
    unsigned long val = strtoul(hex, &end, 16);
    if (*end != 0) return false;
    out[i] = (uint8_t)val;
  }
  return true;
}

void MFRC522Screen::_callDictAttackWithFile(uint8_t fileIndex) {
  if (fileIndex >= _dictFileCount) return;

  String filePath = String(_dictPath) + "/" + _dictFileNames[fileIndex];
  String content = Uni.Storage->readFile(filePath.c_str());
  if (content.length() == 0) {
    ShowStatusAction::show("Empty dictionary file");
    render();
    return;
  }

  // Parse keys from file
  static constexpr uint8_t MAX_KEYS = 128;
  uint8_t keys[MAX_KEYS][6];
  uint8_t keyCount = 0;

  int start = 0;
  while (start < (int)content.length() && keyCount < MAX_KEYS) {
    int nl = content.indexOf('\n', start);
    if (nl < 0) nl = content.length();
    String line = content.substring(start, nl);
    if (_parseHexKey(line, keys[keyCount])) keyCount++;
    start = nl + 1;
  }

  if (keyCount == 0) {
    ShowStatusAction::show("No valid keys found");
    render();
    return;
  }

  auto piccType = static_cast<MFRC522_I2C::PICC_Type>(_module->PICC_GetType(_currentCard.sak));
  auto it = _mf1CardDetails.find(piccType);
  if (it == _mf1CardDetails.end()) {
    ShowStatusAction::show("Unsupported tag");
    _goMifareClassic();
    return;
  }

  size_t totalSectors = it->second.first;
  int recovered = 0;
  int uncovered = 0;

  // Count uncovered keys first
  for (size_t s = 0; s < totalSectors; s++) {
    if (!_mf1AuthKeys[s].first) uncovered++;
    if (!_mf1AuthKeys[s].second) uncovered++;
  }

  if (uncovered == 0) {
    ShowStatusAction::show("All keys already found");
    _goMifareClassic();
    return;
  }

  int progress = 0;
  int totalWork = uncovered;

  const MFRC522_I2C::PICC_Command keyTypes[] = {
    MFRC522_I2C::PICC_CMD_MF_AUTH_KEY_A,
    MFRC522_I2C::PICC_CMD_MF_AUTH_KEY_B
  };

  ProgressView::init();
  for (size_t sector = 0; sector < totalSectors; sector++) {
    for (const auto& keyType : keyTypes) {
      bool isKeyA = (keyType == MFRC522_I2C::PICC_CMD_MF_AUTH_KEY_A);
      auto& slot = isKeyA ? _mf1AuthKeys[sector].first : _mf1AuthKeys[sector].second;
      if (slot) continue; // already discovered

      int pct = totalWork > 0 ? (progress * 100 / totalWork) : 0;
      char msg[48];
      snprintf(msg, sizeof(msg), "Dict S%d key %c (%d keys)",
        (int)sector, isKeyA ? 'A' : 'B', keyCount);
      ProgressView::progress(msg, pct);

      int blockIndex = (sector < 32)
        ? ((int)sector * 4 + 3)
        : (128 + ((int)sector - 32) * 16 + 15);

      for (uint8_t k = 0; k < keyCount; k++) {
        MFRC522_I2C::MIFARE_Key mfKey = {
          keys[k][0], keys[k][1], keys[k][2],
          keys[k][3], keys[k][4], keys[k][5]
        };

        auto response = static_cast<MFRC522_I2C::StatusCode>(
          _module->PCD_Authenticate(keyType, blockIndex, &mfKey, &_currentCard));

        if (response == MFRC522_I2C::STATUS_OK) {
          slot = NFCUtility::MIFARE_Key(
            keys[k][0], keys[k][1], keys[k][2],
            keys[k][3], keys[k][4], keys[k][5]);
          recovered++;
          break;
        }

        // Reset card state for next attempt
        int counter = 0;
        do {
          counter++;
          delay(100);
          if (counter > 5) {
            char err[48];
            snprintf(err, sizeof(err), "Card reset failed\nRecovered %d keys", recovered);
            ShowStatusAction::show(err);
            _goMifareClassic();
            return;
          }
        } while (!_resetCardState());
      }

      progress++;
    }
  }

  if (recovered > 0) {
    int n = Achievement.inc("nfc_dict_attack");
    if (n == 1) Achievement.unlock("nfc_dict_attack");
  }
  char msg[48];
  snprintf(msg, sizeof(msg), "Recovered %d keys", recovered);
  ShowStatusAction::show(msg);
  _goMifareClassic();
}

void MFRC522Screen::_callStaticNested() {
  auto piccType = static_cast<MFRC522_I2C::PICC_Type>(_module->PICC_GetType(_currentCard.sak));
  auto it = _mf1CardDetails.find(piccType);
  if (it == _mf1CardDetails.end()) {
    ShowStatusAction::show("Unsupported tag");
    render();
    return;
  }

  size_t totalSectors = it->second.first;

  // Find exploit sector: first sector with a known key
  int exploitSector = -1;
  uint64_t exploitKey = 0;
  uint8_t exploitCmd = MFRC522_I2C::PICC_CMD_MF_AUTH_KEY_A;
  for (size_t s = 0; s < totalSectors; s++) {
    if (_mf1AuthKeys[s].first) {
      exploitSector = (int)s;
      exploitCmd = MFRC522_I2C::PICC_CMD_MF_AUTH_KEY_A;
      auto kv = _mf1AuthKeys[s].first.value();
      for (int b = 0; b < 6; b++)
        exploitKey = (exploitKey << 8) | kv[b];
      break;
    }
    if (_mf1AuthKeys[s].second) {
      exploitSector = (int)s;
      exploitCmd = MFRC522_I2C::PICC_CMD_MF_AUTH_KEY_B;
      auto kv = _mf1AuthKeys[s].second.value();
      for (int b = 0; b < 6; b++)
        exploitKey = (exploitKey << 8) | kv[b];
      break;
    }
  }

  if (exploitSector < 0) {
    ShowStatusAction::show("Need at least one known key first!");
    render();
    return;
  }

  uint32_t uid = 0;
  for (int i = 0; i < 4; i++)
    uid = (uid << 8) | _currentCard.uidByte[i];

  int exploitTrailer = (exploitSector < 32)
    ? (exploitSector * 4 + 3)
    : (128 + (exploitSector - 32) * 16 + 15);

  // Check if card has static nonce
  ProgressView::init();
  ProgressView::progress("Checking static nonce...", 5);
  if (!StaticNestedAttack::isStaticNonce(_module, uid, exploitCmd, exploitTrailer, exploitKey)) {
    _module->PCD_Init();
    ShowStatusAction::show("Card does not have static nonce");
    _goMifareClassic();
    return;
  }

  int recovered = 0;

  for (size_t s = 0; s < totalSectors; s++) {
    if (_mf1AuthKeys[s].first && _mf1AuthKeys[s].second)
      continue;

    int targetTrailer = (s < 32) ? ((int)s * 4 + 3) : (128 + ((int)s - 32) * 16 + 15);

    // Attack missing key A
    if (!_mf1AuthKeys[s].first) {
      char msg[48];
      snprintf(msg, sizeof(msg), "Static nested S%d A...", (int)s);
      ProgressView::progress(msg, (int)(s * 100 / totalSectors));

      auto result = StaticNestedAttack::crack(
        _module, uid, exploitCmd, exploitTrailer, exploitKey,
        MFRC522_I2C::PICC_CMD_MF_AUTH_KEY_A, targetTrailer,
        [](const char* m, int pct) -> bool {
          ProgressView::progress(m, pct);
          return true;
        });

      if (result.success) {
        uint8_t kb[6];
        uint64_t k = result.key;
        for (int i = 5; i >= 0; i--) { kb[i] = (uint8_t)(k & 0xFF); k >>= 8; }
        _mf1AuthKeys[s].first = NFCUtility::MIFARE_Key(kb[0], kb[1], kb[2], kb[3], kb[4], kb[5]);
        recovered++;
      }
    }

    // Attack missing key B
    if (!_mf1AuthKeys[s].second) {
      char msg[48];
      snprintf(msg, sizeof(msg), "Static nested S%d B...", (int)s);
      ProgressView::progress(msg, (int)(s * 100 / totalSectors));

      auto result = StaticNestedAttack::crack(
        _module, uid, exploitCmd, exploitTrailer, exploitKey,
        MFRC522_I2C::PICC_CMD_MF_AUTH_KEY_B, targetTrailer,
        [](const char* m, int pct) -> bool {
          ProgressView::progress(m, pct);
          return true;
        });

      if (result.success) {
        uint8_t kb[6];
        uint64_t k = result.key;
        for (int i = 5; i >= 0; i--) { kb[i] = (uint8_t)(k & 0xFF); k >>= 8; }
        _mf1AuthKeys[s].second = NFCUtility::MIFARE_Key(kb[0], kb[1], kb[2], kb[3], kb[4], kb[5]);
        recovered++;
      }
    }
  }

  _module->PCD_Init();

  if (recovered > 0) {
    int n = Achievement.inc("nfc_static_nested");
    if (n == 1) Achievement.unlock("nfc_static_nested");
  }
  char msg[48];
  snprintf(msg, sizeof(msg), "Recovered %d keys", recovered);
  ShowStatusAction::show(msg);
  _goMifareClassic();
}

void MFRC522Screen::_callDarksideAttack() {
  // Scan for card if called from main menu (no card scanned yet)
  if (_state == STATE_MAIN_MENU) {
    ShowStatusAction::show("Place card on reader...", 0);

    const auto scanStart = millis();
    bool found = false;
    while (millis() - scanStart < 5000) {
      Uni.update();
      if (Uni.Nav->wasPressed()) {
        auto dir = Uni.Nav->readDirection();
        if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
          _goMainMenu();
          return;
        }
      }
      if (_module->PICC_IsNewCardPresent() && _module->PICC_ReadCardSerial()) {
        _currentCard.sak = _module->uid.sak;
        _currentCard.size = _module->uid.size;
        memcpy(_currentCard.uidByte, _module->uid.uidByte, _currentCard.size);
        _mf1AuthKeys.fill({});
        found = true;
        break;
      }
      delay(50);
    }
    if (!found) {
      ShowStatusAction::show("No card found");
      _goMainMenu();
      return;
    }
  }

  auto piccType = static_cast<MFRC522_I2C::PICC_Type>(_module->PICC_GetType(_currentCard.sak));
  auto it = _mf1CardDetails.find(piccType);
  if (it == _mf1CardDetails.end()) {
    ShowStatusAction::show("Unsupported tag");
    render();
    return;
  }

  uint32_t uid = 0;
  for (int i = 0; i < 4; i++)
    uid = (uid << 8) | _currentCard.uidByte[i];

  // Attack sector 0 key A (trailer block 3)
  ProgressView::init();
  ProgressView::progress("Darkside attack...", 0);

  auto result = DarksideAttack::crack(
    _module, uid, MFRC522_I2C::PICC_CMD_MF_AUTH_KEY_A, 3,
    Uni.Storage,
    [](const char* m, int pct) -> bool {
      ProgressView::progress(m, pct);
      return true;
    });

  _module->PCD_Init();

  if (result.success) {
    uint8_t kb[6];
    uint64_t k = result.key;
    for (int i = 5; i >= 0; i--) { kb[i] = (uint8_t)(k & 0xFF); k >>= 8; }
    _mf1AuthKeys[0].first = NFCUtility::MIFARE_Key(kb[0], kb[1], kb[2], kb[3], kb[4], kb[5]);

    int n = Achievement.inc("nfc_darkside");
    if (n == 1) Achievement.unlock("nfc_darkside");

    char msg[48];
    snprintf(msg, sizeof(msg), "Found key A S0:\n%02X%02X%02X%02X%02X%02X",
      kb[0], kb[1], kb[2], kb[3], kb[4], kb[5]);
    ShowStatusAction::show(msg);
    _goMifareClassic();
  } else {
    ShowStatusAction::show("Darkside attack failed");
    _goMainMenu();
  }
}