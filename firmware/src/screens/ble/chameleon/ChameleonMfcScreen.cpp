#include "ChameleonMfcScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ChameleonMfcWriteScreen.h"
#include "utils/nfc/NdefParser.h"

extern "C" {
#include "utils/crypto/crapto1.h"
}

// Single helper: oddparity of a byte. Used by _isNonce below.
static uint8_t _par8(uint8_t b) {
  b ^= b >> 4; b ^= b >> 2; b ^= b >> 1; return (~b) & 1;
}

// parity check used by nested attack distance enumeration
static uint8_t _isNonce(uint32_t Nt, uint32_t NtEnc, uint32_t Ks1, const uint8_t* par) {
  return (
    (uint8_t)(_par8((Nt >> 24) & 0xFF) == (par[0] ^ _par8((NtEnc >> 24) & 0xFF) ^ BIT(Ks1, 16))) &
    (uint8_t)(_par8((Nt >> 16) & 0xFF) == (par[1] ^ _par8((NtEnc >> 16) & 0xFF) ^ BIT(Ks1,  8))) &
    (uint8_t)(_par8((Nt >>  8) & 0xFF) == (par[2] ^ _par8((NtEnc >>  8) & 0xFF) ^ BIT(Ks1,  0)))
  );
}

static constexpr uint8_t kMfcBuiltinKeys[][6] = {
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
static constexpr uint8_t kMfcBuiltinCount = sizeof(kMfcBuiltinKeys) / 6;

// ── Helpers ──

uint8_t ChameleonMfcScreen::_trailerBlock(uint8_t sector) {
  return (sector < 32) ? (sector * 4 + 3) : (128 + (sector - 32) * 16 + 15);
}

uint16_t ChameleonMfcScreen::_totalBlocks() {
  if (_sectors == 5)  return 20;
  if (_sectors == 40) return 256;
  return 64;
}

const char* ChameleonMfcScreen::title() {
  switch (_state) {
    case STATE_AUTH:               return "MF Classic";
    case STATE_MF_MENU:            return "MIFARE Classic";
    case STATE_SHOW_KEYS:          return "Discovered Keys";
    case STATE_DUMP:
    case STATE_DUMP_RESULT:        return "Dump Memory";
    case STATE_DICT_SEL:
    case STATE_DICT_RUN:
    case STATE_DICT_LOG:           return "Dictionary Attack";
    case STATE_STATIC_NESTED:
    case STATE_STATIC_NESTED_LOG:  return "Static Nested";
    case STATE_NESTED:
    case STATE_NESTED_LOG:         return "Nested Attack";
  }
  return "MIFARE Classic";
}

void ChameleonMfcScreen::onInit() {
  _callAuth();
}

void ChameleonMfcScreen::_goMfMenu() {
  _freeDump();
  _state = STATE_MF_MENU;
  setItems(_mfItems, 5);
  render();
}

// ── Status bar callbacks ──

void ChameleonMfcScreen::_authStatusBarCb(Sprite& sp, int barY, int width, void* userData) {
  auto* self = static_cast<ChameleonMfcScreen*>(userData);
  sp.setTextDatum(TL_DATUM);
  sp.setTextColor(TFT_CYAN);
  sp.drawString(self->_authStatus, 2, barY);
  char pctBuf[8];
  snprintf(pctBuf, sizeof(pctBuf), "%d%%", self->_authPct);
  sp.setTextDatum(TR_DATUM);
  sp.setTextColor(TFT_WHITE);
  sp.drawString(pctBuf, width - 2, barY);
}

void ChameleonMfcScreen::_actionStatusBarCb(Sprite& sp, int barY, int width, void* userData) {
  auto* self = static_cast<ChameleonMfcScreen*>(userData);
  sp.setTextDatum(TL_DATUM);
  sp.setTextColor(TFT_CYAN);
  sp.drawString(self->_actionStatus, 2, barY);
  char pctBuf[8];
  snprintf(pctBuf, sizeof(pctBuf), "%d%%", self->_actionPct);
  sp.setTextDatum(TR_DATUM);
  sp.setTextColor(TFT_WHITE);
  sp.drawString(pctBuf, width - 2, barY);
}

// ── Auth ──

void ChameleonMfcScreen::_callAuth() {
  _state   = STATE_AUTH;
  _running = true;
  memset(_keysA, 0, sizeof(_keysA));
  memset(_keysB, 0, sizeof(_keysB));
  memset(_foundA, 0, sizeof(_foundA));
  memset(_foundB, 0, sizeof(_foundB));
  _recovered = 0;

  _authLog.clear();
  _authPct = 0;
  strncpy(_authStatus, "Starting...", sizeof(_authStatus) - 1);
  render();

  auto& c = ChameleonClient::get();
  c.setMode(1);

  _authLog.addLine("Scanning card...", TFT_YELLOW);
  _authLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _authStatusBarCb, this);

  uint8_t atqa[2] = {}, sak = 0;
  if (!c.scan14A(_uid, &_uidLen, atqa, &sak)) {
    _authLog.addLine("No card detected", TFT_RED);
    _authLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _authStatusBarCb, this);
    delay(1200);
    c.setMode(0);
    _running = false;
    Screen.goBack();
    return;
  }

  _sak = sak;
  if (sak == 0x18)      _sectors = 40;
  else if (sak == 0x01) _sectors = 5;
  else                  _sectors = 16;

  if (!c.mf1Support()) {
    _authLog.addLine("Not MIFARE Classic", TFT_RED);
    _authLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _authStatusBarCb, this);
    delay(1200);
    c.setMode(0);
    _running = false;
    Screen.goBack();
    return;
  }

  char msg[64];
  snprintf(msg, sizeof(msg), "SAK %02X  %d sectors", sak, (int)_sectors);
  _authLog.addLine(msg, TFT_GREEN);
  _authLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _authStatusBarCb, this);

  // Initial scan only tries the FFFFFFFFFFFF default. For deeper checks the
  // user runs Dictionary Attack from the menu — keeps first-entry fast.
  static constexpr uint8_t kDefaultKey[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  int totalWork = _sectors * 2;
  int progress  = 0;

  for (uint8_t s = 0; s < _sectors; s++) {
    uint8_t block = _trailerBlock(s);
    for (int kt = 0; kt < 2; kt++) {
      uint8_t keyType   = (kt == 0) ? 0x60 : 0x61;
      char    keyTypeCh = (kt == 0) ? 'A'  : 'B';
      _authPct = (progress * 100) / totalWork;

      snprintf(_authStatus, sizeof(_authStatus), "S%d %c default", s, keyTypeCh);
      _authLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _authStatusBarCb, this);

      bool ok = c.mf1CheckKey(block, keyType, kDefaultKey);

      char line[48];
      snprintf(line, sizeof(line), "S%d %c: %s",
               s, keyTypeCh, ok ? "FFFFFFFFFFFF" : "not default");
      _authLog.addLine(line, ok ? TFT_GREEN : TFT_DARKGREY);
      _authLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _authStatusBarCb, this);

      if (ok) {
        if (kt == 0) { memcpy(_keysA[s], kDefaultKey, 6); _foundA[s] = true; }
        else         { memcpy(_keysB[s], kDefaultKey, 6); _foundB[s] = true; }
        _recovered++;
      }

      progress++;
    }
  }

  snprintf(msg, sizeof(msg), "Found %d/%d keys", _recovered, totalWork);
  strncpy(_authStatus, msg, sizeof(_authStatus) - 1);
  _authPct = 100;
  _authLog.addLine(msg, _recovered > 0 ? TFT_GREEN : TFT_YELLOW);
  _authLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _authStatusBarCb, this);

  if (_recovered > 0) {
    _saveKeys();
    int n = Achievement.inc("chameleon_dict_attack");
    if (n == 1) Achievement.unlock("chameleon_dict_attack");
    Achievement.setMax("chameleon_mfc_keys_found", _recovered);
    if (_recovered >= 10) Achievement.unlock("chameleon_mfc_keys_found");
  }

  c.setMode(0);
  _running = false;
  _goMfMenu();
}

// ── Discovered Keys ──

void ChameleonMfcScreen::_buildKeyRows() {
  _rowCount = 0;

  char uidStr[20] = {};
  for (uint8_t i = 0; i < _uidLen && i * 2 + 2 < (int)sizeof(uidStr); i++) {
    char h[4]; snprintf(h, sizeof(h), "%02X", _uid[i]); strcat(uidStr, h);
  }
  _rowLabels[_rowCount] = "UID";
  _rowValues[_rowCount] = uidStr;
  _rows[_rowCount] = { _rowLabels[_rowCount].c_str(), _rowValues[_rowCount] };
  _rowCount++;

  char sakStr[8];
  snprintf(sakStr, sizeof(sakStr), "%02X", _sak);
  _rowLabels[_rowCount] = "SAK";
  _rowValues[_rowCount] = sakStr;
  _rows[_rowCount] = { _rowLabels[_rowCount].c_str(), _rowValues[_rowCount] };
  _rowCount++;

  char recStr[16];
  snprintf(recStr, sizeof(recStr), "%d / %d", _recovered, _sectors * 2);
  _rowLabels[_rowCount] = "Keys";
  _rowValues[_rowCount] = recStr;
  _rows[_rowCount] = { _rowLabels[_rowCount].c_str(), _rowValues[_rowCount] };
  _rowCount++;

  for (uint8_t s = 0; s < _sectors && _rowCount + 1 < MAX_ROWS; s++) {
    char lbl[12], val[16];

    snprintf(lbl, sizeof(lbl), "S%02d A", s);
    if (_foundA[s])
      snprintf(val, sizeof(val), "%02X%02X%02X%02X%02X%02X",
               _keysA[s][0], _keysA[s][1], _keysA[s][2],
               _keysA[s][3], _keysA[s][4], _keysA[s][5]);
    else
      snprintf(val, sizeof(val), "---");
    _rowLabels[_rowCount] = lbl;
    _rowValues[_rowCount] = val;
    _rows[_rowCount] = { _rowLabels[_rowCount].c_str(), _rowValues[_rowCount] };
    _rowCount++;

    if (_rowCount >= MAX_ROWS) break;

    snprintf(lbl, sizeof(lbl), "S%02d B", s);
    if (_foundB[s])
      snprintf(val, sizeof(val), "%02X%02X%02X%02X%02X%02X",
               _keysB[s][0], _keysB[s][1], _keysB[s][2],
               _keysB[s][3], _keysB[s][4], _keysB[s][5]);
    else
      snprintf(val, sizeof(val), "---");
    _rowLabels[_rowCount] = lbl;
    _rowValues[_rowCount] = val;
    _rows[_rowCount] = { _rowLabels[_rowCount].c_str(), _rowValues[_rowCount] };
    _rowCount++;
  }

  _scrollView.setRows(_rows, _rowCount);
}

void ChameleonMfcScreen::_showDiscoveredKeys() {
  _state = STATE_SHOW_KEYS;
  _buildKeyRows();
  render();
}

// ── Save keys (same format as ChameleonMfcDictScreen for interop) ──

void ChameleonMfcScreen::_saveKeys() {
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
  if (buf.length() > 0)
    Uni.Storage->writeFile(path.c_str(), buf.c_str());
}

// Helper: log a line then immediately redraw the action log so the user sees it live.
void ChameleonMfcScreen::_log(const char* line, uint16_t color) {
  _actionLog.addLine(line, color);
  _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _actionStatusBarCb, this);
}


// ── Dump Memory ──

void ChameleonMfcScreen::_freeDump() {
  if (_dump) {
    free(_dump);
    _dump = nullptr;
  }
  _dumpLen = 0;
  _dumpBlocks = 0;
}

bool ChameleonMfcScreen::_extractDumpNdef(uint8_t** ndef, size_t* ndefLen) const {
  if (ndef) *ndef = nullptr;
  if (ndefLen) *ndefLen = 0;
  if (!_dump || !_dumpLen || !ndef || !ndefLen) return false;

  uint8_t ndefSectors[39] = {};
  size_t sectorCount = 0;

  auto addIfNdef = [&](uint8_t sector, uint8_t lo, uint8_t hi) {
    if (sector >= _sectors || sectorCount >= sizeof(ndefSectors)) return;
    if (lo == 0x03 && hi == 0xE1) ndefSectors[sectorCount++] = sector;
  };

  // MAD1: sector 0, blocks 1 and 2. AIDs are stored low byte first.
  if (_dumpLen >= 48) {
    const uint8_t* b1 = _dump + 16;
    const uint8_t* b2 = _dump + 32;
    for (uint8_t s = 1; s <= 7 && s < _sectors; ++s) {
      size_t off = 2 + (size_t)(s - 1) * 2;
      addIfNdef(s, b1[off], b1[off + 1]);
    }
    for (uint8_t s = 8; s <= 15 && s < _sectors; ++s) {
      size_t off = (size_t)(s - 8) * 2;
      addIfNdef(s, b2[off], b2[off + 1]);
    }
  }

  // MAD2: Classic 4K sector 16, blocks 64..66, maps sectors 17..39.
  if (_sectors > 16 && _dumpLen >= (67u * 16u)) {
    const uint8_t* m0 = _dump + 64u * 16u;
    const uint8_t* m1 = _dump + 65u * 16u;
    const uint8_t* m2 = _dump + 66u * 16u;
    for (uint8_t s = 17; s <= 23; ++s) {
      size_t off = 2 + (size_t)(s - 17) * 2;
      addIfNdef(s, m0[off], m0[off + 1]);
    }
    for (uint8_t s = 24; s <= 31; ++s) {
      size_t off = (size_t)(s - 24) * 2;
      addIfNdef(s, m1[off], m1[off + 1]);
    }
    for (uint8_t s = 32; s <= 39; ++s) {
      size_t off = (size_t)(s - 32) * 2;
      addIfNdef(s, m2[off], m2[off + 1]);
    }
  }

  if (sectorCount == 0) return false;

  size_t areaLen = 0;
  for (size_t i = 0; i < sectorCount; ++i)
    areaLen += (ndefSectors[i] < 32) ? 48u : 240u;

  uint8_t* area = (uint8_t*)malloc(areaLen);
  if (!area) return false;

  size_t out = 0;
  for (size_t i = 0; i < sectorCount; ++i) {
    const uint8_t sector = ndefSectors[i];
    const uint16_t firstBlock = (sector < 32)
                                  ? (uint16_t)sector * 4u
                                  : (uint16_t)(128u + (sector - 32u) * 16u);
    const uint8_t dataBlocks = (sector < 32) ? 3 : 15;
    for (uint8_t bi = 0; bi < dataBlocks; ++bi) {
      const size_t off = (size_t)(firstBlock + bi) * 16u;
      if (off + 16u > _dumpLen) {
        free(area);
        return false;
      }
      memcpy(area + out, _dump + off, 16);
      out += 16;
    }
  }

  size_t pos = 0;
  while (pos < out) {
    const uint8_t tlv = area[pos++];
    if (tlv == 0x00) continue;
    if (tlv == 0xFE) break;
    if (pos >= out) break;

    size_t len = area[pos++];
    if (len == 0xFF) {
      if (pos + 1 >= out) break;
      len = ((size_t)area[pos] << 8) | area[pos + 1];
      pos += 2;
    }
    if (pos + len > out) break;

    if (tlv == 0x03 && len > 0) {
      uint8_t* extracted = (uint8_t*)malloc(len);
      if (!extracted) {
        free(area);
        return false;
      }
      memcpy(extracted, area + pos, len);
      free(area);
      *ndef = extracted;
      *ndefLen = len;
      return true;
    }
    pos += len;
  }

  free(area);
  return false;
}

void ChameleonMfcScreen::_buildDumpPreview() {
  _rowCount = 0;

  auto addRow = [&](const String& label, const String& value) {
    if (_rowCount >= MAX_ROWS) return false;
    _rowLabels[_rowCount] = label;
    _rowValues[_rowCount] = value;
    _rows[_rowCount] = { _rowLabels[_rowCount].c_str(),
                         _rowValues[_rowCount].c_str() };
    ++_rowCount;
    return true;
  };

  auto addWrappedRow = [&](const String& label, const String& value) {
    if (value.length() == 0) {
      addRow(label, "");
      return;
    }

    String normalized = value;
    normalized.replace("\r\n", "\n");
    normalized.replace("\r", "\n");

    int pos = 0;
    bool first = true;
    while (pos <= (int)normalized.length()) {
      int nl = normalized.indexOf('\n', pos);
      if (nl < 0) nl = normalized.length();

      String line = normalized.substring(pos, nl);
      if (line.length() == 0) {
        addRow(first ? label : "", "");
        first = false;
      } else {
        // Keep individual rows short enough for ScrollListView while
        // preserving explicit line boundaries.
        static constexpr int kChunk = 28;
        int off = 0;
        while (off < (int)line.length()) {
          int end = min(off + kChunk, (int)line.length());
          addRow(first ? label : "", line.substring(off, end));
          first = false;
          off = end;
        }
      }

      if (nl >= (int)normalized.length()) break;
      pos = nl + 1;
    }
  };

  char uid[24] = {};
  for (uint8_t i = 0; i < _uidLen; ++i) {
    char b[4];
    snprintf(b, sizeof(b), "%02X%s", _uid[i], (i + 1 < _uidLen) ? ":" : "");
    strcat(uid, b);
  }

  const char* type = (_sectors == 5) ? "MIFARE Classic Mini"
                   : (_sectors == 40) ? "MIFARE Classic 4K"
                                      : "MIFARE Classic 1K";
  addRow("Type", type);
  addRow("UID", uid);
  addRow("Blocks", String(_dumpBlocks));
  addRow("Dump", String(_dumpLen) + " bytes");

  uint8_t* ndef = nullptr;
  size_t ndefLen = 0;
  NdefParser::Result parsed;
  if (_extractDumpNdef(&ndef, &ndefLen) && NdefParser::parse(ndef, ndefLen, parsed)) {
    switch (parsed.kind) {
      case NdefParser::RECORD_TEXT:
        addRow("NDEF", "Text");
        if (parsed.language.length()) addRow("Language", parsed.language);
        if (parsed.text.length()) addWrappedRow("Text", parsed.text);
        break;
      case NdefParser::RECORD_URL:
        addRow("NDEF", "URL");
        addRow("URL", parsed.uri);
        break;
      case NdefParser::RECORD_PHONE:
        addRow("NDEF", "Phone");
        addRow("Phone", parsed.phone);
        break;
      case NdefParser::RECORD_EMAIL:
        addRow("NDEF", "Email");
        addRow("Email", parsed.email);
        break;
      case NdefParser::RECORD_VCARD:
        addRow("NDEF", "vCard");
        if (parsed.contact.length()) addRow("Contact", parsed.contact);
        if (parsed.company.length()) addRow("Company", parsed.company);
        if (parsed.address.length()) addRow("Address", parsed.address);
        if (parsed.phone.length()) addRow("Phone", parsed.phone);
        if (parsed.email.length()) addRow("Email", parsed.email);
        if (parsed.website.length()) addRow("Website", parsed.website);
        break;
      default:
        addRow("NDEF", "Unsupported");
        break;
    }
  } else {
    addRow("NDEF", "Not found");
  }
  if (ndef) free(ndef);

  addRow("[Press]", "Actions");
  _scrollView.setRows(_rows, _rowCount);
}

void ChameleonMfcScreen::_saveDump() {
  if (!_dump || !_dumpLen || !Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Save failed", 1200);
    render();
    return;
  }

  char suggested[32] = {};
  size_t pos = snprintf(suggested, sizeof(suggested), "%s_",
                        ChameleonClient::tagTypeName(
                            _sectors == 5 ? 1000 : (_sectors == 40 ? 1003 : 1001)));
  for (uint8_t i = 0; i < _uidLen && pos + 2 < sizeof(suggested); ++i) {
    pos += snprintf(suggested + pos, sizeof(suggested) - pos, "%02X", _uid[i]);
  }

  String name = InputTextAction::popup("Save dump", suggested);
  if (InputTextAction::wasCancelled() || name.length() == 0) {
    render();
    return;
  }

  if (name.endsWith(".bin")) name.remove(name.length() - 4);
  String filename = name + ".bin";

  Uni.Storage->makeDir("/unigeek");
  Uni.Storage->makeDir("/unigeek/nfc");
  Uni.Storage->makeDir("/unigeek/nfc/dumps");

  String path = String("/unigeek/nfc/dumps/") + filename;
  fs::File f = Uni.Storage->open(path.c_str(), "w");
  bool ok = false;
  if (f) {
    ok = f.write(_dump, _dumpLen) == _dumpLen;
    f.close();
  }

  render();
  if (ok) {
    String msg = String("Saved: ") + filename;
    ShowStatusAction::show(msg.c_str(), 1500);
    int n = Achievement.inc("chameleon_mfc_dump");
    if (n == 1) Achievement.unlock("chameleon_mfc_dump");
    Screen.goBack();
    return;
  } else {
    ShowStatusAction::show("Save failed", 1200);
  }
  render();
}

void ChameleonMfcScreen::_callDump() {
  _freeDump();
  _state   = STATE_DUMP;
  _running = true;

  _actionLog.clear();
  _actionPct = 0;
  strncpy(_actionStatus, "Dumping...", sizeof(_actionStatus) - 1);
  render();

  auto& c = ChameleonClient::get();

  // _callAuth() returns the CU to emulator mode before opening the submenu.
  // Physical-card reads (2008) require reader mode, otherwise every block read
  // fails and the dump is filled with zeroes, which makes NDEF appear absent.
  c.setMode(1);

  _dumpBlocks = _totalBlocks();
  _dumpLen = (uint16_t)(_dumpBlocks * 16u);
  _dump = (uint8_t*)malloc(_dumpLen);
  if (!_dump) {
    c.setMode(0);
    _dumpLen = 0;
    _actionLog.addLine("Out of memory", TFT_RED);
    _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _actionStatusBarCb, this);
    _running = false;
    _goMfMenu();
    return;
  }

  for (uint16_t block = 0; block < _dumpBlocks; block++) {
    uint8_t s = (block < 128) ? (uint8_t)(block / 4)
                              : (uint8_t)(32 + (block - 128) / 16);

    _actionPct = (block * 100) / _dumpBlocks;

    uint8_t data[16] = {};
    bool ok = false;
    if (_foundA[s]) ok = c.mf1ReadBlock(block, 0x60, _keysA[s], data);
    if (!ok && _foundB[s]) ok = c.mf1ReadBlock(block, 0x61, _keysB[s], data);
    if (!ok) memset(data, 0, 16);

    if (block == _trailerBlock(s)) {
      if (_foundA[s]) memcpy(data, _keysA[s], 6);
      if (_foundB[s]) memcpy(data + 10, _keysB[s], 6);
    }

    memcpy(_dump + (size_t)block * 16u, data, 16);

    if ((block & 0x07) == 0) {
      char msg[48];
      snprintf(msg, sizeof(msg), "Block %u/%u",
               (unsigned)block, (unsigned)_dumpBlocks - 1u);
      snprintf(_actionStatus, sizeof(_actionStatus), "Block %u/%u",
               (unsigned)block, (unsigned)_dumpBlocks - 1u);
      _actionLog.addLine(msg, ok ? TFT_WHITE : TFT_DARKGREY);
      _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _actionStatusBarCb, this);
    }
  }

  _actionPct = 100;
  strncpy(_actionStatus, "Done", sizeof(_actionStatus) - 1);
  c.setMode(0);
  _running = false;
  _state = STATE_DUMP_RESULT;
  _buildDumpPreview();
  render();
}

// ── Dictionary Attack ──

void ChameleonMfcScreen::_loadDictPicker() {
  if (_dictPickDir.length() == 0) _dictPickDir = _kDictDir;
  _browser.root = _kDictDir;
  uint8_t n = _browser.load(this, _dictPickDir, ".txt");

  uint8_t baseOffset = 0;
  if (_dictPickDir == _kDictDir) {
    _dictItems[0] = {"Built-in keys"};
    baseOffset    = 1;
  }
  for (uint8_t i = 0; i < n; i++) _dictItems[i + baseOffset] = _browser.items()[i];
  _dictFileCount = n;
  _state = STATE_DICT_SEL;
  setItems(_dictItems, (uint8_t)(n + baseOffset));
  render();
}

static bool _parseChameleonMfcKey(const String& line, uint8_t out[6]) {
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

bool ChameleonMfcScreen::_loadDictFile(const char* path) {
  _dictKeyCount = 0;
  if (!Uni.Storage || !Uni.Storage->isAvailable()) return false;
  String content = Uni.Storage->readFile(path);
  if (content.length() == 0) return false;
  int start = 0;
  while (start < (int)content.length() && _dictKeyCount < MAX_DICT_KEYS) {
    int nl = content.indexOf('\n', start);
    if (nl < 0) nl = content.length();
    String line = content.substring(start, nl);
    uint8_t k[6];
    if (_parseChameleonMfcKey(line, k)) {
      memcpy(_dictKeys[_dictKeyCount], k, 6);
      _dictKeyCount++;
    }
    start = nl + 1;
  }
  return _dictKeyCount > 0;
}

void ChameleonMfcScreen::_runDictAttack() {
  _state   = STATE_DICT_RUN;
  _running = true;

  _actionLog.clear();
  _actionPct = 0;
  strncpy(_actionStatus, "Starting...", sizeof(_actionStatus) - 1);
  render();

  auto& c = ChameleonClient::get();
  c.setMode(1);

  int newFound  = 0;
  int totalWork = _sectors * 2;
  int progress  = 0;

  for (uint8_t s = 0; s < _sectors; s++) {
    uint8_t block = _trailerBlock(s);
    for (int kt = 0; kt < 2; kt++) {
      uint8_t keyType   = (kt == 0) ? 0x60 : 0x61;
      char    keyTypeCh = (kt == 0) ? 'A'  : 'B';

      if ((kt == 0) ? _foundA[s] : _foundB[s]) { progress++; continue; }

      _actionPct = (progress * 100) / totalWork;

      bool found = false;
      for (uint16_t k = 0; k < _dictKeyCount && !found; k++) {
        snprintf(_actionStatus, sizeof(_actionStatus), "S%d %c %02X%02X%02X%02X%02X%02X",
                 s, keyTypeCh,
                 _dictKeys[k][0], _dictKeys[k][1], _dictKeys[k][2],
                 _dictKeys[k][3], _dictKeys[k][4], _dictKeys[k][5]);
        _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _actionStatusBarCb, this);

        bool ok = c.mf1CheckKey(block, keyType, _dictKeys[k]);

        char line[48];
        snprintf(line, sizeof(line), "S%d %c: %02X%02X%02X%02X%02X%02X",
                 s, keyTypeCh,
                 _dictKeys[k][0], _dictKeys[k][1], _dictKeys[k][2],
                 _dictKeys[k][3], _dictKeys[k][4], _dictKeys[k][5]);
        _actionLog.addLine(line, ok ? TFT_GREEN : TFT_RED);
        _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _actionStatusBarCb, this);

        if (ok) {
          if (kt == 0) { memcpy(_keysA[s], _dictKeys[k], 6); _foundA[s] = true; }
          else         { memcpy(_keysB[s], _dictKeys[k], 6); _foundB[s] = true; }
          _recovered++;
          newFound++;
          found = true;
        }
      }

      if (!found) {
        char nf[32];
        snprintf(nf, sizeof(nf), "  S%d %c: not found", s, keyTypeCh);
        _actionLog.addLine(nf, TFT_RED);
        _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _actionStatusBarCb, this);
      }

      progress++;
    }
  }

  char msg[48];
  snprintf(msg, sizeof(msg), "New: %d  Total: %d keys", newFound, _recovered);
  strncpy(_actionStatus, msg, sizeof(_actionStatus) - 1);
  _actionPct = 100;
  _actionLog.addLine(msg, newFound > 0 ? TFT_GREEN : TFT_RED);
  _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _actionStatusBarCb, this);

  if (newFound > 0) {
    _saveKeys();
    int n = Achievement.inc("chameleon_dict_attack");
    if (n == 1) Achievement.unlock("chameleon_dict_attack");
    Achievement.setMax("chameleon_mfc_keys_found", _recovered);
    if (_recovered >= 10) Achievement.unlock("chameleon_mfc_keys_found");
  }

  c.setMode(0);
  _running = false;
  _state   = STATE_DICT_LOG;
}

// ── Static Nested Attack ─────────────────────────────────────────────────────

void ChameleonMfcScreen::_callStaticNested() {
  _state   = STATE_STATIC_NESTED;
  _running = true;
  _actionLog.clear();
  _actionPct = 0;
  strncpy(_actionStatus, "Init...", sizeof(_actionStatus) - 1);
  render();

  auto& c = ChameleonClient::get();
  c.setMode(1);

  char m[64];

  // ── Find a known key to use as exploit credential ─────────────────────────
  int knownSec = -1;
  uint8_t knownKType = 0;
  uint64_t knownKey64 = 0;
  for (uint8_t s = 0; s < _sectors && knownSec < 0; s++) {
    if (_foundA[s]) {
      knownSec = s; knownKType = 0x60;
      for (int i = 0; i < 6; i++) knownKey64 = (knownKey64 << 8) | _keysA[s][i];
    } else if (_foundB[s]) {
      knownSec = s; knownKType = 0x61;
      for (int i = 0; i < 6; i++) knownKey64 = (knownKey64 << 8) | _keysB[s][i];
    }
  }
  if (knownSec < 0) {
    _log("No known key to exploit", TFT_RED);
    c.setMode(0);
    _running = false; _state = STATE_STATIC_NESTED_LOG; return;
  }
  snprintf(m, sizeof(m), "Exploit: S%d %c key=%012llX",
           knownSec, knownKType == 0x60 ? 'A' : 'B', (unsigned long long)knownKey64);
  _log(m, TFT_CYAN);

  // ── Confirm static nonce via mf1NTLevel (1=static, 2=weak, 3=hard) ────────
  uint8_t ntLevel = 0;
  if (!c.mf1NTLevel(&ntLevel) || ntLevel != 1) {
    snprintf(m, sizeof(m), "Not a static-nonce card (NTLevel=%d) — abort", (int)ntLevel);
    _log(m, ntLevel == 0 ? TFT_RED : TFT_YELLOW);
    c.setMode(0);
    _running = false; _state = STATE_STATIC_NESTED_LOG; return;
  }
  _log("NTLevel=1: static nonce confirmed", TFT_GREEN);

  uint8_t exploitBlock = _trailerBlock((uint8_t)knownSec);
  uint8_t knownKeyBytes[6];
  { uint64_t tmp = knownKey64;
    for (int i = 5; i >= 0; i--) { knownKeyBytes[i] = (uint8_t)(tmp & 0xFF); tmp >>= 8; } }

  uint32_t uid32 = 0;
  for (int i = 0; i < 4 && i < (int)_uidLen; i++)
    uid32 = (uid32 << 8) | _uid[i];
  snprintf(m, sizeof(m), "uid32 = %08lX", (unsigned long)uid32);
  _log(m, TFT_DARKGREY);

  int newKeys = 0;
  int totalTargets = 0;
  for (uint8_t s = 0; s < _sectors; s++)
    for (int kt = 0; kt < 2; kt++)
      if (!((kt == 0) ? _foundA[s] : _foundB[s]))
        totalTargets++;
  int done = 0;

  // ── Attack each unknown sector/key ────────────────────────────────────────
  for (uint8_t targetSec = 0; targetSec < _sectors; targetSec++) {
    for (int kt = 0; kt < 2; kt++) {
      uint8_t tKType   = (kt == 0) ? 0x60 : 0x61;
      char    tkc      = (kt == 0) ? 'A'  : 'B';
      uint8_t tBlock   = _trailerBlock(targetSec);

      if ((kt == 0) ? _foundA[targetSec] : _foundB[targetSec]) { done++; continue; }
      if ((int)targetSec == knownSec && tKType == knownKType)    { done++; continue; }

      _actionPct = totalTargets ? (done * 100) / totalTargets : 0;
      snprintf(_actionStatus, sizeof(_actionStatus), "S%d %c collect", targetSec, tkc);

      snprintf(m, sizeof(m), "──── target S%d %c block=%d ────",
               targetSec, tkc, (int)tBlock);
      _log(m, TFT_CYAN);

      // Firmware-side static-nested acquisition (cmd 2003) — silent retry,
      // single status redraw on success.
      ChameleonClient::NestedSample samples[2];
      int gotN = 0;
      bool collected = false;
      for (int attempt = 0; attempt < 3 && !collected; attempt++) {
        if (c.mf1StaticNestedAcquire(knownKType, exploitBlock, knownKeyBytes,
                                     tKType, tBlock, nullptr, samples,
                                     2, &gotN) && gotN >= 1) {
          collected = true;
          if (totalTargets) {
            _actionPct = (done * 100) / totalTargets + (100 / (2 * totalTargets));
            if (_actionPct > 100) _actionPct = 100;
          }
          snprintf(_actionStatus, sizeof(_actionStatus), "S%d %c acq", targetSec, tkc);
          _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(),
                          _actionStatusBarCb, this);
        }
      }
      if (!collected) {
        snprintf(m, sizeof(m), "S%d %c: acquire failed (firmware)", targetSec, tkc);
        _log(m, TFT_RED);
        done++; continue;
      }

      uint32_t staticNt = samples[0].nt;
      uint32_t encNt2   = samples[0].ntEnc;
      uint32_t ks       = encNt2 ^ staticNt;

      snprintf(_actionStatus, sizeof(_actionStatus), "S%d %c recover", targetSec, tkc);
      _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _actionStatusBarCb, this);

      Crypto1State* revstate = lfsr_recovery32(ks, staticNt ^ uid32);
      if (!revstate) {
        snprintf(m, sizeof(m), "S%d %c: lfsr null (Nt=%08lX ks=%08lX)",
                 targetSec, tkc, (unsigned long)staticNt, (unsigned long)ks);
        _log(m, TFT_RED);
        done++; continue;
      }

      // Count candidates produced (no per-candidate logging — that thrashes
      // the screen on cards with thousands of candidate states).
      int candCount = 0;
      for (Crypto1State* p = revstate; p->odd != 0 || p->even != 0; p++) candCount++;

      bool found = false;
      bool capped = false;
      Crypto1State* rs = revstate;
      int checked = 0, verified = 0;
      while ((rs->odd != 0 || rs->even != 0) && !found) {
        lfsr_rollback_word(rs, staticNt ^ uid32, 0);
        uint64_t candKey64;
        crypto1_get_lfsr(rs, &candKey64);

        Crypto1State* test = crypto1_create(candKey64);
        crypto1_word(test, uid32 ^ staticNt, 0);
        uint32_t testKs = crypto1_word(test, 0, 0);
        crypto1_destroy(test);
        bool softOk = ((encNt2 ^ staticNt) == testKs);

        if (softOk) {
          uint8_t candBytes[6];
          uint64_t tmp = candKey64;
          for (int i = 5; i >= 0; i--) { candBytes[i] = (uint8_t)(tmp & 0xFF); tmp >>= 8; }
          verified++;

          if (c.mf1CheckKey(tBlock, tKType, candBytes)) {
            if (kt == 0) { memcpy(_keysA[targetSec], candBytes, 6); _foundA[targetSec] = true; }
            else         { memcpy(_keysB[targetSec], candBytes, 6); _foundB[targetSec] = true; }
            _recovered++; newKeys++; found = true;
          }
        }

        rs++;
        if (++checked > 100000) { capped = true; break; }
      }
      free(revstate);

      // ── Per-target summary line ──
      if (found) {
        uint8_t* k = (kt == 0) ? _keysA[targetSec] : _keysB[targetSec];
        snprintf(m, sizeof(m),
                 "S%d %c: KEY %02X%02X%02X%02X%02X%02X (cand=%d soft=%d)",
                 targetSec, tkc, k[0], k[1], k[2], k[3], k[4], k[5],
                 candCount, verified);
        _log(m, TFT_GREEN);
      } else {
        snprintf(m, sizeof(m), "S%d %c: no key (cand=%d soft=%d%s)",
                 targetSec, tkc, candCount, verified, capped ? " CAPPED" : "");
        _log(m, TFT_RED);
      }
      done++;
    }
  }

  snprintf(m, sizeof(m), "Static nested done: %d new keys", newKeys);
  strncpy(_actionStatus, m, sizeof(_actionStatus) - 1);
  _actionPct = 100;
  _log(m, newKeys > 0 ? TFT_GREEN : TFT_YELLOW);

  if (newKeys > 0) {
    _saveKeys();
    int n = Achievement.inc("chameleon_static_nested");
    if (n == 1) Achievement.unlock("chameleon_static_nested");
    Achievement.setMax("chameleon_mfc_keys_found", _recovered);
    if (_recovered >= 10) Achievement.unlock("chameleon_mfc_keys_found");
  }

  c.setMode(0);
  _running = false;
  _state = STATE_STATIC_NESTED_LOG;
}

// ── Nested Attack ─────────────────────────────────────────────────────────────

void ChameleonMfcScreen::_callNestedAttack() {
  _state   = STATE_NESTED;
  _running = true;
  _actionLog.clear();
  _actionPct = 0;
  strncpy(_actionStatus, "Init...", sizeof(_actionStatus) - 1);
  render();

  auto& c = ChameleonClient::get();
  c.setMode(1);

  char m[80];

  // ── Find exploit key ──────────────────────────────────────────────────────
  int knownSec = -1;
  uint8_t knownKType = 0;
  uint64_t knownKey64 = 0;
  for (uint8_t s = 0; s < _sectors && knownSec < 0; s++) {
    if (_foundA[s]) {
      knownSec = s; knownKType = 0x60;
      for (int i = 0; i < 6; i++) knownKey64 = (knownKey64 << 8) | _keysA[s][i];
    } else if (_foundB[s]) {
      knownSec = s; knownKType = 0x61;
      for (int i = 0; i < 6; i++) knownKey64 = (knownKey64 << 8) | _keysB[s][i];
    }
  }
  if (knownSec < 0) {
    _log("No known key to exploit", TFT_RED);
    c.setMode(0);
    _running = false; _state = STATE_NESTED_LOG; return;
  }
  snprintf(m, sizeof(m), "Exploit: S%d %c key=%012llX",
           knownSec, knownKType == 0x60 ? 'A' : 'B', (unsigned long long)knownKey64);
  _log(m, TFT_CYAN);

  // ── PRNG check (must be dynamic for nested attack) ────────────────────────
  uint8_t ntLevel = 0;
  if (c.mf1NTLevel(&ntLevel)) {
    snprintf(m, sizeof(m), "NTLevel=%d %s", (int)ntLevel,
             ntLevel == 1 ? "(static — use Static Nested!)" :
             ntLevel == 2 ? "(weak PRNG — OK)" :
             ntLevel == 3 ? "(hardened — likely fail)" : "(unknown)");
    _log(m, ntLevel == 2 ? TFT_GREEN : TFT_YELLOW);
  }

  uint32_t uid32 = 0;
  for (int i = 0; i < 4 && i < (int)_uidLen; i++)
    uid32 = (uid32 << 8) | _uid[i];
  snprintf(m, sizeof(m), "uid32 = %08lX", (unsigned long)uid32);
  _log(m, TFT_DARKGREY);

  uint8_t exploitBlock = _trailerBlock((uint8_t)knownSec);
  uint8_t knownKeyBytes[6];
  { uint64_t tmp = knownKey64;
    for (int i = 5; i >= 0; i--) { knownKeyBytes[i] = (uint8_t)(tmp & 0xFF); tmp >>= 8; } }

  struct NestedSample { uint32_t nt1, encNt2; uint8_t par[3]; };
  static constexpr int COLLECT_NR = 3;
  NestedSample samples[COLLECT_NR];
  int newKeys = 0;

  int totalTargets = 0;
  for (uint8_t s = 0; s < _sectors; s++)
    for (int kt = 0; kt < 2; kt++)
      if (!((kt == 0) ? _foundA[s] : _foundB[s]))
        totalTargets++;
  int done = 0;

  for (uint8_t targetSec = 0; targetSec < _sectors; targetSec++) {
    for (int kt = 0; kt < 2; kt++) {
      uint8_t tKType  = (kt == 0) ? 0x60 : 0x61;
      char    tkc     = (kt == 0) ? 'A'  : 'B';
      uint8_t tBlock  = _trailerBlock(targetSec);

      if ((kt == 0) ? _foundA[targetSec] : _foundB[targetSec]) { done++; continue; }
      if ((int)targetSec == knownSec && tKType == knownKType)    { done++; continue; }

      _actionPct = totalTargets ? (done * 100) / totalTargets : 0;

      snprintf(m, sizeof(m), "──── target S%d %c block=%d ────",
               targetSec, tkc, (int)tBlock);
      _log(m, TFT_CYAN);

      // ── Firmware-side nested acquisition (cmd 2006) ──
      // Each call returns multiple {nt, ntEnc, par} records in one BLE round
      // trip. We retry up to 4 times to gather at least COLLECT_NR samples.
      // No per-sample log/render here — that thrashes the screen. We tick the
      // status bar once per attempt and emit a single summary line at the end.
      int collected = 0;
      for (int attempt = 0; attempt < 4 && collected < COLLECT_NR; attempt++) {
        ChameleonClient::NestedSample fw[8];
        int got = 0;
        if (!c.mf1NestedAcquire(knownKType, exploitBlock, knownKeyBytes,
                                tKType, tBlock, fw, 8, &got) || got == 0) {
          continue;
        }
        for (int i = 0; i < got && collected < COLLECT_NR; i++) {
          samples[collected].nt1    = fw[i].nt;
          samples[collected].encNt2 = fw[i].ntEnc;
          // Firmware packs 4 parity-error bits into low nibble: bit3=byte0 .. bit0=byte3.
          // _isNonce only consumes bits 0..2 (= bytes 0,1,2 of encNt2).
          samples[collected].par[0] = (fw[i].par >> 3) & 1;
          samples[collected].par[1] = (fw[i].par >> 2) & 1;
          samples[collected].par[2] = (fw[i].par >> 1) & 1;
          collected++;
          if (totalTargets) {
            int sub = (collected * 100) / (COLLECT_NR * totalTargets);
            _actionPct = (done * 100) / totalTargets + sub;
            if (_actionPct > 100) _actionPct = 100;
          }
        }
        snprintf(_actionStatus, sizeof(_actionStatus), "S%d %c acq %d/%d",
                 targetSec, tkc, collected, COLLECT_NR);
        _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(),
                        _actionStatusBarCb, this);
      }

      if (collected == 0) {
        snprintf(m, sizeof(m), "S%d %c: no samples after %d attempts",
                 targetSec, tkc, 4);
        _log(m, TFT_RED);
        done++; continue;
      }

      // ── Enumerate 65535 PRNG distances using parity-disambiguating isNonce ──
      // Match details are summarized after the loop instead of logged per-hit.
      bool found = false;
      int matches = 0, recoveries = 0, recNull = 0;
      uint32_t firstMatchD = 0xFFFFFFFFu;
      uint32_t winningD    = 0;
      uint32_t lastTick    = 0;

      for (uint32_t d = 0; d < 65535 && !found; d++) {
        if ((d - lastTick) >= 8000) {
          lastTick = d;
          snprintf(_actionStatus, sizeof(_actionStatus),
                   "S%d %c d=%lu m=%d r=%d", targetSec, tkc,
                   (unsigned long)d, matches, recoveries);
          _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(),
                          _actionStatusBarCb, this);
        }

        uint32_t nt2_0  = prng_successor(samples[0].nt1, d);
        uint32_t ks1_0  = samples[0].encNt2 ^ nt2_0;
        if (!_isNonce(nt2_0, samples[0].encNt2, ks1_0, samples[0].par)) continue;

        bool allMatch = true;
        for (int i = 1; i < collected && allMatch; i++) {
          uint32_t nt2_i = prng_successor(samples[i].nt1, d);
          uint32_t ks1_i = samples[i].encNt2 ^ nt2_i;
          if (!_isNonce(nt2_i, samples[i].encNt2, ks1_i, samples[i].par)) allMatch = false;
        }
        if (!allMatch) continue;

        matches++;
        if (firstMatchD == 0xFFFFFFFFu) firstMatchD = d;

        Crypto1State* revstate = lfsr_recovery32(ks1_0, nt2_0 ^ uid32);
        if (!revstate) { recNull++; continue; }
        recoveries++;

        Crypto1State* rs = revstate;
        int checked = 0;
        while ((rs->odd != 0 || rs->even != 0) && !found) {
          lfsr_rollback_word(rs, nt2_0 ^ uid32, 0);
          uint64_t candKey64;
          crypto1_get_lfsr(rs, &candKey64);

          bool softOk = true;
          for (int i = 1; i < collected && softOk; i++) {
            uint32_t nt2_i = prng_successor(samples[i].nt1, d);
            Crypto1State* test = crypto1_create(candKey64);
            crypto1_word(test, uid32 ^ nt2_i, 0);
            uint32_t testKs = crypto1_word(test, 0, 0);
            crypto1_destroy(test);
            if ((samples[i].encNt2 ^ nt2_i) != testKs) softOk = false;
          }

          if (softOk) {
            uint8_t candBytes[6];
            uint64_t tmp = candKey64;
            for (int i = 5; i >= 0; i--) { candBytes[i] = (uint8_t)(tmp & 0xFF); tmp >>= 8; }

            if (c.mf1CheckKey(tBlock, tKType, candBytes)) {
              if (kt == 0) { memcpy(_keysA[targetSec], candBytes, 6); _foundA[targetSec] = true; }
              else         { memcpy(_keysB[targetSec], candBytes, 6); _foundB[targetSec] = true; }
              _recovered++; newKeys++; found = true; winningD = d;
            }
          }

          rs++;
          if (++checked > 100000) break;
        }
        free(revstate);
      }

      // ── Per-target summary: one line in either outcome ──
      if (found) {
        // candBytes is no longer in scope here; rebuild from the stored key.
        uint8_t* k = (kt == 0) ? _keysA[targetSec] : _keysB[targetSec];
        snprintf(m, sizeof(m),
                 "S%d %c: KEY %02X%02X%02X%02X%02X%02X (d=%lu m=%d r=%d)",
                 targetSec, tkc, k[0], k[1], k[2], k[3], k[4], k[5],
                 (unsigned long)winningD, matches, recoveries);
        _log(m, TFT_GREEN);
      } else {
        snprintf(m, sizeof(m),
                 "S%d %c: no key (col=%d m=%d r=%d null=%d firstD=%lu)",
                 targetSec, tkc, collected, matches, recoveries, recNull,
                 firstMatchD == 0xFFFFFFFFu ? 0UL : (unsigned long)firstMatchD);
        _log(m, TFT_RED);
      }
      done++;
    }
  }

  snprintf(m, sizeof(m), "Nested done: %d new keys", newKeys);
  strncpy(_actionStatus, m, sizeof(_actionStatus) - 1);
  _actionPct = 100;
  _log(m, newKeys > 0 ? TFT_GREEN : TFT_YELLOW);

  if (newKeys > 0) {
    _saveKeys();
    int n = Achievement.inc("chameleon_nested_attack");
    if (n == 1) Achievement.unlock("chameleon_nested_attack");
    Achievement.setMax("chameleon_mfc_keys_found", _recovered);
    if (_recovered >= 10) Achievement.unlock("chameleon_mfc_keys_found");
  }

  c.setMode(0);
  _running = false;
  _state = STATE_NESTED_LOG;
}

// ── Navigation ──

void ChameleonMfcScreen::onUpdate() {
  if (_running) return;

  if (_state == STATE_SHOW_KEYS) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) { _goMfMenu(); return; }
      _scrollView.onNav(dir);
    }
    return;
  }

  if (_state == STATE_DUMP_RESULT) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        _goMfMenu();
        return;
      }
      if (dir == INavigation::DIR_PRESS) {
        static const InputSelectAction::Option opts[] = {
          {"Save Dump to File", "save"},
          {"Write to Tag",       "write"},
        };
        const char* r = InputSelectAction::popup("Dump Actions", opts, 2, nullptr);
        if (!r) { render(); return; }
        if (strcmp(r, "save") == 0) {
          _saveDump();
        } else {
          if (_dumpLen != 1024) {
            render();
            ShowStatusAction::show("Classic 1K only for now", 1500);
            render();
          } else {
            Screen.push(new ChameleonMfcWriteScreen(_dump, _dumpLen));
          }
        }
        return;
      }
      _scrollView.onNav(dir);
    }
    return;
  }

  if (_state == STATE_DUMP) return;

  if (_state == STATE_DICT_LOG ||
      _state == STATE_STATIC_NESTED_LOG ||
      _state == STATE_NESTED_LOG) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        _goMfMenu(); return;
      }
      if (dir == INavigation::DIR_UP)   _actionLog.scroll(1);
      if (dir == INavigation::DIR_DOWN) _actionLog.scroll(-1);
      _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _actionStatusBarCb, this);
    }
    return;
  }

  ListScreen::onUpdate();
}

void ChameleonMfcScreen::onRender() {
  if (_state == STATE_AUTH) {
    _authLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _authStatusBarCb, this);
    return;
  }
  if (_state == STATE_SHOW_KEYS || _state == STATE_DUMP_RESULT) {
    _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  if (_state == STATE_DUMP          || _state == STATE_DICT_RUN         ||
      _state == STATE_DICT_LOG      || _state == STATE_STATIC_NESTED     ||
      _state == STATE_STATIC_NESTED_LOG || _state == STATE_NESTED        ||
      _state == STATE_NESTED_LOG) {
    _actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _actionStatusBarCb, this);
    return;
  }
  ListScreen::onRender();
}

void ChameleonMfcScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_MF_MENU) {
    switch (index) {
      case 0: _showDiscoveredKeys();  break;
      case 1: _callDump();            break;
      case 2: _loadDictPicker();      break;
      case 3: _callStaticNested();    break;
      case 4: _callNestedAttack();    break;
    }
  } else if (_state == STATE_DICT_SEL) {
    uint8_t baseOffset = (_dictPickDir == _kDictDir) ? 1 : 0;
    if (baseOffset && index == 0) {
      _dictKeyCount = kMfcBuiltinCount;
      memcpy(_dictKeys, kMfcBuiltinKeys, kMfcBuiltinCount * 6);
    } else {
      uint8_t fi = index - baseOffset;
      if (fi >= _browser.count()) return;
      const auto& e = _browser.entry(fi);
      if (e.isDir) {
        _dictPickDir = e.path;
        _loadDictPicker();
        return;
      }
      if (!_loadDictFile(e.path.c_str())) {
        ShowStatusAction::show("Load keys failed", 1200);
        render();
        return;
      }
    }
    if (_dictKeyCount == 0) {
      ShowStatusAction::show("No keys in source", 1200);
      render();
      return;
    }
    _runDictAttack();
  }
}

void ChameleonMfcScreen::onBack() {
  switch (_state) {
    case STATE_MF_MENU:
      Screen.goBack(); break;
    case STATE_DICT_SEL: {
      // Clamp at _kDictDir — never climb above /unigeek/nfc/dictionaries.
      if (_dictPickDir == _kDictDir || _dictPickDir.length() == 0) {
        _dictPickDir = "";
        _goMfMenu();
        return;
      }
      int slash = _dictPickDir.lastIndexOf('/');
      _dictPickDir = (slash > 0) ? _dictPickDir.substring(0, slash) : _kDictDir;
      _loadDictPicker();
      return;
    }
    case STATE_SHOW_KEYS:
    case STATE_STATIC_NESTED_LOG:
    case STATE_NESTED_LOG:
      _goMfMenu(); break;
    default:
      Screen.goBack(); break;
  }
}
