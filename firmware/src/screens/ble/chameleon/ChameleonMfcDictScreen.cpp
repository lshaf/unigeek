#include "ChameleonMfcDictScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "ChameleonHFMenuScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "core/ConfigManager.h"
#include "ui/actions/ShowStatusAction.h"

// Builtin default Mifare Classic key list (trimmed from upstream gMifareClassicKeysList)
static constexpr uint8_t kBuiltinKeys[][6] = {
  {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
  {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5},
  {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7},
  {0x00,0x00,0x00,0x00,0x00,0x00},
  {0xB0,0xB1,0xB2,0xB3,0xB4,0xB5},
  {0x4D,0x3A,0x99,0xC3,0x51,0xDD},
  {0x1A,0x98,0x2C,0x7E,0x45,0x9A},
  {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF},
  {0x71,0x4C,0x5C,0x88,0x6E,0x97},
  {0x58,0x7E,0xE5,0xF9,0x35,0x0F},
  {0xA0,0x47,0x8C,0xC3,0x90,0x91},
  {0x53,0x3C,0xB6,0xC7,0x23,0xF6},
  {0x8F,0xD0,0xA4,0xF2,0x56,0xE9},
  {0x00,0x00,0x00,0x00,0x00,0x01},
  {0x11,0x22,0x33,0x44,0x55,0x66},
  {0x26,0x97,0x34,0x3B,0x00,0x00},
  {0x12,0x34,0x56,0x78,0x9A,0xBC},
  {0xBD,0x49,0x3A,0x39,0x62,0xB6},
};
static constexpr uint8_t kBuiltinCount = sizeof(kBuiltinKeys) / 6;

static constexpr const char* kDictDir = "/unigeek/nfc/dictionaries";

uint8_t ChameleonMfcDictScreen::_trailerBlock(uint8_t sector) {
  return (sector < 32) ? (sector * 4 + 3) : (128 + (sector - 32) * 16 + 15);
}

void ChameleonMfcDictScreen::_loadFilePicker() {
  _fileCount = 0;

  _items[0] = {"Built-in keys"};

  if (Uni.Storage && Uni.Storage->isAvailable()) {
    IStorage::DirEntry entries[MAX_DICT_FILES];
    uint8_t total = Uni.Storage->listDir(kDictDir, entries, MAX_DICT_FILES);
    for (uint8_t i = 0; i < total && _fileCount < MAX_DICT_FILES; i++) {
      if (!entries[i].isDir && entries[i].name.endsWith(".txt")) {
        _fileNames[_fileCount] = entries[i].name;
        _items[1 + _fileCount] = { _fileNames[_fileCount].c_str() };
        _fileCount++;
      }
    }
  }

  setItems(_items, 1 + _fileCount);
}

void ChameleonMfcDictScreen::onInit() {
  _state = STATE_SELECT;
  _loadFilePicker();
}

void ChameleonMfcDictScreen::onBack() {
  Screen.goBack();
}

void ChameleonMfcDictScreen::onUpdate() {
  if (_state == STATE_RUNNING) return;

  if (_state == STATE_LOG_DONE) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        _state = STATE_SELECT;
        _loadFilePicker();
        render();
        return;
      }
      if (dir == INavigation::DIR_PRESS) {
        _state = STATE_RESULT;
        render();
        return;
      }
      if (dir == INavigation::DIR_UP)   _runLog.scroll(1);
      if (dir == INavigation::DIR_DOWN) _runLog.scroll(-1);
      _runLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _runStatusBarCb, this);
    }
    return;
  }

  if (_state == STATE_RESULT) {
    if (Uni.Nav->isPressed() && Uni.Nav->heldDuration() >= 1000) {
      Uni.Nav->suppressCurrentPress();
      _state = STATE_SELECT;
      _loadFilePicker();
      render();
      return;
    }
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        _state = STATE_SELECT;
        _loadFilePicker();
        render();
        return;
      }
      _scrollView.onNav(dir);
    }
    return;
  }

  ListScreen::onUpdate();
}

void ChameleonMfcDictScreen::onRender() {
  if (_state == STATE_RESULT) {
    _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  if (_state == STATE_RUNNING || _state == STATE_LOG_DONE) {
    _runLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _runStatusBarCb, this);
    return;
  }
  ListScreen::onRender();
}

void ChameleonMfcDictScreen::onItemSelected(uint8_t index) {
  if (_state != STATE_SELECT) return;

  char srcLabel[40];
  if (index == 0) {
    _loadBuiltinKeys();
    snprintf(srcLabel, sizeof(srcLabel), "Built-in");
  } else {
    uint8_t fi = index - 1;
    if (fi >= _fileCount) return;
    String path = String(kDictDir) + "/" + _fileNames[fi];
    if (!_loadFileKeys(path.c_str())) {
      ShowStatusAction::show("Load keys failed", 1200);
      render();
      return;
    }
    snprintf(srcLabel, sizeof(srcLabel), "%s", _fileNames[fi].c_str());
  }

  if (_keyCount == 0) {
    ShowStatusAction::show("No keys in source", 1200);
    render();
    return;
  }

  _runAttack(srcLabel);
}

bool ChameleonMfcDictScreen::_loadBuiltinKeys() {
  _keyCount = kBuiltinCount;
  memcpy(_keys, kBuiltinKeys, kBuiltinCount * 6);
  return true;
}

static bool _parseHexKey(const String& line, uint8_t out[6]) {
  String s = line;
  s.trim();
  if (s.length() == 0 || s.startsWith("#")) return false;
  s.replace(":", "");
  s.replace(" ", "");
  if (s.length() != 12) return false;
  for (int i = 0; i < 6; i++) {
    char hex[3] = { s[i * 2], s[i * 2 + 1], 0 };
    char* end = nullptr;
    unsigned long v = strtoul(hex, &end, 16);
    if (*end != 0) return false;
    out[i] = (uint8_t)v;
  }
  return true;
}

bool ChameleonMfcDictScreen::_loadFileKeys(const char* path) {
  _keyCount = 0;
  if (!Uni.Storage || !Uni.Storage->isAvailable()) return false;
  String content = Uni.Storage->readFile(path);
  if (content.length() == 0) return false;

  int start = 0;
  while (start < (int)content.length() && _keyCount < MAX_KEYS) {
    int nl = content.indexOf('\n', start);
    if (nl < 0) nl = content.length();
    String line = content.substring(start, nl);
    uint8_t k[6];
    if (_parseHexKey(line, k)) {
      memcpy(_keys[_keyCount], k, 6);
      _keyCount++;
    }
    start = nl + 1;
  }
  return _keyCount > 0;
}

void ChameleonMfcDictScreen::_runStatusBarCb(Sprite& sp, int barY, int width, void* userData) {
  auto* self = static_cast<ChameleonMfcDictScreen*>(userData);
  sp.setTextDatum(TL_DATUM);
  sp.setTextColor(TFT_CYAN);
  sp.drawString(self->_runStatus, 2, barY);
  char pctBuf[8];
  snprintf(pctBuf, sizeof(pctBuf), "%d%%", self->_runPct);
  sp.setTextDatum(TR_DATUM);
  sp.setTextColor(TFT_WHITE);
  sp.drawString(pctBuf, width - 2, barY);
}

void ChameleonMfcDictScreen::_runAttack(const char* sourceLabel) {
  _state     = STATE_RUNNING;
  _running   = true;
  _recovered = 0;
  for (int s = 0; s < 40; s++) { _foundA[s] = false; _foundB[s] = false; }

  _runLog.clear();
  _runPct = 0;
  snprintf(_runStatus, sizeof(_runStatus), "Src: %s", sourceLabel);
  _runLog.addLine(_runStatus, TFT_CYAN);
  _runLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _runStatusBarCb, this);

  auto& c = ChameleonClient::get();
  c.setMode(1);

  _runLog.addLine("Scanning card...", TFT_YELLOW);
  _runLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _runStatusBarCb, this);

  uint8_t atqa[2] = {}, sak = 0;
  if (!c.scan14A(_uid, &_uidLen, atqa, &sak)) {
    _runLog.addLine("No card detected", TFT_RED);
    _runLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _runStatusBarCb, this);
    _state   = STATE_SELECT;
    _running = false;
    _loadFilePicker();
    render();
    return;
  }

  if (sak == 0x18)      _sectors = 40;
  else if (sak == 0x01) _sectors = 5;
  else                  _sectors = 16;

  char msg[64];
  snprintf(msg, sizeof(msg), "SAK %02X  %d sectors  %u keys",
           sak, (int)_sectors, (unsigned)_keyCount);
  _runLog.addLine(msg, TFT_GREEN);
  _runLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _runStatusBarCb, this);

  int totalWork = _sectors * 2;
  int progress  = 0;

  for (uint8_t s = 0; s < _sectors; s++) {
    uint8_t block      = _trailerBlock(s);
    for (int kt = 0; kt < 2; kt++) {
      uint8_t keyType   = (kt == 0) ? 0x60 : 0x61;
      char    keyTypeCh = (kt == 0) ? 'A'  : 'B';

      _runPct = (progress * 100) / totalWork;

      bool found = false;
      for (uint16_t k = 0; k < _keyCount && !found; k++) {
        snprintf(_runStatus, sizeof(_runStatus), "S%d %c %02X%02X%02X%02X%02X%02X",
                 s, keyTypeCh,
                 _keys[k][0], _keys[k][1], _keys[k][2],
                 _keys[k][3], _keys[k][4], _keys[k][5]);
        _runLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _runStatusBarCb, this);

        bool ok = c.mf1CheckKey(block, keyType, _keys[k]);

        char line[48];
        snprintf(line, sizeof(line), "S%d %c: %02X%02X%02X%02X%02X%02X",
                 s, keyTypeCh,
                 _keys[k][0], _keys[k][1], _keys[k][2],
                 _keys[k][3], _keys[k][4], _keys[k][5]);
        _runLog.addLine(line, ok ? TFT_GREEN : TFT_RED);
        _runLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _runStatusBarCb, this);

        if (ok) {
          if (kt == 0) { memcpy(_keysA[s], _keys[k], 6); _foundA[s] = true; }
          else         { memcpy(_keysB[s], _keys[k], 6); _foundB[s] = true; }
          _recovered++;
          found = true;
        }
      }

      if (!found) {
        char nf[32];
        snprintf(nf, sizeof(nf), "  S%d %c: not found", s, keyTypeCh);
        _runLog.addLine(nf, TFT_RED);
        _runLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _runStatusBarCb, this);
      }

      progress++;
    }
  }

  snprintf(msg, sizeof(msg), "Done: %d keys found", _recovered);
  strncpy(_runStatus, msg, sizeof(_runStatus) - 1);
  _runPct = 100;
  _runLog.addLine(msg, _recovered > 0 ? TFT_GREEN : TFT_RED);
  _runLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _runStatusBarCb, this);

  if (_recovered > 0) {
    _saveKeys();
    int n = Achievement.inc("chameleon_dict_attack");
    if (n == 1) Achievement.unlock("chameleon_dict_attack");
    Achievement.setMax("chameleon_mfc_keys_found", _recovered);
    if (_recovered >= 10) Achievement.unlock("chameleon_mfc_keys_found");
  }

  _buildResultRows();
  _state   = STATE_LOG_DONE;
  _running = false;
}

void ChameleonMfcDictScreen::_buildResultRows() {
  _rowCount = 0;

  char header[32];
  snprintf(header, sizeof(header), "%d / %d keys", _recovered, _sectors * 2);
  _rowLabels[_rowCount] = "Recovered";
  _rowValues[_rowCount] = header;
  _rows[_rowCount] = { _rowLabels[_rowCount].c_str(), _rowValues[_rowCount] };
  _rowCount++;

  char uidStr[20] = {};
  for (uint8_t i = 0; i < _uidLen && i * 2 + 2 < (int)sizeof(uidStr); i++) {
    char h[4]; snprintf(h, sizeof(h), "%02X", _uid[i]); strcat(uidStr, h);
  }
  _rowLabels[_rowCount] = "UID";
  _rowValues[_rowCount] = uidStr;
  _rows[_rowCount] = { _rowLabels[_rowCount].c_str(), _rowValues[_rowCount] };
  _rowCount++;

  for (uint8_t s = 0; s < _sectors && _rowCount + 1 < MAX_RESULT_ROWS; s++) {
    char lbl[12];
    char keyHex[16];

    snprintf(lbl, sizeof(lbl), "S%02d A", s);
    if (_foundA[s]) {
      snprintf(keyHex, sizeof(keyHex), "%02X%02X%02X%02X%02X%02X",
               _keysA[s][0], _keysA[s][1], _keysA[s][2],
               _keysA[s][3], _keysA[s][4], _keysA[s][5]);
    } else {
      snprintf(keyHex, sizeof(keyHex), "---");
    }
    _rowLabels[_rowCount] = lbl;
    _rowValues[_rowCount] = keyHex;
    _rows[_rowCount] = { _rowLabels[_rowCount].c_str(), _rowValues[_rowCount] };
    _rowCount++;

    snprintf(lbl, sizeof(lbl), "S%02d B", s);
    if (_foundB[s]) {
      snprintf(keyHex, sizeof(keyHex), "%02X%02X%02X%02X%02X%02X",
               _keysB[s][0], _keysB[s][1], _keysB[s][2],
               _keysB[s][3], _keysB[s][4], _keysB[s][5]);
    } else {
      snprintf(keyHex, sizeof(keyHex), "---");
    }
    _rowLabels[_rowCount] = lbl;
    _rowValues[_rowCount] = keyHex;
    _rows[_rowCount] = { _rowLabels[_rowCount].c_str(), _rowValues[_rowCount] };
    _rowCount++;
  }

  _scrollView.setRows(_rows, _rowCount);
}

void ChameleonMfcDictScreen::_saveKeys() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) return;
  Uni.Storage->makeDir("/unigeek/nfc/keys");

  char uidHex[16] = {};
  for (uint8_t i = 0; i < _uidLen && i * 2 + 2 < (int)sizeof(uidHex); i++) {
    char h[4]; snprintf(h, sizeof(h), "%02X", _uid[i]); strcat(uidHex, h);
  }
  String path = String("/unigeek/nfc/keys/") + uidHex + ".txt";
  String buf;
  for (uint8_t s = 0; s < _sectors; s++) {
    char line[48];
    if (_foundA[s]) {
      snprintf(line, sizeof(line), "S%02d A %02X%02X%02X%02X%02X%02X\n",
               s, _keysA[s][0], _keysA[s][1], _keysA[s][2],
               _keysA[s][3], _keysA[s][4], _keysA[s][5]);
      buf += line;
    }
    if (_foundB[s]) {
      snprintf(line, sizeof(line), "S%02d B %02X%02X%02X%02X%02X%02X\n",
               s, _keysB[s][0], _keysB[s][1], _keysB[s][2],
               _keysB[s][3], _keysB[s][4], _keysB[s][5]);
      buf += line;
    }
  }
  Uni.Storage->writeFile(path.c_str(), buf.c_str());
}
