#include "PN532I2cScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/PinConfigManager.h"
#include "core/AchievementManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/views/ProgressView.h"

// ── raw I2C helpers for Gen1a / Gen3 ──────────────────────────────────────
// Adafruit_PN532 exposes sendCommandCheckAck() publicly but readdata() is
// private. These helpers build commands in pn532_packetbuffer (the library's
// global) and read the response directly over Wire after the ACK is received.

extern byte pn532_packetbuffer[];

static void _nfcReadI2C(TwoWire* wire, uint8_t* buf, uint8_t n) {
  uint8_t total = n + 1;
  if (wire->requestFrom((uint8_t)PN532_I2C_ADDRESS, total) != total) return;
  wire->read(); // status byte
  for (uint8_t i = 0; i < n; i++)
    buf[i] = wire->available() ? wire->read() : 0;
}

static bool _nfcWriteReg(Adafruit_PN532* nfc, TwoWire* wire, uint16_t reg, uint8_t val) {
  pn532_packetbuffer[0] = PN532_COMMAND_WRITEREGISTER;
  pn532_packetbuffer[1] = (reg >> 8) & 0xFF;
  pn532_packetbuffer[2] = reg & 0xFF;
  pn532_packetbuffer[3] = val;
  if (!nfc->sendCommandCheckAck(pn532_packetbuffer, 4, 200)) return false;
  uint8_t resp[10];
  _nfcReadI2C(wire, resp, 10);
  return resp[5] == PN532_PN532TOHOST && resp[6] == (PN532_COMMAND_WRITEREGISTER + 1);
}

static bool _nfcCommThru(Adafruit_PN532* nfc, TwoWire* wire,
                          const uint8_t* data, uint8_t dlen,
                          uint8_t* resp, uint8_t& rlen,
                          uint16_t timeoutMs = 300) {
  pn532_packetbuffer[0] = PN532_COMMAND_INCOMMUNICATETHRU;
  for (uint8_t i = 0; i < dlen; i++) pn532_packetbuffer[1 + i] = data[i];
  if (!nfc->sendCommandCheckAck(pn532_packetbuffer, 1 + dlen, timeoutMs)) return false;
  uint8_t buf[20];
  _nfcReadI2C(wire, buf, 20);
  if (buf[5] != PN532_PN532TOHOST || buf[6] != (PN532_COMMAND_INCOMMUNICATETHRU + 1)) return false;
  if (buf[7] & 0x3F) return false;
  uint8_t payLen = buf[3] > 3 ? buf[3] - 3 : 0;
  rlen = payLen < rlen ? payLen : rlen;
  if (rlen > 0) memcpy(resp, buf + 8, rlen);
  return true;
}

static bool _nfcDataExch(Adafruit_PN532* nfc, TwoWire* wire,
                          const uint8_t* data, uint8_t dlen,
                          uint8_t* resp, uint8_t& rlen,
                          uint16_t timeoutMs = 1000) {
  pn532_packetbuffer[0] = PN532_COMMAND_INDATAEXCHANGE;
  pn532_packetbuffer[1] = 1; // Tg
  for (uint8_t i = 0; i < dlen; i++) pn532_packetbuffer[2 + i] = data[i];
  if (!nfc->sendCommandCheckAck(pn532_packetbuffer, 2 + dlen, timeoutMs)) return false;
  uint8_t buf[24];
  _nfcReadI2C(wire, buf, 24);
  if (buf[5] != PN532_PN532TOHOST || buf[6] != PN532_RESPONSE_INDATAEXCHANGE) return false;
  if (buf[7] & 0x3F) return false;
  uint8_t payLen = buf[3] > 3 ? buf[3] - 3 : 0;
  rlen = payLen < rlen ? payLen : rlen;
  if (rlen > 0) memcpy(resp, buf + 8, rlen);
  return true;
}

// ── title ──────────────────────────────────────────────────────────────────

const char* PN532I2cScreen::title() {
  switch (_state) {
    case STATE_MAIN_MENU:       return "PN532 I2C";
    case STATE_INFO:            return "Firmware Info";
    case STATE_SCAN_RESULT:     return "Scan Result";
    case STATE_SCAN_14A:        return "Scan ISO14443A";
    case STATE_MIFARE_MENU:     return "MIFARE Classic";
    case STATE_MIFARE_DUMP:     return "Memory Dump";
    case STATE_MIFARE_KEYS:     return "Discovered Keys";
    case STATE_DICT_SELECT:     return "Dictionary Attack";
    case STATE_ULTRALIGHT_MENU: return "Ultralight / NTAG";
    case STATE_MAGIC_MENU:      return "Magic Card";
    case STATE_RAW_RESULT:      return "Result";
    case STATE_EMULATE:         return "Emulate Card";
    case STATE_NTAG_MENU:       return "NTAG Emulate";
  }
  return "PN532 I2C";
}

// ── lifecycle ──────────────────────────────────────────────────────────────

void PN532I2cScreen::onInit() {
  if (!_initModule()) return;
  _goMain();
}

void PN532I2cScreen::onUpdate() {
  if (_state == STATE_SCAN_RESULT) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        _goMain();
      } else if (dir == INavigation::DIR_PRESS) {
        _doScan14A();
      } else {
        _scrollView.onNav(dir);
      }
    }
    return;
  }
  if (_state == STATE_MIFARE_DUMP) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        _hasDump = false;
        _goMifare();
      } else if (dir == INavigation::DIR_PRESS && _hasDump) {
        _doSaveDump();
      } else {
        _scrollView.onNav(dir);
      }
    }
    return;
  }
  if (_state == STATE_INFO || _state == STATE_MIFARE_KEYS || _state == STATE_RAW_RESULT) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        if (_state == STATE_MIFARE_KEYS) _goMifare();
        else _goMain();
      } else {
        _scrollView.onNav(dir);
      }
    }
    return;
  }
  ListScreen::onUpdate();
}

void PN532I2cScreen::onRender() {
  if (_state == STATE_INFO || _state == STATE_SCAN_RESULT ||
      _state == STATE_MIFARE_DUMP || _state == STATE_MIFARE_KEYS ||
      _state == STATE_RAW_RESULT || _state == STATE_EMULATE) {
    _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  ListScreen::onRender();
}

void PN532I2cScreen::onItemSelected(uint8_t index) {
  switch (_state) {
    case STATE_MAIN_MENU:
      switch (index) {
        case 0: _doScan14A();        break;
        case 1: _goMifare();         break;
        case 2: _goUltralight();     break;
        case 3: _goMagic();          break;
        case 4: _showFirmwareInfo(); break;
        case 5: _doNtagMenu();       break;
      }
      break;
    case STATE_MIFARE_MENU:
      switch (index) {
        case 0: _doAuthenticate();     break;
        case 1: _doDumpMemory();       break;
        case 2: _doShowKeys();         break;
        case 3: _doDictionaryPicker(); break;
      }
      break;
    case STATE_ULTRALIGHT_MENU:
      switch (index) {
        case 0: _doUltralightDump();  break;
        case 1: _doUltralightWrite(); break;
      }
      break;
    case STATE_MAGIC_MENU:
      switch (index) {
        case 0: _doDetectGen1a(); break;
        case 1: _doGen3SetUid();  break;
        case 2: _doGen3LockUid(); break;
      }
      break;
    case STATE_DICT_SELECT:
      _doDictionaryAttackWithFile(index);
      break;
    case STATE_NTAG_MENU:
      if (index == 0) _doNtagText();
      else if (index == 1) _doNtagUrl();
      break;
    default: break;
  }
}

void PN532I2cScreen::onBack() {
  switch (_state) {
    case STATE_MAIN_MENU:
      _cleanup();
      Screen.goBack();
      break;
    case STATE_MIFARE_MENU:
    case STATE_ULTRALIGHT_MENU:
    case STATE_MAGIC_MENU:
      _goMain();
      break;
    case STATE_DICT_SELECT:
      _goMifare();
      break;
    case STATE_NTAG_MENU:
      _goMain();
      break;
    default:
      _goMain();
      break;
  }
}

// ── init / cleanup ─────────────────────────────────────────────────────────

bool PN532I2cScreen::_initModule() {
  ProgressView::init();
  ProgressView::progress("Probing PN532 I2C...", 10);

  // ExI2C is already initialised by Device — just probe it
  if (Uni.ExI2C) {
    _nfc = new Adafruit_PN532(255, 255, Uni.ExI2C);
    _nfc->begin(); // begin(false) internally — no Wire.begin()
    ProgressView::progress("Probing ExI2C...", 35);
    uint32_t fw = _nfc->getFirmwareVersion();
    if (fw) {
      _fwIc  = (fw >> 24) & 0xFF;
      _fwVer = (fw >> 16) & 0xFF;
      _fwRev = (fw >> 8)  & 0xFF;
      _fwSup =  fw        & 0xFF;
      _wire    = Uni.ExI2C;
      _busName = "ExI2C";
      _ready   = true;
      int n = Achievement.inc("pn532_i2c_first_use");
      if (n == 1) Achievement.unlock("pn532_i2c_first_use");
      return true;
    }
    delete _nfc; _nfc = nullptr;
  }

  // Fall back to InI2C
  if (Uni.InI2C) {
    _nfc = new Adafruit_PN532(255, 255, Uni.InI2C);
    _nfc->begin();
    ProgressView::progress("Probing InI2C...", 35);
    uint32_t fw = _nfc->getFirmwareVersion();
    if (fw) {
      _fwIc  = (fw >> 24) & 0xFF;
      _fwVer = (fw >> 16) & 0xFF;
      _fwRev = (fw >> 8)  & 0xFF;
      _fwSup =  fw        & 0xFF;
      _wire    = Uni.InI2C;
      _busName = "InI2C";
      _ready   = true;
      int n = Achievement.inc("pn532_i2c_first_use");
      if (n == 1) Achievement.unlock("pn532_i2c_first_use");
      return true;
    }
    delete _nfc; _nfc = nullptr;
  }

  ShowStatusAction::show("PN532 I2C not found");
  Screen.goBack();
  return false;
}

void PN532I2cScreen::_cleanup() {
  delete _nfc; _nfc = nullptr;
  _wire    = nullptr;
  _busName = nullptr;
  _ready   = false;
  _hasCard = false;
  _mfKeys.fill({});
}

// ── nav helpers ────────────────────────────────────────────────────────────

void PN532I2cScreen::_goMain() {
  _state = STATE_MAIN_MENU;
  setItems(_mainItems, 6);
  render();
}

void PN532I2cScreen::_goMifare() {
  _state = STATE_MIFARE_MENU;
  setItems(_mfItems);
  render();
}

void PN532I2cScreen::_goUltralight() {
  _state = STATE_ULTRALIGHT_MENU;
  setItems(_ulItems);
  render();
}

void PN532I2cScreen::_goMagic() {
  _state = STATE_MAGIC_MENU;
  setItems(_magicItems);
  render();
}

// ── display helpers ────────────────────────────────────────────────────────

String PN532I2cScreen::_hexUid(const uint8_t* uid, uint8_t len) const {
  String s;
  for (uint8_t i = 0; i < len; i++) {
    char buf[4];
    sprintf(buf, "%s%02X", i == 0 ? "" : ":", uid[i]);
    s += buf;
  }
  return s;
}

String PN532I2cScreen::_hexBlock(const uint8_t* data, uint8_t len) const {
  String s;
  for (uint8_t i = 0; i < len; i++) {
    char buf[4];
    sprintf(buf, "%s%02X", i == 0 ? "" : " ", data[i]);
    s += buf;
  }
  return s;
}

void PN532I2cScreen::_resetRows() { _rowCount = 0; }

void PN532I2cScreen::_pushRow(const String& label, const String& value) {
  if (_rowCount >= MAX_ROWS) return;
  _rowLabels[_rowCount] = label;
  _rowValues[_rowCount] = value;
  _rows[_rowCount] = { _rowLabels[_rowCount].c_str(), _rowValues[_rowCount] };
  _rowCount++;
}

std::pair<size_t, size_t> PN532I2cScreen::_mfDims(uint8_t sak) const {
  if (sak == 0x09) return {5,  20};
  if (sak == 0x08) return {16, 64};
  if (sak == 0x18) return {40, 256};
  return {0, 0};
}

const char* PN532I2cScreen::_inferType(uint8_t sak, uint16_t atqa) const {
  uint8_t atqaHi = (atqa >> 8) & 0xFF;
  if (sak == 0x09) return "MF Classic Mini";
  if (sak == 0x08) return "MF Classic 1K";
  if (sak == 0x18) return "MF Classic 4K";
  if (sak == 0x28) return "MF Plus / SmartMX";
  if (sak == 0x20) return atqaHi == 0x03 ? "MIFARE DESFire" : "ISO14443-4";
  if (sak == 0x00) return (atqa & 0x00FF) == 0x44 ? "MIFARE UL / NTAG" : "ISO14443A T2";
  return "ISO14443A";
}

// ── scan helper ────────────────────────────────────────────────────────────

bool PN532I2cScreen::_scanCardOrShow(uint32_t timeoutMs) {
  ShowStatusAction::show("Place card on reader...", 0);
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    Uni.update();
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) return false;
    }
    uint8_t uid[7]; uint8_t uidLen;
    if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 200)) {
      memcpy(_uid, uid, uidLen);
      _uidLen  = uidLen;
      _atqa    = ((uint16_t)pn532_packetbuffer[9] << 8) | pn532_packetbuffer[10];
      _sak     = pn532_packetbuffer[11];
      _hasCard = true;
      _mfKeys.fill({});
      return true;
    }
    delay(50);
  }
  ShowStatusAction::show("No card found");
  return false;
}

// ── actions ────────────────────────────────────────────────────────────────

void PN532I2cScreen::_showFirmwareInfo() {
  _state = STATE_INFO;
  _resetRows();
  char buf[16];
  sprintf(buf, "0x%02X", _fwIc);
  _pushRow("IC", buf);
  sprintf(buf, "%u.%u", _fwVer, _fwRev);
  _pushRow("Version", buf);
  sprintf(buf, "0x%02X", _fwSup);
  _pushRow("Support", buf);
  _pushRow("Transport", "I2C (0x24)");
  if (_busName) _pushRow("Bus", _busName);
  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532I2cScreen::_doScan14A() {
  _state = STATE_SCAN_14A;
  ShowStatusAction::show("Scanning 14A...", 0);
  bool ok = false;
  uint32_t start = millis();
  while (millis() - start < 5000) {
    Uni.update();
    if (Uni.Nav->wasPressed()) {
      if (Uni.Nav->readDirection() == INavigation::DIR_BACK) { _goMain(); return; }
    }
    uint8_t uid[7]; uint8_t uidLen;
    if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 200)) {
      memcpy(_uid, uid, uidLen);
      _uidLen  = uidLen;
      _atqa    = ((uint16_t)pn532_packetbuffer[9] << 8) | pn532_packetbuffer[10];
      _sak     = pn532_packetbuffer[11];
      _hasCard = true;
      _mfKeys.fill({});
      ok = true;
      break;
    }
    delay(50);
  }
  if (!ok) { ShowStatusAction::show("No card"); _goMain(); return; }

  int n = Achievement.inc("nfc_uid_first");
  if (n == 1)  Achievement.unlock("nfc_uid_first");
  if (n == 10) Achievement.unlock("nfc_uid_10");

  _state = STATE_SCAN_RESULT;
  _resetRows();
  char buf[24];
  _pushRow("UID",  _hexUid(_uid, _uidLen));
  _pushRow("Type", _inferType(_sak, _atqa));
  snprintf(buf, sizeof(buf), "%02X:%02X", (_atqa >> 8) & 0xFF, _atqa & 0xFF);
  _pushRow("ATQA", buf);
  snprintf(buf, sizeof(buf), "%02X", _sak);
  _pushRow("SAK", buf);
  snprintf(buf, sizeof(buf), "%d bytes", _uidLen);
  _pushRow("UID Len", buf);
  _pushRow("Protocol", "ISO14443A");
  _pushRow("[Press]", "Scan again");
  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532I2cScreen::_doAuthenticate() {
  if (!_hasCard && !_scanCardOrShow(5000)) { _goMifare(); return; }

  auto dims = _mfDims(_sak);
  if (dims.first == 0) { ShowStatusAction::show("Not MIFARE Classic"); _goMifare(); return; }
  size_t totalSectors = dims.first;

  _mfKeys.fill({});
  ProgressView::init();
  bool keyFound = false;

  for (size_t sector = 0; sector < totalSectors; sector++) {
    uint32_t trailer = (sector < 32) ? (sector * 4 + 3) : (128 + (sector - 32) * 16 + 15);
    for (uint8_t kt = 0; kt < 2; kt++) {
      bool useKeyB = (kt == 1);
      char msg[48];
      snprintf(msg, sizeof(msg), "S%d %s", (int)sector, useKeyB ? "B" : "A");
      int pct = (int)((sector * 2 + kt) * 100 / (totalSectors * 2));
      ProgressView::progress(msg, pct);

      bool found = false;
      for (const auto& key : NFCUtility::getDefaultKeys()) {
        const auto kv = key.value();
        if (_nfc->mifareclassic_AuthenticateBlock(
              _uid, _uidLen, trailer, useKeyB ? 1 : 0, (uint8_t*)kv.data())) {
          if (useKeyB) _mfKeys[sector].second = key;
          else         _mfKeys[sector].first  = key;
          found = true;
          if (!keyFound) {
            keyFound = true;
            int n = Achievement.inc("nfc_key_found");
            if (n == 1) Achievement.unlock("nfc_key_found");
          }
          break;
        }
        // Re-select card for next key attempt
        uint8_t rUid[7]; uint8_t rLen;
        _nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, rUid, &rLen, 200);
      }
      if (!found) {
        uint8_t rUid[7]; uint8_t rLen;
        _nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, rUid, &rLen, 200);
      }
    }
  }

  _goMifare();
}

void PN532I2cScreen::_doDumpMemory() {
  if (!_hasCard) { ShowStatusAction::show("Authenticate first"); _goMifare(); return; }
  auto dims = _mfDims(_sak);
  if (dims.first == 0) { ShowStatusAction::show("Not MIFARE Classic"); _goMifare(); return; }

  _state = STATE_MIFARE_DUMP;
  _resetRows();
  _hasDump  = false;
  size_t totalBlocks = dims.second;

  memset(_dumpImg, 0x00, sizeof(_dumpImg));
  static constexpr uint8_t kTrailer[16] = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0xFF,0x07,0x80,0x69, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
  };
  for (int s = 0; s < 16; s++) memcpy(&_dumpImg[(s * 4 + 3) * 16], kTrailer, 16);

  _dumpImg[0] = _uid[0]; _dumpImg[1] = _uid[1];
  _dumpImg[2] = _uid[2]; _dumpImg[3] = _uid[3];
  _dumpImg[4] = _uid[0] ^ _uid[1] ^ _uid[2] ^ _uid[3];
  _dumpImg[5] = _sak;
  _dumpImg[6] = (_atqa >> 8) & 0xFF;
  _dumpImg[7] = _atqa & 0xFF;

  int readCount = 0;
  ProgressView::init();

  for (size_t blk = 0; blk < totalBlocks; blk++) {
    size_t sector  = (blk < 128) ? (blk / 4) : ((blk - 128) / 16 + 32);
    uint32_t trailer = (sector < 32) ? (sector * 4 + 3) : (128 + (sector - 32) * 16 + 15);
    int pct = (int)(blk * 100 / totalBlocks);
    char msg[32];
    snprintf(msg, sizeof(msg), "Block %d", (int)blk);
    ProgressView::progress(msg, pct);

    String label = "B" + String((int)blk);
    auto& slotA = _mfKeys[sector].first;
    auto& slotB = _mfKeys[sector].second;
    bool useKeyB = !slotA && (bool)slotB;
    auto& slot   = useKeyB ? slotB : slotA;
    if (!slot) { _pushRow(label, "-"); continue; }

    const auto kv = slot.value();
    if (!_nfc->mifareclassic_AuthenticateBlock(
          _uid, _uidLen, trailer, useKeyB ? 1 : 0, (uint8_t*)kv.data())) {
      // Try re-select + re-auth
      uint8_t rUid[7]; uint8_t rLen;
      if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, rUid, &rLen, 200)) {
        if (!_nfc->mifareclassic_AuthenticateBlock(
              _uid, _uidLen, trailer, useKeyB ? 1 : 0, (uint8_t*)kv.data())) {
          _pushRow(label, "-"); continue;
        }
      } else { _pushRow(label, "-"); continue; }
    }

    uint8_t data[16];
    if (!_nfc->mifareclassic_ReadDataBlock((uint8_t)blk, data)) {
      _pushRow(label, "-"); continue;
    }
    _pushRow(label, _hexBlock(data + 13, 3));
    readCount++;
    if (blk < 64) memcpy(&_dumpImg[blk * 16], data, 16);
  }

  char summary[32];
  snprintf(summary, sizeof(summary), "%d/%d blocks", readCount, (int)totalBlocks);
  _pushRow("Read", summary);
  _pushRow("[Press]", "Save dump");
  _hasDump = true;

  int n = Achievement.inc("nfc_dump_memory");
  if (n == 1) Achievement.unlock("nfc_dump_memory");

  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532I2cScreen::_doShowKeys() {
  if (!_hasCard) { ShowStatusAction::show("Authenticate first"); _goMifare(); return; }
  auto dims = _mfDims(_sak);
  if (dims.first == 0) { ShowStatusAction::show("Not MIFARE Classic"); _goMifare(); return; }

  _state = STATE_MIFARE_KEYS;
  _resetRows();
  _pushRow("UID", _hexUid(_uid, _uidLen));
  for (size_t s = 0; s < dims.first; s++) {
    _pushRow("S" + String((int)s) + " A", String(_mfKeys[s].first.c_str().c_str()));
    _pushRow("S" + String((int)s) + " B", String(_mfKeys[s].second.c_str().c_str()));
  }
  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532I2cScreen::_doDictionaryPicker() {
  if (!_hasCard) { ShowStatusAction::show("Authenticate first"); _goMifare(); return; }

  _state = STATE_DICT_SELECT;
  uint8_t n = _browser.load(this, _dictPath, ".txt");
  if (n == 0) {
    ShowStatusAction::show("No dictionary files");
    _goMifare();
    return;
  }
  setItems(_browser.items(), n);
}

static bool _parseHexKeyI2c(const String& line, uint8_t out[6]) {
  String s = line; s.trim();
  if (s.length() == 0 || s.startsWith("#")) return false;
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

void PN532I2cScreen::_doDictionaryAttackWithFile(uint8_t fileIndex) {
  if (fileIndex >= _browser.count()) return;
  String filePath = _browser.entry(fileIndex).path;
  String content = Uni.Storage->readFile(filePath.c_str());
  if (content.length() == 0) { ShowStatusAction::show("Empty file"); return; }

  static constexpr uint8_t MAX_KEYS = 128;
  uint8_t keys[MAX_KEYS][6];
  uint8_t keyCount = 0;
  int start = 0;
  while (start < (int)content.length() && keyCount < MAX_KEYS) {
    int nl = content.indexOf('\n', start);
    if (nl < 0) nl = content.length();
    String line = content.substring(start, nl);
    if (_parseHexKeyI2c(line, keys[keyCount])) keyCount++;
    start = nl + 1;
  }
  if (keyCount == 0) { ShowStatusAction::show("No valid keys"); return; }

  auto dims = _mfDims(_sak);
  if (dims.first == 0) { ShowStatusAction::show("Not MIFARE Classic"); _goMifare(); return; }

  size_t totalSectors = dims.first;
  int recovered = 0;

  ProgressView::init();
  for (size_t sector = 0; sector < totalSectors; sector++) {
    uint32_t trailer = (sector < 32) ? (sector * 4 + 3) : (128 + (sector - 32) * 16 + 15);
    for (uint8_t kt = 0; kt < 2; kt++) {
      bool useKeyB = (kt == 1);
      auto& slot = useKeyB ? _mfKeys[sector].second : _mfKeys[sector].first;
      if (slot) continue;

      char msg[48];
      snprintf(msg, sizeof(msg), "Dict S%d %s", (int)sector, useKeyB ? "B" : "A");
      int pct = (int)((sector * 2 + kt) * 100 / (totalSectors * 2));
      ProgressView::progress(msg, pct);

      for (uint8_t k = 0; k < keyCount; k++) {
        if (_nfc->mifareclassic_AuthenticateBlock(
              _uid, _uidLen, trailer, useKeyB ? 1 : 0, keys[k])) {
          slot = NFCUtility::MIFARE_Key(keys[k][0], keys[k][1], keys[k][2],
                                        keys[k][3], keys[k][4], keys[k][5]);
          recovered++;
          break;
        }
        uint8_t rUid[7]; uint8_t rLen;
        _nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, rUid, &rLen, 200);
      }
    }
  }

  if (recovered > 0) {
    int n = Achievement.inc("nfc_dict_attack");
    if (n == 1) Achievement.unlock("nfc_dict_attack");
  }
  char msg[48];
  snprintf(msg, sizeof(msg), "Recovered %d keys", recovered);
  ShowStatusAction::show(msg);
  _goMifare();
}

void PN532I2cScreen::_doUltralightDump() {
  ShowStatusAction::show("Place UL/NTAG on reader...", 0);
  uint8_t uid[7]; uint8_t uidLen;
  uint32_t start = millis();
  bool ok = false;
  while (millis() - start < 5000) {
    Uni.update();
    if (Uni.Nav->wasPressed() &&
        Uni.Nav->readDirection() == INavigation::DIR_BACK) { _goUltralight(); return; }
    if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 200)) { ok = true; break; }
    delay(50);
  }
  if (!ok) { ShowStatusAction::show("No card"); _goUltralight(); return; }

  _state = STATE_RAW_RESULT;
  _resetRows();
  _pushRow("UID", _hexUid(uid, uidLen));

  ProgressView::init();
  for (uint8_t page = 0; page < 64; page++) {
    int pct = page * 100 / 64;
    char msg[24]; snprintf(msg, sizeof(msg), "Page %d", page);
    ProgressView::progress(msg, pct);
    uint8_t data[4];
    if (!_nfc->mifareultralight_ReadPage(page, data)) break;
    _pushRow("P" + String(page), _hexBlock(data, 4));
  }
  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532I2cScreen::_doUltralightWrite() {
  ShowStatusAction::show("Place UL/NTAG on reader...", 0);
  uint8_t uid[7]; uint8_t uidLen;
  uint32_t start = millis();
  bool ok = false;
  while (millis() - start < 5000) {
    Uni.update();
    if (Uni.Nav->wasPressed() &&
        Uni.Nav->readDirection() == INavigation::DIR_BACK) { _goUltralight(); return; }
    if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 200)) { ok = true; break; }
    delay(50);
  }
  if (!ok) { ShowStatusAction::show("No card"); _goUltralight(); return; }

  int page = InputNumberAction::popup("Page (4..63)", 4, 63, 4);
  if (InputNumberAction::wasCancelled()) { _goUltralight(); return; }

  String hex = InputTextAction::popup("Page data (8 hex)", "", InputTextAction::INPUT_HEX);
  if (InputTextAction::wasCancelled()) { _goUltralight(); return; }
  hex.replace(" ", "");
  if (hex.length() != 8) { ShowStatusAction::show("Need 8 hex chars"); _goUltralight(); return; }

  uint8_t data[4];
  for (int i = 0; i < 4; i++) {
    char b[3] = { hex[i * 2], hex[i * 2 + 1], 0 };
    char* end; unsigned long v = strtoul(b, &end, 16);
    if (*end != 0) { ShowStatusAction::show("Bad hex"); _goUltralight(); return; }
    data[i] = (uint8_t)v;
  }

  if (_nfc->mifareultralight_WritePage((uint8_t)page, data)) {
    ShowStatusAction::show("Write OK");
  } else {
    ShowStatusAction::show("Write failed");
  }
  _goUltralight();
}

void PN532I2cScreen::_doDetectGen1a() {
  ShowStatusAction::show("Place card on reader...", 0);
  uint8_t uid[7]; uint8_t uidLen;
  uint32_t start = millis();
  bool ok = false;
  while (millis() - start < 5000) {
    Uni.update();
    if (Uni.Nav->wasPressed() &&
        Uni.Nav->readDirection() == INavigation::DIR_BACK) { _goMagic(); return; }
    if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 200)) { ok = true; break; }
    delay(50);
  }
  if (!ok) { ShowStatusAction::show("No card"); _goMagic(); return; }

  // Set CIU_BitFraming TxLastBits=7 so the magic byte is sent as 7 bits
  _nfcWriteReg(_nfc, _wire, 0x633D, 0x07);

  static const uint8_t magic1[] = {0x40};
  uint8_t resp[4]; uint8_t rlen = sizeof(resp);
  bool isGen1a = _nfcCommThru(_nfc, _wire, magic1, 1, resp, rlen, 200);
  isGen1a = isGen1a && rlen >= 1 && resp[0] == 0x0A;

  _nfcWriteReg(_nfc, _wire, 0x633D, 0x00); // restore bit framing

  if (isGen1a) {
    int n = Achievement.inc("pn532_magic_detect");
    if (n == 1) Achievement.unlock("pn532_magic_detect");
  }
  ShowStatusAction::show(isGen1a ? "Gen1a detected" : "Not Gen1a");
  _goMagic();
}

void PN532I2cScreen::_doGen3SetUid() {
  ShowStatusAction::show("Place Gen3 card...", 0);
  uint8_t uid[7]; uint8_t uidLen;
  uint32_t start = millis();
  bool ok = false;
  while (millis() - start < 5000) {
    Uni.update();
    if (Uni.Nav->wasPressed() &&
        Uni.Nav->readDirection() == INavigation::DIR_BACK) { _goMagic(); return; }
    if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 200)) { ok = true; break; }
    delay(50);
  }
  if (!ok) { ShowStatusAction::show("No card"); _goMagic(); return; }

  String hex = InputTextAction::popup("New UID (8 or 14 hex)", "", InputTextAction::INPUT_HEX);
  if (InputTextAction::wasCancelled()) { _goMagic(); return; }
  hex.replace(" ", ""); hex.replace(":", "");
  if (hex.length() != 8 && hex.length() != 14) {
    ShowStatusAction::show("UID must be 4 or 7 bytes");
    _goMagic();
    return;
  }

  uint8_t newUid[7] = {0};
  uint8_t newUidLen = hex.length() / 2;
  for (uint8_t i = 0; i < newUidLen; i++) {
    char b[3] = { hex[i * 2], hex[i * 2 + 1], 0 };
    char* end; unsigned long v = strtoul(b, &end, 16);
    if (*end != 0) { ShowStatusAction::show("Bad hex"); _goMagic(); return; }
    newUid[i] = (uint8_t)v;
  }

  // Gen3 Set UID: 90 FB CC CC <len> <uid bytes> 00
  uint8_t cmd[12];
  cmd[0] = 0x90; cmd[1] = 0xFB; cmd[2] = 0xCC; cmd[3] = 0xCC;
  cmd[4] = newUidLen;
  memcpy(&cmd[5], newUid, newUidLen);
  cmd[5 + newUidLen] = 0x00;
  uint8_t resp[8]; uint8_t rlen = sizeof(resp);
  bool ok2 = _nfcDataExch(_nfc, _wire, cmd, 6 + newUidLen, resp, rlen);

  if (ok2) {
    int n = Achievement.inc("pn532_magic_detect");
    if (n == 1) Achievement.unlock("pn532_magic_detect");
  }
  ShowStatusAction::show(ok2 ? "Gen3 UID set" : "Set UID failed");
  _goMagic();
}

void PN532I2cScreen::_doGen3LockUid() {
  ShowStatusAction::show("Place Gen3 card...", 0);
  uint8_t uid[7]; uint8_t uidLen;
  uint32_t start = millis();
  bool ok = false;
  while (millis() - start < 5000) {
    Uni.update();
    if (Uni.Nav->wasPressed() &&
        Uni.Nav->readDirection() == INavigation::DIR_BACK) { _goMagic(); return; }
    if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 200)) { ok = true; break; }
    delay(50);
  }
  if (!ok) { ShowStatusAction::show("No card"); _goMagic(); return; }

  static const uint8_t cmd[] = {0x90, 0xFD, 0x11, 0x11, 0x00};
  uint8_t resp[8]; uint8_t rlen = sizeof(resp);
  bool locked = _nfcDataExch(_nfc, _wire, cmd, sizeof(cmd), resp, rlen);
  ShowStatusAction::show(locked ? "Gen3 UID locked" : "Lock failed");
  _goMagic();
}

void PN532I2cScreen::_doSaveDump() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable");
    render();
    return;
  }
  Uni.Storage->makeDir("/unigeek/nfc");
  Uni.Storage->makeDir(_dumpPath);

  String uid = _hexUid(_uid, _uidLen);
  uid.replace(":", "");
  String path = String(_dumpPath) + "/" + uid + ".bin";

  fs::File f = Uni.Storage->open(path.c_str(), "w");
  if (!f) { ShowStatusAction::show("Save failed"); render(); return; }
  f.write(_dumpImg, sizeof(_dumpImg));
  f.close();

  char msg[48];
  snprintf(msg, sizeof(msg), "Saved: %s.bin", uid.c_str());
  ShowStatusAction::show(msg);
  render();
}

// ── emulation helpers ──────────────────────────────────────────────────────

// Poll PN532 status byte until ready, then read the full response packet.
// Used after sendCommandCheckAck for commands where the response is delayed
// (TgInitAsTarget waiting for a reader, TgGetData waiting for reader cmd, etc.).
static bool _nfcPollResponse(TwoWire* wire, uint8_t* buf, uint8_t n, uint32_t timeoutMs) {
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    wire->requestFrom((uint8_t)PN532_I2C_ADDRESS, (uint8_t)1);
    if (wire->available() && (wire->read() & 0x01)) {
      _nfcReadI2C(wire, buf, n);
      return true;
    }
    delay(10);
  }
  return false;
}

// Sends a raw PN532 command frame over I2C and reads back only the ACK frame.
// Unlike sendCommandCheckAck, does NOT wait for the actual command response —
// safe for long-running commands like TgInitAsTarget and TgGetData whose
// responses arrive only when the reader acts (seconds later).
static bool _nfcSendCmdReadAck(TwoWire* wire, const uint8_t* cmd, uint8_t cmdlen, uint32_t timeoutMs) {
  uint8_t LEN = cmdlen + 1; // +1 for TFI byte
  // preamble(1) + startcode(2) + LEN(1) + LCS(1) + TFI(1) + data(cmdlen) + DCS(1) + postamble(1)
  uint8_t packet[8 + cmdlen];
  packet[0] = 0x00; packet[1] = 0x00; packet[2] = 0xFF;
  packet[3] = LEN;
  packet[4] = (uint8_t)(~LEN + 1);
  packet[5] = 0xD4; // TFI = host → PN532
  uint8_t sum = 0xD4;
  for (uint8_t i = 0; i < cmdlen; i++) { packet[6 + i] = cmd[i]; sum += cmd[i]; }
  packet[6 + cmdlen] = (uint8_t)(~sum + 1);
  packet[7 + cmdlen] = 0x00;

  wire->beginTransmission(PN532_I2C_ADDRESS);
  wire->write(packet, 8 + cmdlen);
  wire->endTransmission();
  delay(1); // I2C tuning (matches Adafruit SLOWDOWN)

  // Wait for PN532 ready (bit 0 of status byte), then read the 6-byte ACK frame
  uint32_t start = millis();
  bool rdy = false;
  while (millis() - start < timeoutMs) {
    wire->requestFrom((uint8_t)PN532_I2C_ADDRESS, (uint8_t)1);
    if (wire->available() && (wire->read() & 0x01)) { rdy = true; break; }
    delay(5);
  }
  if (!rdy) return false;

  uint8_t ack[6];
  _nfcReadI2C(wire, ack, 6); // reads status + 6 bytes, discards status
  return (ack[0] == 0x00 && ack[1] == 0x00 && ack[2] == 0xFF &&
          ack[3] == 0x00 && ack[4] == 0xFF && ack[5] == 0x00);
}

// ISO7816 Type 4 tag emulation via PN532 TgInitAsTarget (mode=0x05, PICC+Passive).
// Presents the given 3-byte NFCID1T to the reader and serves the NDEF payload
// via CC + NDEF file selects and READ BINARY commands.
void PN532I2cScreen::_emulateLoop(const uint8_t* nfcid1, const uint8_t* ndef, uint16_t ndefLen) {
  Serial.printf("[EMU] nfcid1: %02X %02X %02X  ndefLen: %u\n", nfcid1[0], nfcid1[1], nfcid1[2], ndefLen);
  Serial.printf("[EMU] SAMConfig...\n");
  bool samOk = _nfc->SAMConfig();
  Serial.printf("[EMU] SAMConfig: %s\n", samOk ? "ok" : "FAIL");

  // NDEF file: 2-byte length header followed by the NDEF message bytes
  static constexpr uint16_t MAX_NDEF = 128;
  uint8_t ndefFile[MAX_NDEF + 2] = {};
  uint16_t ndefFileLen = 2;
  if (ndef && ndefLen > 0 && ndefLen <= MAX_NDEF) {
    ndefFile[0] = (ndefLen >> 8) & 0xFF;
    ndefFile[1] = ndefLen & 0xFF;
    memcpy(&ndefFile[2], ndef, ndefLen);
    ndefFileLen = 2 + ndefLen;
  }

  // Capability Container (CC) — fixed for Type 4 NDEF emulation
  static constexpr uint8_t cc[] = {
    0x00, 0x0F,        // CCLEN = 15
    0x20,              // Mapping Version 2.0
    0x00, 0x54,        // MLe (max read)
    0x00, 0xFF,        // MLc (max write)
    0x04, 0x06,        // NDEF File Control TLV: T=4 L=6
    0xE1, 0x04,        // File Identifier
    0x00, MAX_NDEF,    // max NDEF file size
    0x00, 0x00         // read + write access: granted
  };

  bool running = true;
  while (running) {
    // SAMConfig resets PN532 RF/SAM state between sessions — required to clear
    // residual ISO-DEP state that causes TgGetData to return 0x13 on retry.
    _nfc->SAMConfig();
    ShowStatusAction::show("Waiting for reader...", 0);

    // TgInitAsTarget: mode=0x05 (PICC+Passive), Type 4 (SEL_RES=0x20)
    // Use _nfcSendCmdReadAck instead of sendCommandCheckAck — the Adafruit
    // helper waits for the full command response, which only arrives when a
    // reader presents. _nfcSendCmdReadAck reads only the immediate ACK frame
    // and lets us poll for the actual response separately.
    uint8_t target[38] = {};
    target[0] = PN532_COMMAND_TGINITASTARGET;
    target[1] = 0x05;        // PICC only + Passive only
    target[2] = 0x04;        // SENS_RES[0]
    target[3] = 0x00;        // SENS_RES[1]
    target[4] = nfcid1[0];
    target[5] = nfcid1[1];
    target[6] = nfcid1[2];
    target[7] = 0x20;        // SEL_RES: ISO14443-4 compliant
    // [8..37] = FeliCa(18) + NFCID3T(10) + GiLen(1) + TkLen(1) — all zero

    Serial.printf("[EMU] TgInitAsTarget cmd[0..7]: %02X %02X %02X %02X %02X %02X %02X %02X\n",
      target[0], target[1], target[2], target[3],
      target[4], target[5], target[6], target[7]);

    bool ackOk = _nfcSendCmdReadAck(_wire, target, 38, 1000);
    Serial.printf("[EMU] TgInitAsTarget ACK: %s\n", ackOk ? "ok" : "FAIL");
    if (!ackOk) {
      ShowStatusAction::show("PN532 error");
      break;
    }

    // Wait for reader to present — TgInitAsTargetResponse arrives when found
    bool readerFound = false;
    uint32_t start = millis();
    while (millis() - start < 30000) {
      Uni.update();
      if (Uni.Nav->wasPressed()) {
        if (Uni.Nav->readDirection() == INavigation::DIR_BACK) { running = false; break; }
      }
      _wire->requestFrom((uint8_t)PN532_I2C_ADDRESS, (uint8_t)1);
      if (_wire->available() && (_wire->read() & 0x01)) {
        uint8_t ibuf[20] = {};
        _nfcReadI2C(_wire, ibuf, 20);
        Serial.printf("[EMU] TgInitAsTarget resp[3..7]: %02X %02X %02X %02X %02X\n",
          ibuf[3], ibuf[4], ibuf[5], ibuf[6], ibuf[7]);
        if (ibuf[6] == (PN532_COMMAND_TGINITASTARGET + 1)) { readerFound = true; break; }
      }
      delay(50);
    }
    if (!readerFound) continue;
    Serial.printf("[EMU] Reader found, starting APDU loop\n");

    ShowStatusAction::show("Emulating...", 0);

    // ISO7816 APDU exchange loop
    enum SelectedFile { SEL_NONE, SEL_CC, SEL_NDEF } currentFile = SEL_NONE;
    static const uint8_t kNdefApp[] = {0xD2,0x76,0x00,0x00,0x85,0x01,0x01};

    while (running) {
      Uni.update();
      if (Uni.Nav->wasPressed()) {
        if (Uni.Nav->readDirection() == INavigation::DIR_BACK) { running = false; break; }
      }

      // TgGetData: send command and read ACK only, then poll for the APDU
      uint8_t tgGet[1] = { PN532_COMMAND_TGGETDATA };
      if (!_nfcSendCmdReadAck(_wire, tgGet, 1, 500)) {
        Serial.printf("[EMU] TgGetData ACK FAIL\n");
        break;
      }
      uint8_t gbuf[70] = {};
      bool gotApdu = false;
      uint32_t getStart = millis();
      while (millis() - getStart < 5000) {
        Uni.update();
        if (Uni.Nav->wasPressed()) {
          if (Uni.Nav->readDirection() == INavigation::DIR_BACK) { running = false; break; }
        }
        _wire->requestFrom((uint8_t)PN532_I2C_ADDRESS, (uint8_t)1);
        if (_wire->available() && (_wire->read() & 0x01)) {
          _nfcReadI2C(_wire, gbuf, 70);
          gotApdu = true; break;
        }
        delay(20);
      }
      if (!gotApdu || !running) {
        if (!gotApdu) Serial.printf("[EMU] TgGetData timeout (reader gone?)\n");
        break;
      }

      if (gbuf[5] != PN532_PN532TOHOST || gbuf[6] != (PN532_COMMAND_TGGETDATA + 1)) {
        Serial.printf("[EMU] TgGetData bad resp: [3..8]=%02X %02X %02X %02X %02X %02X\n",
          gbuf[3], gbuf[4], gbuf[5], gbuf[6], gbuf[7], gbuf[8]);
        break;
      }
      if (gbuf[7] & 0x3F) {
        Serial.printf("[EMU] TgGetData err: %02X  raw[3..9]=%02X %02X %02X %02X %02X %02X %02X\n",
          gbuf[7], gbuf[3], gbuf[4], gbuf[5], gbuf[6], gbuf[7], gbuf[8], gbuf[9]);
        break;
      }
      uint8_t cmdLen = gbuf[3] > 3 ? gbuf[3] - 3 : 0;
      if (cmdLen < 2) { Serial.printf("[EMU] TgGetData short frame len=%u\n", cmdLen); break; }

      uint8_t* apdu = &gbuf[8]; // [CLA, INS, P1, P2, LC, DATA...]
      uint8_t  ins  = apdu[1];
      uint8_t  p1   = cmdLen >= 3 ? apdu[2] : 0;
      uint8_t  p2   = cmdLen >= 4 ? apdu[3] : 0;
      uint8_t  lc   = cmdLen >= 5 ? apdu[4] : 0;
      Serial.printf("[EMU] APDU INS=%02X P1=%02X P2=%02X LC=%02X\n", ins, p1, p2, lc);

      uint8_t resp[66] = {};
      uint8_t respLen  = 2;
      resp[0] = 0x6A; resp[1] = 0x81; // default: function not supported

      switch (ins) {
        case 0xA4:  // SELECT FILE
          if (p1 == 0x04) {
            // SELECT by name — accept only NDEF application
            if (lc >= 7 && memcmp(&apdu[5], kNdefApp, 7) == 0) {
              resp[0] = 0x90; resp[1] = 0x00;
            } else {
              resp[0] = 0x6A; resp[1] = 0x82;  // file not found
            }
          } else {
            // SELECT by file ID
            if (lc == 2 && apdu[5] == 0xE1) {
              if      (apdu[6] == 0x03) { currentFile = SEL_CC;   resp[0] = 0x90; resp[1] = 0x00; }
              else if (apdu[6] == 0x04) { currentFile = SEL_NDEF; resp[0] = 0x90; resp[1] = 0x00; }
              else                      { resp[0] = 0x6A; resp[1] = 0x82; }
            } else {
              resp[0] = 0x90; resp[1] = 0x00;  // accept other selects generically
            }
          }
          break;

        case 0xB0:  // READ BINARY
        {
          uint16_t offset = ((uint16_t)p1 << 8) | p2;
          uint8_t  le     = lc;
          const uint8_t* src    = (currentFile == SEL_CC)   ? cc       : ndefFile;
          uint16_t        srcLen = (currentFile == SEL_CC)   ? sizeof(cc) : ndefFileLen;
          if (currentFile == SEL_NONE || offset >= srcLen) {
            resp[0] = 0x6A; resp[1] = 0x82;
          } else {
            uint8_t avail = (uint8_t)(srcLen - offset);
            uint8_t count = (le == 0 || le > avail) ? avail : le;
            if (count > 62) count = 62;
            memcpy(resp, src + offset, count);
            resp[count]   = 0x90;
            resp[count+1] = 0x00;
            respLen = count + 2;
          }
          break;
        }

        case 0xD6:  // UPDATE BINARY — accept silently (read-only emulation)
          resp[0] = 0x90; resp[1] = 0x00;
          break;

        default:
          resp[0] = 0x6A; resp[1] = 0x81;
          break;
      }
      Serial.printf("[EMU] Response SW=%02X%02X len=%u\n", resp[respLen-2], resp[respLen-1], respLen);

      // TgSetData: use sendCommandCheckAck (not the split ACK-only helper).
      // TgSetData response comes back quickly (~100-200ms once the reader ACKs
      // our I-block), so the second waitready in sendCommandCheckAck succeeds
      // reliably. Using the split helper here would risk reading the reader's
      // next APDU into sbuf (consuming it before TgGetData can fetch it).
      pn532_packetbuffer[0] = PN532_COMMAND_TGSETDATA;
      memcpy(&pn532_packetbuffer[1], resp, respLen);
      if (!_nfc->sendCommandCheckAck(pn532_packetbuffer, 1 + respLen, 1000)) {
        Serial.printf("[EMU] TgSetData FAIL\n");
        running = false; break;
      }
      uint8_t sbuf[12] = {};
      _nfcReadI2C(_wire, sbuf, 12); // response already ready after sendCommandCheckAck
      Serial.printf("[EMU] TgSetData resp[5..7]: %02X %02X %02X\n", sbuf[5], sbuf[6], sbuf[7]);
      if (sbuf[7] != 0x00) {
        Serial.printf("[EMU] TgSetData status err: %02X — reader disconnected?\n", sbuf[7]);
        break;
      }
    }
  }

  _nfc->SAMConfig();
}

void PN532I2cScreen::_doNtagMenu() {
  _state = STATE_NTAG_MENU;
  setItems(_ntagItems, 2);
}

void PN532I2cScreen::_doNtagText() {
  String text = InputTextAction::popup("Enter text to emulate", "");
  if (InputTextAction::wasCancelled() || text.length() == 0) { _doNtagMenu(); return; }
  render();

  // NDEF Text record: D1 01 <payloadLen> 54 02 'e' 'n' <text>
  uint8_t ndef[128] = {};
  uint8_t payloadLen = (uint8_t)(1 + 2 + text.length()); // status + "en" + text
  uint16_t ndefLen = 0;
  if (4 + payloadLen <= 128) {
    ndef[0] = 0xD1;       // MB|ME|SR|TNF=WellKnown
    ndef[1] = 0x01;       // Type length = 1
    ndef[2] = payloadLen;
    ndef[3] = 'T';
    ndef[4] = 0x02;       // UTF-8, lang code length = 2
    ndef[5] = 'e';
    ndef[6] = 'n';
    memcpy(&ndef[7], text.c_str(), text.length());
    ndefLen = 4 + payloadLen;
  }

  static const uint8_t nfcid1[] = {0xDC, 0x44, 0x20};
  _state = STATE_EMULATE;
  _emulateLoop(nfcid1, ndef, ndefLen);
  int n = Achievement.inc("pn532_emulate");
  if (n == 1) Achievement.unlock("pn532_emulate");
  ShowStatusAction::show("Emulation ended");
  _doNtagMenu();
}

void PN532I2cScreen::_doNtagUrl() {
  String url = InputTextAction::popup("Enter URL to emulate", "https://");
  if (InputTextAction::wasCancelled() || url.length() == 0) { _doNtagMenu(); return; }
  render();

  // Strip recognized URI prefix → compact encoding per NFC Forum URI spec
  uint8_t prefix = 0x00;
  const char* body = url.c_str();
  if      (url.startsWith("https://www.")) { prefix = 0x02; body += 12; }
  else if (url.startsWith("http://www."))  { prefix = 0x01; body += 11; }
  else if (url.startsWith("https://"))     { prefix = 0x04; body += 8; }
  else if (url.startsWith("http://"))      { prefix = 0x03; body += 7; }

  // NDEF URI record: D1 01 <payloadLen> 55 <prefix> <body>
  uint8_t ndef[128] = {};
  uint8_t bodyLen   = (uint8_t)strlen(body);
  uint8_t payloadLen = 1 + bodyLen; // prefix byte + body
  uint16_t ndefLen = 0;
  if (4 + payloadLen <= 128) {
    ndef[0] = 0xD1;
    ndef[1] = 0x01;
    ndef[2] = payloadLen;
    ndef[3] = 'U';
    ndef[4] = prefix;
    memcpy(&ndef[5], body, bodyLen);
    ndefLen = 5 + bodyLen;
  }

  static const uint8_t nfcid1[] = {0xDC, 0x44, 0x20};
  _state = STATE_EMULATE;
  _emulateLoop(nfcid1, ndef, ndefLen);
  int n = Achievement.inc("pn532_emulate");
  if (n == 1) Achievement.unlock("pn532_emulate");
  ShowStatusAction::show("Emulation ended");
  _doNtagMenu();
}