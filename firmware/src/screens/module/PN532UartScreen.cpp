#include "PN532UartScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/PinConfigManager.h"
#include "core/AchievementManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/views/ProgressView.h"

const char* PN532UartScreen::title() {
  switch (_state) {
    case STATE_MAIN_MENU:        return _isKiller ? "PN532Killer" : "PN532";
    case STATE_INFO:             return "Firmware Info";
    case STATE_SCAN_RESULT:      return "Scan Result";
    case STATE_SCAN_14A:         return "Scan ISO14443A";
    case STATE_SCAN_15:          return "Scan ISO15693";
    case STATE_SCAN_LF:          return "Scan EM4100";
    case STATE_MIFARE_MENU:      return "MIFARE Classic";
    case STATE_MIFARE_DUMP:      return "Memory Dump";
    case STATE_MIFARE_KEYS:      return "Discovered Keys";
    case STATE_DICT_SELECT:      return "Dictionary Attack";
    case STATE_ULTRALIGHT_MENU:  return "Ultralight / NTAG";
    case STATE_MAGIC_MENU:       return "Magic Card";
    case STATE_RAW_RESULT:       return "Result";
    case STATE_EMULATE:          return "Emulate Card";
  }
  return _isKiller ? "PN532Killer" : "PN532";
}

void PN532UartScreen::onInit() {
  if (!_initModule()) return;
  _goMain();
}

void PN532UartScreen::onUpdate() {
  if (_state == STATE_INFO || _state == STATE_SCAN_RESULT ||
      _state == STATE_MIFARE_DUMP || _state == STATE_MIFARE_KEYS ||
      _state == STATE_RAW_RESULT) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        // return to most recent menu
        if (_state == STATE_MIFARE_DUMP || _state == STATE_MIFARE_KEYS) _goMifare();
        else _goMain();
      } else {
        _scrollView.onNav(dir);
      }
    }
    return;
  }
  ListScreen::onUpdate();
}

void PN532UartScreen::onRender() {
  if (_state == STATE_INFO || _state == STATE_SCAN_RESULT ||
      _state == STATE_MIFARE_DUMP || _state == STATE_MIFARE_KEYS ||
      _state == STATE_RAW_RESULT) {
    _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  ListScreen::onRender();
}

void PN532UartScreen::onItemSelected(uint8_t index) {
  switch (_state) {
    case STATE_MAIN_MENU:
      switch (index) {
        case 0: _doScan14A();         break;
        case 1: _doScan15();          break;
        case 2: _doScanLF();          break;
        case 3: _goMifare();          break;
        case 4: _goUltralight();      break;
        case 5: _goMagic();           break;
        // case 6: _doEmulate();      // hidden until full APDU emulation is ready
        case 6: _showFirmwareInfo();  break;
      }
      break;
    case STATE_MIFARE_MENU:
      switch (index) {
        case 0: _doAuthenticate();    break;
        case 1: _doDumpMemory();      break;
        case 2: _doShowKeys();        break;
        case 3: _doDictionaryPicker();break;
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
        case 0: _doDetectGen1a();     break;
        case 1: _doGen3SetUid();      break;
        case 2: _doGen3LockUid();     break;
      }
      break;
    case STATE_DICT_SELECT:
      _doDictionaryAttackWithFile(index);
      break;
    default: break;
  }
}

void PN532UartScreen::onBack() {
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
    default:
      _goMain();
      break;
  }
}

bool PN532UartScreen::_initModule() {
  int tx = PinConfig.getInt(PIN_CONFIG_PN532_TX, PIN_CONFIG_PN532_TX_DEFAULT);
  int rx = PinConfig.getInt(PIN_CONFIG_PN532_RX, PIN_CONFIG_PN532_RX_DEFAULT);
  int baud = PinConfig.getInt(PIN_CONFIG_PN532_BAUD, PIN_CONFIG_PN532_BAUD_DEFAULT);
  if (tx < 0 || rx < 0) {
    ShowStatusAction::show("Configure PN532 TX/RX pins first");
    Screen.goBack();
    return false;
  }

  ProgressView::init();
  ProgressView::progress("Opening UART...", 10);

  // GPS shares the same HardwareSerial(2) — make sure it's idle before we claim it.
  _hsu = new PN532HSU(2, (int8_t)tx, (int8_t)rx, (uint32_t)baud);
  if (!_hsu->begin()) {
    ShowStatusAction::show("UART begin failed");
    delete _hsu; _hsu = nullptr;
    Screen.goBack();
    return false;
  }

  ProgressView::progress("Waking PN532...", 35);
  _pn = new PN532(*_hsu);
  if (!_pn->init()) {
    ShowStatusAction::show("PN532 not responding");
    _cleanup();
    Screen.goBack();
    return false;
  }

  ProgressView::progress("Reading firmware...", 70);
  _pn->getFirmwareVersion(_fw);

  // Best-effort PN532Killer detection — many stock PN532s answer with bad-frame.
  uint8_t killer = 0;
  _isKiller = _pn->isPN532Killer(killer);
  _killerCode = killer;

  _ready = true;
  int n = Achievement.inc("pn532_first_use");
  if (n == 1) Achievement.unlock("pn532_first_use");
  return true;
}

void PN532UartScreen::_cleanup() {
  if (_pn)  { delete _pn;  _pn  = nullptr; }
  if (_hsu) { delete _hsu; _hsu = nullptr; }
  _ready = false;
  _hasCard = false;
  _mfKeys.fill({});
}

void PN532UartScreen::_goMain() {
  _state = STATE_MAIN_MENU;
  setItems(_mainItems);
  render();
}

void PN532UartScreen::_goMifare() {
  _state = STATE_MIFARE_MENU;
  setItems(_mfItems);
  render();
}

void PN532UartScreen::_goUltralight() {
  _state = STATE_ULTRALIGHT_MENU;
  setItems(_ulItems);
  render();
}

void PN532UartScreen::_goMagic() {
  _state = STATE_MAGIC_MENU;
  setItems(_magicItems);
  render();
}

// ── helpers ────────────────────────────────────────────────────────────────

String PN532UartScreen::_hexUid(const uint8_t* uid, uint8_t len) const {
  String s;
  for (uint8_t i = 0; i < len; i++) {
    char buf[4];
    sprintf(buf, "%s%02X", i == 0 ? "" : ":", uid[i]);
    s += buf;
  }
  return s;
}

String PN532UartScreen::_hexBlock(const uint8_t* data, uint8_t len) const {
  String s;
  for (uint8_t i = 0; i < len; i++) {
    char buf[4];
    sprintf(buf, "%s%02X", i == 0 ? "" : " ", data[i]);
    s += buf;
  }
  return s;
}

void PN532UartScreen::_resetRows() {
  _rowCount = 0;
}

void PN532UartScreen::_pushRow(const String& label, const String& value) {
  if (_rowCount >= MAX_ROWS) return;
  _rowLabels[_rowCount] = label;
  _rowValues[_rowCount] = value;
  _rows[_rowCount] = { _rowLabels[_rowCount].c_str(), _rowValues[_rowCount] };
  _rowCount++;
}

std::pair<size_t, size_t> PN532UartScreen::_mfDims(uint8_t sak) const {
  // MIFARE Classic SAKs (rough) — Mini=0x09 5/20, 1K=0x08 16/64, 4K=0x18 40/256
  if (sak == 0x09) return {5, 20};
  if (sak == 0x08) return {16, 64};
  if (sak == 0x18) return {40, 256};
  return {0, 0};
}

bool PN532UartScreen::_scanCardOrShow(uint32_t timeoutMs) {
  ShowStatusAction::show("Place card on reader...", 0);
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    Uni.update();
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) return false;
    }
    PN532::Target14A t;
    if (_pn->listPassiveTarget14A(t, 200)) {
      _card = t;
      _hasCard = true;
      _mfKeys.fill({});
      return true;
    }
    delay(50);
  }
  ShowStatusAction::show("No card found");
  return false;
}

const char* PN532UartScreen::_inferType(uint8_t sak, uint16_t atqa) const {
  uint8_t atqaHi = (atqa >> 8) & 0xFF;
  if (sak == 0x09) return "MF Classic Mini";
  if (sak == 0x08) return "MF Classic 1K";
  if (sak == 0x18) return "MF Classic 4K";
  if (sak == 0x28) return "MF Plus / SmartMX";
  if (sak == 0x20) return atqaHi == 0x03 ? "MIFARE DESFire" : "ISO14443-4";
  if (sak == 0x00) return (atqa & 0x00FF) == 0x44 ? "MIFARE UL / NTAG" : "ISO14443A T2";
  return "ISO14443A";
}

// ── actions ────────────────────────────────────────────────────────────────

void PN532UartScreen::_showFirmwareInfo() {
  _state = STATE_INFO;
  _resetRows();
  if (_fw.valid) {
    char buf[16];
    sprintf(buf, "0x%02X", _fw.ic);
    _pushRow("IC", buf);
    sprintf(buf, "%u.%u", _fw.version, _fw.revision);
    _pushRow("Version", buf);
    sprintf(buf, "0x%02X", _fw.support);
    _pushRow("Support", buf);
  } else {
    _pushRow("Firmware", "n/a");
  }
  _pushRow("Variant", _isKiller ? "PN532Killer" : "Stock PN532");
  if (_isKiller) {
    char buf[8];
    sprintf(buf, "0x%02X", _killerCode);
    _pushRow("Killer Code", buf);
  }
  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532UartScreen::_doScan14A() {
  _state = STATE_SCAN_14A;
  PN532::Target14A t;
  ShowStatusAction::show("Scanning 14A...", 0);
  bool ok = false;
  uint32_t start = millis();
  while (millis() - start < 5000) {
    Uni.update();
    if (Uni.Nav->wasPressed()) {
      if (Uni.Nav->readDirection() == INavigation::DIR_BACK) { _goMain(); return; }
    }
    if (_pn->listPassiveTarget14A(t, 200)) { ok = true; break; }
    delay(50);
  }

  if (!ok) {
    ShowStatusAction::show("No card");
    _goMain();
    return;
  }

  _card = t; _hasCard = true; _mfKeys.fill({});

  int n = Achievement.inc("nfc_uid_first");
  if (n == 1)  Achievement.unlock("nfc_uid_first");
  if (n == 10) Achievement.unlock("nfc_uid_10");

  _state = STATE_SCAN_RESULT;
  _resetRows();
  char buf[24];
  _pushRow("UID",      _hexUid(t.uid, t.uidLen));
  _pushRow("Type",     _inferType(t.sak, t.atqa));
  snprintf(buf, sizeof(buf), "%02X:%02X", (t.atqa >> 8) & 0xFF, t.atqa & 0xFF);
  _pushRow("ATQA", buf);
  snprintf(buf, sizeof(buf), "%02X", t.sak);
  _pushRow("SAK", buf);
  snprintf(buf, sizeof(buf), "%d bytes", t.uidLen);
  _pushRow("UID Len", buf);
  if (t.atsLen > 0) _pushRow("ATS", _hexBlock(t.ats, t.atsLen));
  _pushRow("Protocol", "ISO14443A");
  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532UartScreen::_doScan15() {
  _state = STATE_SCAN_15;
  ShowStatusAction::show("Scanning 15693...", 0);
  PN532::Target15 t;
  bool ok = false;
  uint32_t start = millis();
  while (millis() - start < 5000) {
    Uni.update();
    if (Uni.Nav->wasPressed()) {
      if (Uni.Nav->readDirection() == INavigation::DIR_BACK) { _goMain(); return; }
    }
    if (_pn->listPassiveTarget15(t, 200)) { ok = true; break; }
    delay(50);
  }
  if (!ok) { ShowStatusAction::show("No 15693 tag"); _goMain(); return; }

  _state = STATE_SCAN_RESULT;
  _resetRows();
  char buf[8];
  _pushRow("UID", _hexUid(t.uid, 8));
  snprintf(buf, sizeof(buf), "%02X", t.dsfid);
  _pushRow("DSFID", buf);
  _pushRow("Protocol", "ISO15693");
  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532UartScreen::_doScanLF() {
  _state = STATE_SCAN_LF;
  ShowStatusAction::show("Scanning EM4100...", 0);
  PN532::TargetLF t;
  bool ok = false;
  uint32_t start = millis();
  while (millis() - start < 5000) {
    Uni.update();
    if (Uni.Nav->wasPressed()) {
      if (Uni.Nav->readDirection() == INavigation::DIR_BACK) { _goMain(); return; }
    }
    if (_pn->listPassiveTargetEM4100(t, 200)) { ok = true; break; }
    delay(50);
  }
  if (!ok) { ShowStatusAction::show("No LF tag"); _goMain(); return; }

  _state = STATE_SCAN_RESULT;
  _resetRows();
  _pushRow("UID",      _hexUid(t.uid, t.uidLen));
  _pushRow("Protocol", "EM4100 (LF)");
  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532UartScreen::_doAuthenticate() {
  if (!_hasCard && !_scanCardOrShow(5000)) { _goMifare(); return; }

  auto dims = _mfDims(_card.sak);
  if (dims.first == 0) {
    ShowStatusAction::show("Not MIFARE Classic");
    _goMifare();
    return;
  }
  size_t totalSectors = dims.first;

  _mfKeys.fill({});
  ProgressView::init();
  bool keyFound = false;

  for (size_t sector = 0; sector < totalSectors; sector++) {
    int trailer = (sector < 32) ? (sector * 4 + 3) : (128 + (sector - 32) * 16 + 15);
    for (uint8_t kt = 0; kt < 2; kt++) {
      bool useKeyB = (kt == 1);
      char msg[48];
      snprintf(msg, sizeof(msg), "S%d %s", (int)sector, useKeyB ? "B" : "A");
      int pct = (int)((sector * 2 + kt) * 100 / (totalSectors * 2));
      ProgressView::progress(msg, pct);

      bool found = false;
      for (const auto& key : NFCUtility::getDefaultKeys()) {
        const auto kv = key.value();
        const uint8_t* authUid = _card.uidLen == 7 ? &_card.uid[3] : _card.uid;
        if (_pn->mifareAuth((uint8_t)trailer, useKeyB, kv.data(), authUid)) {
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
        // re-select after auth failure
        _pn->inRelease();
        PN532::Target14A re;
        if (!_pn->listPassiveTarget14A(re, 200)) break;
      }
      if (!found) {
        _pn->inRelease();
        PN532::Target14A re;
        _pn->listPassiveTarget14A(re, 200);
      }
    }
  }

  _goMifare();
}

void PN532UartScreen::_doDumpMemory() {
  if (!_hasCard) { ShowStatusAction::show("Authenticate first"); _goMifare(); return; }
  auto dims = _mfDims(_card.sak);
  if (dims.first == 0) { ShowStatusAction::show("Not MIFARE Classic"); _goMifare(); return; }

  _state = STATE_MIFARE_DUMP;
  _resetRows();
  size_t totalBlocks = dims.second;

  ProgressView::init();
  for (size_t blk = 0; blk < totalBlocks; blk++) {
    int sector = (blk < 128) ? (blk / 4) : ((blk - 128) / 16 + 32);
    int trailer = (sector < 32) ? (sector * 4 + 3) : (128 + (sector - 32) * 16 + 15);
    int pct = (int)(blk * 100 / totalBlocks);
    char msg[32];
    snprintf(msg, sizeof(msg), "Block %d", (int)blk);
    ProgressView::progress(msg, pct);

    String label = "B" + String((int)blk);
    auto& slotA = _mfKeys[sector].first;
    auto& slotB = _mfKeys[sector].second;
    bool useKeyB = !slotA && (bool)slotB;
    auto& slot = useKeyB ? slotB : slotA;

    if (!slot) { _pushRow(label, "?? ?? ?? ?? ?? ?? ?? ??"); continue; }

    const uint8_t* authUid = _card.uidLen == 7 ? &_card.uid[3] : _card.uid;
    if (!_pn->mifareAuth((uint8_t)trailer, useKeyB, slot.value().data(), authUid)) {
      _pn->inRelease();
      PN532::Target14A re;
      if (_pn->listPassiveTarget14A(re, 200)) {
        if (!_pn->mifareAuth((uint8_t)trailer, useKeyB, slot.value().data(), authUid)) {
          _pushRow(label, "auth fail");
          continue;
        }
      } else { _pushRow(label, "lost card"); continue; }
    }

    uint8_t data[16];
    if (!_pn->mifareRead((uint8_t)blk, data)) { _pushRow(label, "read fail"); continue; }
    _pushRow(label, _hexBlock(data, 16));
  }

  int n = Achievement.inc("nfc_dump_memory");
  if (n == 1) Achievement.unlock("nfc_dump_memory");

  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532UartScreen::_doShowKeys() {
  if (!_hasCard) { ShowStatusAction::show("Authenticate first"); _goMifare(); return; }
  auto dims = _mfDims(_card.sak);
  if (dims.first == 0) { ShowStatusAction::show("Not MIFARE Classic"); _goMifare(); return; }

  _state = STATE_MIFARE_KEYS;
  _resetRows();
  _pushRow("UID", _hexUid(_card.uid, _card.uidLen));
  for (size_t s = 0; s < dims.first; s++) {
    _pushRow("S" + String((int)s) + " A", String(_mfKeys[s].first.c_str().c_str()));
    _pushRow("S" + String((int)s) + " B", String(_mfKeys[s].second.c_str().c_str()));
  }
  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532UartScreen::_doDictionaryPicker() {
  if (!_hasCard) { ShowStatusAction::show("Authenticate first"); _goMifare(); return; }

  _state = STATE_DICT_SELECT;
  _dictFileCount = 0;
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage not available"); _goMifare(); return;
  }

  IStorage::DirEntry entries[MAX_DICT_FILES];
  uint8_t count = Uni.Storage->listDir(_dictPath, entries, MAX_DICT_FILES);
  for (uint8_t i = 0; i < count && _dictFileCount < MAX_DICT_FILES; i++) {
    if (!entries[i].isDir && entries[i].name.endsWith(".txt")) {
      _dictFileNames[_dictFileCount] = entries[i].name;
      _dictItems[_dictFileCount] = { _dictFileNames[_dictFileCount].c_str() };
      _dictFileCount++;
    }
  }
  if (_dictFileCount == 0) {
    ShowStatusAction::show("No dictionary files");
    _goMifare();
    return;
  }
  setItems(_dictItems, _dictFileCount);
}

static bool _parseHexKeyLine(const String& line, uint8_t out[6]) {
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

void PN532UartScreen::_doDictionaryAttackWithFile(uint8_t fileIndex) {
  if (fileIndex >= _dictFileCount) return;
  String filePath = String(_dictPath) + "/" + _dictFileNames[fileIndex];
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
    if (_parseHexKeyLine(line, keys[keyCount])) keyCount++;
    start = nl + 1;
  }
  if (keyCount == 0) { ShowStatusAction::show("No valid keys"); return; }

  auto dims = _mfDims(_card.sak);
  if (dims.first == 0) { ShowStatusAction::show("Not MIFARE Classic"); _goMifare(); return; }

  size_t totalSectors = dims.first;
  int recovered = 0;

  ProgressView::init();
  for (size_t sector = 0; sector < totalSectors; sector++) {
    int trailer = (sector < 32) ? (sector * 4 + 3) : (128 + (sector - 32) * 16 + 15);
    for (uint8_t kt = 0; kt < 2; kt++) {
      bool useKeyB = (kt == 1);
      auto& slot = useKeyB ? _mfKeys[sector].second : _mfKeys[sector].first;
      if (slot) continue;

      char msg[48];
      snprintf(msg, sizeof(msg), "Dict S%d %s", (int)sector, useKeyB ? "B" : "A");
      int pct = (int)((sector * 2 + kt) * 100 / (totalSectors * 2));
      ProgressView::progress(msg, pct);

      const uint8_t* authUid = _card.uidLen == 7 ? &_card.uid[3] : _card.uid;
      for (uint8_t k = 0; k < keyCount; k++) {
        if (_pn->mifareAuth((uint8_t)trailer, useKeyB, keys[k], authUid)) {
          slot = NFCUtility::MIFARE_Key(keys[k][0], keys[k][1], keys[k][2],
                                        keys[k][3], keys[k][4], keys[k][5]);
          recovered++;
          break;
        }
        _pn->inRelease();
        PN532::Target14A re;
        if (!_pn->listPassiveTarget14A(re, 200)) break;
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

void PN532UartScreen::_doUltralightDump() {
  PN532::Target14A t;
  ShowStatusAction::show("Place UL/NTAG on reader...", 0);
  uint32_t start = millis();
  bool ok = false;
  while (millis() - start < 5000) {
    Uni.update();
    if (Uni.Nav->wasPressed() &&
        Uni.Nav->readDirection() == INavigation::DIR_BACK) { _goUltralight(); return; }
    if (_pn->listPassiveTarget14A(t, 200)) { ok = true; break; }
    delay(50);
  }
  if (!ok) { ShowStatusAction::show("No card"); _goUltralight(); return; }

  _state = STATE_RAW_RESULT;
  _resetRows();
  _pushRow("UID", _hexUid(t.uid, t.uidLen));

  ProgressView::init();
  for (uint8_t page = 0; page < 64; page += 4) {
    uint8_t data[16];
    int pct = (int)(page * 100 / 64);
    char msg[24]; snprintf(msg, sizeof(msg), "Page %d", page);
    ProgressView::progress(msg, pct);
    if (!_pn->ultralightRead(page, data)) break;
    for (int i = 0; i < 4; i++) {
      String label = "P" + String(page + i);
      _pushRow(label, _hexBlock(&data[i * 4], 4));
    }
  }
  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532UartScreen::_doUltralightWrite() {
  PN532::Target14A t;
  ShowStatusAction::show("Place UL/NTAG on reader...", 0);
  uint32_t start = millis();
  bool ok = false;
  while (millis() - start < 5000) {
    Uni.update();
    if (Uni.Nav->wasPressed() &&
        Uni.Nav->readDirection() == INavigation::DIR_BACK) { _goUltralight(); return; }
    if (_pn->listPassiveTarget14A(t, 200)) { ok = true; break; }
    delay(50);
  }
  if (!ok) { ShowStatusAction::show("No card"); _goUltralight(); return; }

  int page = InputNumberAction::popup("Page (4..39)", 4, 39, 4);
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

  if (_pn->ultralightWrite4((uint8_t)page, data)) {
    ShowStatusAction::show("Write OK");
  } else {
    ShowStatusAction::show("Write failed");
  }
  _goUltralight();
}

void PN532UartScreen::_doDetectGen1a() {
  PN532::Target14A t;
  ShowStatusAction::show("Place card on reader...", 0);
  uint32_t start = millis();
  bool ok = false;
  while (millis() - start < 5000) {
    Uni.update();
    if (Uni.Nav->wasPressed() &&
        Uni.Nav->readDirection() == INavigation::DIR_BACK) { _goMagic(); return; }
    if (_pn->listPassiveTarget14A(t, 200)) { ok = true; break; }
    delay(50);
  }
  if (!ok) { ShowStatusAction::show("No card"); _goMagic(); return; }
  bool gen1a = _pn->isGen1a();
  if (gen1a) {
    int n = Achievement.inc("pn532_magic_detect");
    if (n == 1) Achievement.unlock("pn532_magic_detect");
  }
  ShowStatusAction::show(gen1a ? "Gen1a detected" : "Not Gen1a");
  _goMagic();
}

void PN532UartScreen::_doGen3SetUid() {
  PN532::Target14A t;
  ShowStatusAction::show("Place Gen3 card...", 0);
  uint32_t start = millis();
  bool ok = false;
  while (millis() - start < 5000) {
    Uni.update();
    if (Uni.Nav->wasPressed() &&
        Uni.Nav->readDirection() == INavigation::DIR_BACK) { _goMagic(); return; }
    if (_pn->listPassiveTarget14A(t, 200)) { ok = true; break; }
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

  uint8_t uid[7] = {0};
  uint8_t uidLen = hex.length() / 2;
  for (uint8_t i = 0; i < uidLen; i++) {
    char b[3] = { hex[i * 2], hex[i * 2 + 1], 0 };
    char* end; unsigned long v = strtoul(b, &end, 16);
    if (*end != 0) { ShowStatusAction::show("Bad hex"); _goMagic(); return; }
    uid[i] = (uint8_t)v;
  }

  bool ok2 = _pn->gen3SetUid(uid, uidLen);
  if (ok2) {
    int n = Achievement.inc("pn532_magic_detect");
    if (n == 1) Achievement.unlock("pn532_magic_detect");
  }
  ShowStatusAction::show(ok2 ? "Gen3 UID set" : "Set UID failed");
  _goMagic();
}

void PN532UartScreen::_doGen3LockUid() {
  PN532::Target14A t;
  ShowStatusAction::show("Place Gen3 card...", 0);
  uint32_t start = millis();
  bool ok = false;
  while (millis() - start < 5000) {
    Uni.update();
    if (Uni.Nav->wasPressed() &&
        Uni.Nav->readDirection() == INavigation::DIR_BACK) { _goMagic(); return; }
    if (_pn->listPassiveTarget14A(t, 200)) { ok = true; break; }
    delay(50);
  }
  if (!ok) { ShowStatusAction::show("No card"); _goMagic(); return; }
  ShowStatusAction::show(_pn->gen3LockUid() ? "Gen3 UID locked" : "Lock failed");
  _goMagic();
}

void PN532UartScreen::_doEmulate() {
  if (!_hasCard) {
    ShowStatusAction::show("Scan a card first");
    _goMain();
    return;
  }

  _state = STATE_EMULATE;

  char msg[64];
  snprintf(msg, sizeof(msg), "Emulating: %s", _hexUid(_card.uid, _card.uidLen).c_str());
  ShowStatusAction::show(msg, 0);

  while (true) {
    Uni.update();
    if (Uni.Nav->wasPressed()) {
      if (Uni.Nav->readDirection() == INavigation::DIR_BACK) break;
    }
    if (_pn->tgInitAsTarget(_card.atqa, _card.sak, _card.uid, _card.uidLen, 300)) {
      int n = Achievement.inc("pn532_emulate");
      if (n == 1) Achievement.unlock("pn532_emulate");
      ShowStatusAction::show("Card read by reader!", 0);
      delay(800);
      ShowStatusAction::show(msg, 0);
    }
    delay(10);
  }

  _goMain();
}
