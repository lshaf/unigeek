#include "PN532I2cScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/PinConfigManager.h"
#include "core/AchievementManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/views/ProgressView.h"
#include "../../utils/nfc/NdefBuilder.h"
#include "../../utils/nfc/NdefParser.h"

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
    case STATE_SCAN_RESULT:
    case STATE_SCAN_14A:        return "HF Reader";
    case STATE_MIFARE_MENU:     return "MIFARE Classic";
    case STATE_MIFARE_DUMP:     return "Memory Dump";
    case STATE_MIFARE_KEYS:     return "Discovered Keys";
    case STATE_DICT_SELECT:     return "Dictionary Attack";
    case STATE_ULTRALIGHT_MENU: return "Ultralight / NTAG";
    case STATE_MAGIC_MENU:      return "Magic Card";
    case STATE_RAW_RESULT:      return "Result";
    case STATE_EMULATE:         return "Emulate Card";
    case STATE_NTAG_MENU:       return "Emulate NDEF";
    case STATE_NDEF_WRITE_MENU: return "Write NDEF";
    case STATE_NDEF_RESULT:     return "NDEF Result";
    case STATE_NDEF_FILE_SELECT:return "NDEF Files";
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
  if (_state == STATE_NDEF_RESULT) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        if (_ndefWritePreview) {
          bool fromFile = _ndefWritePreviewFromFile;
          _ndefWritePreview = false;
          _ndefWritePreviewFromFile = false;
          if (fromFile) _doWriteNdefFromFile();
          else _goNdefParent();
        } else {
          _goNdefParent();
        }
      } else if (dir == INavigation::DIR_PRESS && _hasNdef) {
        if (_ndefWritePreview) {
          bool ok = _writeNdefRecord(_ndefBuf, _ndefLen);
          if (ok) {
            _ndefWritePreview = false;
            _ndefWritePreviewFromFile = false;
            _ndefPickDir = "";
            _goNdefParent();
          } else {
            render();
          }
        } else {
          _showNdefActions();
        }
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
      _state == STATE_RAW_RESULT || _state == STATE_NDEF_RESULT ||
      _state == STATE_EMULATE) {
    _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  ListScreen::onRender();
}

void PN532I2cScreen::onItemSelected(uint8_t index) {
  switch (_state) {
    case STATE_MAIN_MENU:
      switch (index) {
        case 0: _doScan14A();         break;
        case 1: _goMifare();          break;
        case 2: _goUltralight();      break;
        case 3: _goMagic();           break;
        case 4: _showFirmwareInfo();  break;
      }
      break;
    case STATE_MIFARE_MENU:
      switch (index) {
        case 0:
          _ndefTarget = NDEF_TARGET_MIFARE_CLASSIC;
          _doReadClassicNdef();
          break;
        case 1:
          _ndefTarget = NDEF_TARGET_MIFARE_CLASSIC;
          _goNdefWrite();
          break;
        case 2:
          _ndefTarget = NDEF_TARGET_MIFARE_CLASSIC;
          _doEraseClassicNdef();
          break;
        case 3: _doAuthenticate();     break;
        case 4: _doDumpMemory();       break;
        case 5: _doShowKeys();         break;
        case 6: _doDictionaryPicker(); break;
      }
      break;
    case STATE_ULTRALIGHT_MENU:
      switch (index) {
        case 0:
          _ndefTarget = NDEF_TARGET_ULTRALIGHT;
          _doReadNdef();
          break;
        case 1:
          _ndefTarget = NDEF_TARGET_ULTRALIGHT;
          _goNdefWrite();
          break;
        case 2:
          _ndefTarget = NDEF_TARGET_ULTRALIGHT;
          _doEraseNdef();
          break;
        case 3: _doUltralightDump();  break;
        case 4: _doUltralightWrite(); break;
      }
      break;
    case STATE_NDEF_WRITE_MENU:
      if (index == 0) _doWriteNdefText();
      else if (index == 1) _doWriteNdefUrl();
      else if (index == 2) _doWriteNdefPhone();
      else if (index == 3) _doWriteNdefEmail();
      else if (index == 4) _doWriteNdefVcard();
      else if (index == 5) _doWriteNdefFromFile();
      break;
    case STATE_NDEF_FILE_SELECT:
      _doWriteNdefFileSelected(index);
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
      if (_dictPickDir == _dictPath || _dictPickDir.length() == 0) {
        _dictPickDir = "";
        _goMifare();
      } else {
        int slash = _dictPickDir.lastIndexOf('/');
        _dictPickDir = (slash > 0) ? _dictPickDir.substring(0, slash) : _dictPath;
        _doDictionaryPicker();
      }
      break;
    case STATE_NTAG_MENU:
      _goMain();
      break;
    case STATE_NDEF_WRITE_MENU:
      _goNdefParent();
      break;
    case STATE_NDEF_RESULT:
      if (_ndefWritePreview) {
        bool fromFile = _ndefWritePreviewFromFile;
        _ndefWritePreview = false;
        _ndefWritePreviewFromFile = false;
        if (fromFile) _doWriteNdefFromFile();
        else _goNdefParent();
      } else {
        _goNdefParent();
      }
      break;
    case STATE_NDEF_FILE_SELECT:
      if (_ndefPickDir == _ndefPath || _ndefPickDir.length() == 0) {
        _ndefPickDir = "";
        _goNdefWrite();
      } else {
        int slash = _ndefPickDir.lastIndexOf('/');
        String parent = (slash > 0) ? _ndefPickDir.substring(0, slash) : String(_ndefPath);
        if (!parent.startsWith(_ndefPath)) parent = _ndefPath;
        _ndefPickDir = parent;
        _doWriteNdefFromFile();
      }
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

  // Re-begin ExI2C on the currently-configured pins so runtime ext_sda/ext_scl
  // changes from Settings → Pin take effect without a reboot.
  if (Uni.ExI2C) {
    int sda = PinConfig.getInt(PIN_CONFIG_EXT_SDA, PIN_CONFIG_EXT_SDA_DEFAULT);
    int scl = PinConfig.getInt(PIN_CONFIG_EXT_SCL, PIN_CONFIG_EXT_SCL_DEFAULT);
    Uni.ExI2C->begin(sda, scl);

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
      ProgressView::finish();
      _ready   = true;
      int n = Achievement.inc("pn532_i2c_first_use");
      if (n == 1) Achievement.unlock("pn532_i2c_first_use");
      return true;
    }
    delete _nfc; _nfc = nullptr;
    Uni.ExI2C->end();  // free pins for next consumer
  }

  // Fall back to InI2C
  if (Uni.InI2C) {
#ifdef DEVICE_PN532_REINIT_IN_I2C
    // This board requires a clean I2C restart before probing its on-board PN532.
    // Do not do this generically: InI2C may be shared with internal peripherals.
    Uni.InI2C->end();
    Uni.InI2C->begin();
#endif

#ifdef DEVICE_HAS_PN532_POWER_CONTROL
    // Wake the on-board PN532 only when we actually fall back to the internal bus.
    // RSTPDN release is quick; 10 ms leaves a conservative startup margin.
    digitalWrite(PN532_RESET_PIN, HIGH);
    delay(10);
#endif

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
      ProgressView::finish();
      _ready   = true;
      int n = Achievement.inc("pn532_i2c_first_use");
      if (n == 1) Achievement.unlock("pn532_i2c_first_use");
      return true;
    }
    delete _nfc; _nfc = nullptr;
#ifdef DEVICE_HAS_PN532_POWER_CONTROL
    // A failed internal probe must not leave the on-board PN532 awake.
    digitalWrite(PN532_RESET_PIN, LOW);
#endif
  }

  ShowStatusAction::show("PN532 I2C not found");
  Screen.goBack();
  return false;
}

void PN532I2cScreen::_cleanup() {
  delete _nfc; _nfc = nullptr;
  // Release ExI2C so a later screen can re-begin with a different pin set.
  // InI2C is shared with the PMIC / on-board peripherals — never end() it.
  if (_wire && _wire == Uni.ExI2C) _wire->end();
#ifdef DEVICE_HAS_PN532_POWER_CONTROL
  if (_wire && _wire == Uni.InI2C) digitalWrite(PN532_RESET_PIN, LOW);
#endif
  _wire    = nullptr;
  _busName = nullptr;
  _ready   = false;
  _hasCard = false;
  _mfKeys.fill({});
}

// ── nav helpers ────────────────────────────────────────────────────────────

void PN532I2cScreen::_goMain() {
  _state = STATE_MAIN_MENU;
  setItems(_mainItems, 5);
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

void PN532I2cScreen::_goNdefWrite() {
  _ndefWritePreview = false;
  _ndefWritePreviewFromFile = false;
  _state = STATE_NDEF_WRITE_MENU;
  setItems(_ndefWriteItems, 6);
  render();
}

void PN532I2cScreen::_goNdefParent() {
  if (_ndefTarget == NDEF_TARGET_MIFARE_CLASSIC) _goMifare();
  else _goUltralight();
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


void PN532I2cScreen::_pushWrappedRow(const String& label, const String& value) {
  if (value.length() == 0) {
    _pushRow(label, "");
    return;
  }

  // ScrollListView uses the built-in size-1 font. Keep enough room for the
  // label on the first row; continuation rows can use almost the full width.
  // Using conservative character widths avoids overlap/clipping without
  // changing ScrollListView globally.
  int totalChars = (bodyW() - 10) / 6;
  if (totalChars < 12) totalChars = 12;

  int firstChars = totalChars - (int)label.length() - 2;
  if (firstChars < 8) firstChars = 8;

  int pos = 0;
  bool first = true;

  while (pos < (int)value.length() && _rowCount < MAX_ROWS) {
    while (pos < (int)value.length() &&
           (value[pos] == ' ' || value[pos] == '\n' || value[pos] == '\r' || value[pos] == '\t')) {
      pos++;
    }
    if (pos >= (int)value.length()) break;

    const int maxChars = first ? firstChars : totalChars;
    int end = pos + maxChars;
    if (end > (int)value.length()) end = value.length();

    // Respect explicit newlines.
    int newline = value.indexOf('\n', pos);
    if (newline >= pos && newline < end) {
      end = newline;
    } else if (end < (int)value.length()) {
      // Prefer a word boundary. For URLs / very long tokens there may be no
      // spaces, in which case the hard character boundary is used.
      int split = -1;
      for (int i = end; i > pos; --i) {
        if (value[i - 1] == ' ' || value[i - 1] == '\t') {
          split = i - 1;
          break;
        }
      }
      if (split > pos) end = split;
    }

    if (end <= pos) end = min(pos + maxChars, (int)value.length());

    String part = value.substring(pos, end);
    part.trim();
    _pushRow(first ? label : "", part);

    pos = end;
    first = false;

    // Skip whitespace/newline consumed at the wrap point.
    while (pos < (int)value.length() &&
           (value[pos] == ' ' || value[pos] == '\n' || value[pos] == '\r' || value[pos] == '\t')) {
      pos++;
    }
  }
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


const char* PN532I2cScreen::_inferType2Variant() {
  if (_sak != 0x00 || !_nfc || !_wire) return nullptr;

  // Keep the PN532 path aligned with the Chameleon Type-2 detector:
  //   1) GET_VERSION for NTAG21x / Ultralight EV1
  //   2) AUTH probe for legacy Ultralight C
  //   3) page-boundary probe for original 16-page Ultralight
  //
  // InDataExchange can be unreliable for Type-2 proprietary commands on some
  // PN532/library combinations, so each probe may fall back to
  // InCommunicateThru.

  auto type2Exchange = [&](const uint8_t* tx, uint8_t txLen,
                           uint8_t* rx, uint8_t& rxLen,
                           uint32_t timeoutMs = 500) -> bool {
    uint8_t cap = rxLen;
    if (_nfcDataExch(_nfc, _wire, tx, txLen, rx, rxLen, timeoutMs))
      return true;

    rxLen = cap;
    memset(rx, 0, cap);
    return _nfcCommThru(_nfc, _wire, tx, txLen, rx, rxLen, timeoutMs);
  };

  // NTAG21x / Ultralight EV1 GET_VERSION.
  const uint8_t getVersion = 0x60;
  uint8_t version[8] = {};
  bool gotVersion = false;

  for (uint8_t attempt = 0; attempt < 3 && !gotVersion; ++attempt) {
    uint8_t len = sizeof(version);
    memset(version, 0, sizeof(version));
    if (type2Exchange(&getVersion, 1, version, len) && len >= 8)
      gotVersion = true;
    else
      delay(20);
  }

  if (gotVersion) {
    // NXP GET_VERSION starts with fixed byte 0x00 and vendor ID 0x04.
    if (version[0] != 0x00 || version[1] != 0x04) return nullptr;

    if (version[2] == 0x04) { // NTAG21x
      switch (version[6]) {
        case 0x0B: return "NTAG210";
        case 0x0E: return "NTAG212";
        case 0x0F: return "NTAG213";
        case 0x11: return "NTAG215";
        case 0x13: return "NTAG216";
        default:   return nullptr;
      }
    }

    if (version[2] == 0x03) { // MIFARE Ultralight EV1
      switch (version[6]) {
        case 0x0B: return "Ultralight EV1 11";
        case 0x0E: return "Ultralight EV1 21";
        default:   return "Ultralight EV1";
      }
    }

    // A real but unknown GET_VERSION response must not be forced into one of
    // the legacy Ultralight variants.
    return nullptr;
  }

  // Legacy Ultralight C has no GET_VERSION. AUTHENTICATE part 1 returns
  // AF + 8 encrypted bytes when the command is supported.
  const uint8_t authCmd[2] = {0x1A, 0x00};
  uint8_t authResp[16] = {};
  uint8_t authLen = sizeof(authResp);
  const bool isUltralightC =
      type2Exchange(authCmd, sizeof(authCmd), authResp, authLen) &&
      authLen >= 9 && authResp[0] == 0xAF;

  // The UL-C probe starts authentication. Re-select the card so the scan does
  // not leave it in a partial authentication state for the next PN532 action.
  uint8_t reselectUid[7] = {};
  uint8_t reselectUidLen = 0;
  _nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A,
                            reselectUid, &reselectUidLen, 200);

  if (isUltralightC) return "Ultralight C";

  // Original MIFARE Ultralight: page 0 is readable, while page 16 is beyond
  // the 16-page memory. READ returns four pages (16 bytes).
  const uint8_t read0[2]  = {0x30, 0x00};
  const uint8_t read16[2] = {0x30, 0x10};
  uint8_t data[18] = {};
  uint8_t len = sizeof(data);
  bool page0Ok = type2Exchange(read0, sizeof(read0), data, len) && len >= 16;

  len = sizeof(data);
  memset(data, 0, sizeof(data));
  bool page16Ok = type2Exchange(read16, sizeof(read16), data, len) && len >= 16;

  if (page0Ok && !page16Ok) return "Ultralight";

  return nullptr;
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
      const bool sameCard =
        _hasCard &&
        uidLen == _uidLen &&
        memcmp(uid, _uid, uidLen) == 0;

      memcpy(_uid, uid, uidLen);
      _uidLen  = uidLen;
      _atqa    = ((uint16_t)pn532_packetbuffer[9] << 8) | pn532_packetbuffer[10];
      _sak     = pn532_packetbuffer[11];
      _hasCard = true;

      if (!sameCard) {
        _mfKeys.fill({});
      }
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

  // Match the Chameleon HF reader scan presentation.
  auto& lcd = Uni.Lcd;
  const int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Scanning ISO14443A...", bx + bw / 2, by + bh / 2 - 8);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.drawString("Hold card near reader", bx + bw / 2, by + bh / 2 + 8);

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
  if (!ok) { ShowStatusAction::show("No card found", 1200); _goMain(); return; }

  int n = Achievement.inc("nfc_uid_first");
  if (n == 1)  Achievement.unlock("nfc_uid_first");
  if (n == 10) Achievement.unlock("nfc_uid_10");

  _state = STATE_SCAN_RESULT;
  _resetRows();
  char buf[24];
  _pushRow("UID",  _hexUid(_uid, _uidLen));
  const char* typeName = _inferType(_sak, _atqa);
  if (_sak == 0x00) {
    const char* concreteType = _inferType2Variant();
    if (concreteType) typeName = concreteType;
  }
  _pushRow("Type", typeName);
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

  ProgressView::finish();
  _goMifare();
}

void PN532I2cScreen::_doDumpMemory() {
  if (!_hasCard) { ShowStatusAction::show("Authenticate first"); _goMifare(); return; }
  auto dims = _mfDims(_sak);
  if (dims.first == 0) { ShowStatusAction::show("Not MIFARE Classic"); _goMifare(); return; }

  _state = STATE_MIFARE_DUMP;
  _resetRows();
  _hasDump = false;
  const size_t totalSectors = dims.first;
  const size_t totalBlocks = dims.second;
  _dumpLen = totalBlocks * 16u;

  memset(_dumpImg, 0x00, sizeof(_dumpImg));
  static constexpr uint8_t kTrailer[16] = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0xFF,0x07,0x80,0x69, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
  };
  for (size_t sector = 0; sector < totalSectors; ++sector) {
    const size_t trailerBlock =
        (sector < 32) ? (sector * 4 + 3)
                      : (128 + (sector - 32) * 16 + 15);
    memcpy(&_dumpImg[trailerBlock * 16], kTrailer, 16);
  }

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
    memcpy(&_dumpImg[blk * 16], data, 16);
  }

  char summary[32];
  snprintf(summary, sizeof(summary), "%d/%d blocks", readCount, (int)totalBlocks);
  _pushRow("Read", summary);
  _pushRow("[Press]", "Save dump");
  _hasDump = true;

  int n = Achievement.inc("nfc_dump_memory");
  if (n == 1) Achievement.unlock("nfc_dump_memory");

  ProgressView::finish();
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
  if (_dictPickDir.length() == 0) _dictPickDir = _dictPath;
  _browser.root = _dictPath;
  uint8_t n = _browser.load(this, _dictPickDir, ".txt");
  if (n == 0 && _dictPickDir == _dictPath) {
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
  const auto& e = _browser.entry(fileIndex);
  if (e.isDir) {
    _dictPickDir = e.path;
    _doDictionaryPicker();
    return;
  }
  String filePath = e.path;
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

  ProgressView::finish();
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

  _state = STATE_NDEF_RESULT;
  _resetRows();
  memcpy(_uid, uid, uidLen);
  _uidLen = uidLen;
  _pushRow("UID", _hexUid(uid, uidLen));

  memcpy(_uid, uid, uidLen);
  _uidLen = uidLen;
  _hasNdef = false;
  _ndefLen = 0;

  ProgressView::init();
  static constexpr uint8_t TOTAL_PAGES = 64;
  for (uint8_t page = 0; page < TOTAL_PAGES; page++) {
    const uint8_t currentPage = page + 1;
    int pct = (int)((uint16_t)page * 100u / TOTAL_PAGES);
    char msg[32];
    snprintf(msg, sizeof(msg), "Reading %u/%u pages",
             (unsigned)currentPage, (unsigned)TOTAL_PAGES);
    ProgressView::progress(msg, pct);
    uint8_t data[4];
    if (!_nfc->mifareultralight_ReadPage(page, data)) break;
    _pushRow("P" + String(page), _hexBlock(data, 4));
  }
  ProgressView::finish();
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

  // Restore the Ultralight screen after the input overlays before I/O.
  render();

  if (_nfc->mifareultralight_WritePage((uint8_t)page, data)) {
    ShowStatusAction::show("Write OK");
  } else {
    ShowStatusAction::show("Write failed");
  }
  _goUltralight();
}


void PN532I2cScreen::_doReadNdef() {
  _ndefTarget = NDEF_TARGET_ULTRALIGHT;
  _hasNdef = false;
  _ndefLen = 0;
  _ndefCapacity = 0;
  ShowStatusAction::show("Place UL/NTAG on reader...", 0);

  uint8_t uid[7];
  uint8_t uidLen = 0;
  uint32_t start = millis();
  bool ok = false;

  while (millis() - start < 5000) {
    Uni.update();
    if (Uni.Nav->wasPressed() &&
        Uni.Nav->readDirection() == INavigation::DIR_BACK) {
      _goUltralight();
      return;
    }

    if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 200)) {
      ok = true;
      break;
    }
    delay(50);
  }

  if (!ok) {
    ShowStatusAction::show("No card");
    _goUltralight();
    return;
  }

  // NFC Forum Type 2 Capability Container (page 3).
  // Byte 2 gives the data-area capacity in units of 8 bytes.
  uint8_t cc[4] = {};
  if (_nfc->mifareultralight_ReadPage(3, cc) && cc[0] == 0xE1) {
    _ndefCapacity = (size_t)cc[2] * 8;
  }

  // Type 2 Tag user memory starts at page 4. Read a conservative 60 pages
  // (240 bytes), enough for common short NDEF records and matching the current
  // UniGeek Ultralight page range.
  static constexpr uint8_t FIRST_PAGE = 4;
  static constexpr uint8_t LAST_PAGE  = 63;
  static constexpr size_t USER_BYTES  = (LAST_PAGE - FIRST_PAGE + 1) * 4;

  uint8_t user[USER_BYTES] = {};
  size_t userLen = 0;

  ProgressView::init();
  const uint8_t totalPages = LAST_PAGE - FIRST_PAGE + 1;
  for (uint8_t page = FIRST_PAGE; page <= LAST_PAGE; page++) {
    const uint8_t currentPage = page - FIRST_PAGE + 1;
    char msg[32];
    snprintf(msg, sizeof(msg), "Reading %u/%u pages",
             (unsigned)currentPage, (unsigned)totalPages);
    ProgressView::progress(msg,
                           (int)((uint16_t)(currentPage - 1) * 100u / totalPages));

    uint8_t data[4];
    if (!_nfc->mifareultralight_ReadPage(page, data)) break;

    memcpy(&user[userLen], data, 4);
    userLen += 4;
  }
  ProgressView::finish();

  // Parse the Type 2 Tag TLV stream and locate the first NDEF Message TLV (0x03).
  const uint8_t* ndef = nullptr;
  size_t ndefLen = 0;
  size_t pos = 0;

  while (pos < userLen) {
    uint8_t tlv = user[pos++];

    if (tlv == 0x00) continue; // NULL TLV
    if (tlv == 0xFE) break;    // Terminator TLV
    if (pos >= userLen) break;

    size_t len = user[pos++];
    if (len == 0xFF) {
      if (pos + 1 >= userLen) break;
      len = ((size_t)user[pos] << 8) | user[pos + 1];
      pos += 2;
    }

    if (pos + len > userLen) break;

    if (tlv == 0x03) {
      ndef = &user[pos];
      ndefLen = len;
      break;
    }

    pos += len;
  }

  _showNdefResult(uid, uidLen, ndef, ndefLen);
}


void PN532I2cScreen::_showNdefResult(const uint8_t* uid, uint8_t uidLen,
                                         const uint8_t* ndef, size_t ndefLen) {
  _state = STATE_NDEF_RESULT;
  _resetRows();

  if (!_ndefWritePreview && uid && uidLen > 0) {
    memcpy(_uid, uid, uidLen);
    _uidLen = uidLen;
  }
  _hasNdef = false;
  _ndefLen = 0;

  if (uid && uidLen > 0) {
    _pushRow("UID", _hexUid(uid, uidLen));
  }

  if (!ndef) {
    _pushRow("NDEF", "Not found");
    _scrollView.setRows(_rows, _rowCount);
    render();
    return;
  }

  if (ndefLen <= MAX_NDEF_BYTES) {
    memcpy(_ndefBuf, ndef, ndefLen);
    _ndefLen = ndefLen;
    _hasNdef = true;
  }

  char lenBuf[24];
  char capBuf[24];
  char freeBuf[24];

  snprintf(lenBuf, sizeof(lenBuf), "%u bytes", (unsigned)ndefLen);
  _pushRow("NDEF Size", lenBuf);

  if (_ndefCapacity > 0) {
    const size_t freeBytes =
        (_ndefCapacity > ndefLen) ? (_ndefCapacity - ndefLen) : 0;

    snprintf(capBuf, sizeof(capBuf), "%u bytes", (unsigned)_ndefCapacity);
    snprintf(freeBuf, sizeof(freeBuf), "%u bytes", (unsigned)freeBytes);

    _pushRow("Capacity", capBuf);
    _pushRow("Free", freeBuf);
  }

  if (ndefLen == 0) {
    _pushRow("NDEF", "Empty");
    _pushRow("[Press]", "Actions");
    _scrollView.setRows(_rows, _rowCount);
    render();
    return;
  }

  NdefParser::Result parsed;
  if (!NdefParser::parse(ndef, ndefLen, parsed)) {
    _pushRow("NDEF", "Invalid record");
    _scrollView.setRows(_rows, _rowCount);
    render();
    return;
  }

  char tnfBuf[8];
  snprintf(tnfBuf, sizeof(tnfBuf), "%u", parsed.tnf);
  _pushRow("TNF", tnfBuf);
  _pushRow("Type", parsed.type.length() ? parsed.type : "(empty)");

  switch (parsed.kind) {
    case NdefParser::RECORD_TEXT:
      _pushRow("Record", "Text");
      _pushRow("Encoding", parsed.encoding);
      if (parsed.language.length()) _pushRow("Language", parsed.language);
      if (parsed.encoding == "UTF-16") {
        _pushRow("Text", "(UTF-16 raw)");
        uint8_t rawLen = (uint8_t)(parsed.payloadLen < 32 ? parsed.payloadLen : 32);
        _pushRow("Raw", _hexBlock(parsed.payload, rawLen));
      } else {
        _pushWrappedRow("Text", parsed.text);
      }
      break;

    case NdefParser::RECORD_URL:
      _pushRow("Record", "URI");
      _pushWrappedRow("URI", parsed.uri);
      break;

    case NdefParser::RECORD_PHONE:
      _pushRow("Record", "Phone");
      _pushWrappedRow("Phone", parsed.phone);
      break;

    case NdefParser::RECORD_EMAIL:
      _pushRow("Record", "Email");
      _pushWrappedRow("Mail", parsed.email);
      break;

    case NdefParser::RECORD_VCARD:
      _pushRow("Record", "vCard");
      if (parsed.contact.length()) _pushWrappedRow("Contact name", parsed.contact);
      if (parsed.company.length()) _pushWrappedRow("Company", parsed.company);
      if (parsed.address.length()) _pushWrappedRow("Address", parsed.address);
      if (parsed.phone.length()) _pushWrappedRow("Phone", parsed.phone);
      if (parsed.email.length()) _pushWrappedRow("Mail", parsed.email);
      if (parsed.website.length()) _pushWrappedRow("Website", parsed.website);
      if (!parsed.contact.length() && !parsed.company.length() &&
          !parsed.address.length() && !parsed.phone.length() &&
          !parsed.email.length() && !parsed.website.length()) {
        String vcard;
        vcard.reserve(parsed.payloadLen);
        for (size_t i = 0; i < parsed.payloadLen; ++i) vcard += (char)parsed.payload[i];
        vcard.replace("\r\n", "\n");
        vcard.replace("\r", "\n");
        _pushWrappedRow("vCard", vcard);
      }
      break;

    default: {
      _pushRow("Record", "Unsupported");
      uint8_t rawLen = (uint8_t)(parsed.payloadLen < 32 ? parsed.payloadLen : 32);
      _pushRow("Payload", _hexBlock(parsed.payload, rawLen));
      break;
    }
  }

  if (_hasNdef) {
    _pushRow("[Press]", _ndefWritePreview ? "Write to Tag" : "Actions");
  }

  _scrollView.setRows(_rows, _rowCount);
  render();
}

// ── MIFARE Classic NDEF ─────────────────────────────────────────────────────
//
// NFC Forum formatted MIFARE Classic uses MAD AID 0x03E1 to mark NFC sectors.
// MAD sectors are readable with public Key A A0:A1:A2:A3:A4:A5 and
// non-proprietary NFC sectors use public Key A D3:F7:D3:F7:D3:F7.
//
// This first implementation intentionally operates on cards that are already
// NFC/NDEF formatted. It never rewrites MAD entries, sector trailers, keys or
// access bits.

bool PN532I2cScreen::_classicAuthSector(uint8_t sector, const uint8_t key[6]) {
  uint16_t block = (sector < 32)
                     ? (uint16_t)sector * 4
                     : (uint16_t)(128 + (sector - 32) * 16);

  if (_nfc->mifareclassic_AuthenticateBlock(
        _uid, _uidLen, block, 0, const_cast<uint8_t*>(key))) {
    return true;
  }

  // A failed MIFARE authentication can leave the PICC halted. Re-select once.
  uint8_t uid[7] = {};
  uint8_t uidLen = 0;
  if (!_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 250)) {
    return false;
  }
  if (uidLen != _uidLen || memcmp(uid, _uid, uidLen) != 0) return false;

  return _nfc->mifareclassic_AuthenticateBlock(
      _uid, _uidLen, block, 0, const_cast<uint8_t*>(key));
}

bool PN532I2cScreen::_classicNdefSectors(uint8_t* sectors,
                                         size_t maxSectors,
                                         size_t& count) {
  count = 0;
  if (!sectors || maxSectors == 0) return false;

  auto dims = _mfDims(_sak);
  if (dims.first == 0) return false;

  static const uint8_t MAD_KEY_A[6] = {
    0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5
  };

  // MAD1 lives in sector 0. Block 1: CRC, Info, AIDs S1..S7.
  // Block 2: AIDs S8..S15. AID bytes are stored low byte first.
  if (!_classicAuthSector(0, MAD_KEY_A)) return false;

  uint8_t b1[16] = {};
  uint8_t b2[16] = {};
  if (!_nfc->mifareclassic_ReadDataBlock(1, b1) ||
      !_nfc->mifareclassic_ReadDataBlock(2, b2)) {
    return false;
  }

  auto addIfNdef = [&](uint8_t sector, uint8_t lo, uint8_t hi) {
    if (sector >= dims.first) return;
    if (lo == 0x03 && hi == 0xE1 && count < maxSectors) {
      sectors[count++] = sector;
    }
  };

  for (uint8_t s = 1; s <= 7; s++) {
    size_t off = 2 + (size_t)(s - 1) * 2;
    addIfNdef(s, b1[off], b1[off + 1]);
  }
  for (uint8_t s = 8; s <= 15; s++) {
    size_t off = (size_t)(s - 8) * 2;
    addIfNdef(s, b2[off], b2[off + 1]);
  }

  // MAD2 uses sector 16 on Classic 4K and maps sectors 17..39.
  if (dims.first > 16) {
    if (_classicAuthSector(16, MAD_KEY_A)) {
      uint8_t m0[16] = {};
      uint8_t m1[16] = {};
      uint8_t m2[16] = {};
      if (_nfc->mifareclassic_ReadDataBlock(64, m0) &&
          _nfc->mifareclassic_ReadDataBlock(65, m1) &&
          _nfc->mifareclassic_ReadDataBlock(66, m2)) {
        for (uint8_t s = 17; s <= 23; s++) {
          size_t off = 2 + (size_t)(s - 17) * 2;
          addIfNdef(s, m0[off], m0[off + 1]);
        }
        for (uint8_t s = 24; s <= 31; s++) {
          size_t off = (size_t)(s - 24) * 2;
          addIfNdef(s, m1[off], m1[off + 1]);
        }
        for (uint8_t s = 32; s <= 39; s++) {
          size_t off = (size_t)(s - 32) * 2;
          addIfNdef(s, m2[off], m2[off + 1]);
        }
      }
    }
  }

  return count > 0;
}

bool PN532I2cScreen::_classicReadNdefArea(const uint8_t* sectors,
                                           size_t sectorCount,
                                           uint8_t*& area,
                                           size_t& areaLen) {
  area = nullptr;
  areaLen = 0;
  if (!sectors || sectorCount == 0) return false;

  size_t capacity = 0;
  for (size_t i = 0; i < sectorCount; i++) {
    capacity += (sectors[i] < 32) ? 48 : 240;
  }

  area = new uint8_t[capacity];
  if (!area) return false;

  static const uint8_t NFC_KEY_A[6] = {
    0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7
  };

  ProgressView::init();
  size_t out = 0;

  for (size_t si = 0; si < sectorCount; si++) {
    uint8_t sector = sectors[si];
    if (!_classicAuthSector(sector, NFC_KEY_A)) {
      ProgressView::finish();
      delete[] area;
      area = nullptr;
      areaLen = 0;
      return false;
    }

    uint16_t firstBlock = (sector < 32)
                            ? (uint16_t)sector * 4
                            : (uint16_t)(128 + (sector - 32) * 16);
    uint8_t dataBlocks = (sector < 32) ? 3 : 15;

    for (uint8_t bi = 0; bi < dataBlocks; bi++) {
      char msg[32];
      snprintf(msg, sizeof(msg), "Reading S%u B%u",
               (unsigned)sector, (unsigned)bi);
      ProgressView::progress(msg, (int)(out * 100 / capacity));

      uint8_t data[16] = {};
      if (!_nfc->mifareclassic_ReadDataBlock((uint8_t)(firstBlock + bi), data)) {
        ProgressView::finish();
        delete[] area;
        area = nullptr;
        areaLen = 0;
        return false;
      }
      memcpy(area + out, data, 16);
      out += 16;
    }
  }

  ProgressView::finish();
  areaLen = out;
  return true;
}

void PN532I2cScreen::_doReadClassicNdef() {
  _ndefTarget = NDEF_TARGET_MIFARE_CLASSIC;
  _hasNdef = false;
  _ndefLen = 0;
  _ndefCapacity = 0;

  if (!_scanCardOrShow(5000)) {
    _goMifare();
    return;
  }

  auto dims = _mfDims(_sak);
  if (dims.first == 0) {
    ShowStatusAction::show("Not MIFARE Classic");
    _goMifare();
    return;
  }

  uint8_t sectors[39] = {};
  size_t sectorCount = 0;
  if (!_classicNdefSectors(sectors, sizeof(sectors), sectorCount)) {
    ShowStatusAction::show("No NDEF sectors in MAD");
    _goMifare();
    return;
  }

  _ndefCapacity = 0;
  for (size_t i = 0; i < sectorCount; i++) {
    _ndefCapacity += (sectors[i] < 32) ? 48 : 240;
  }

  uint8_t* area = nullptr;
  size_t areaLen = 0;
  if (!_classicReadNdefArea(sectors, sectorCount, area, areaLen)) {
    ShowStatusAction::show("Failed to read NDEF sectors");
    _goMifare();
    return;
  }

  const uint8_t* ndef = nullptr;
  size_t ndefLen = 0;
  size_t pos = 0;

  while (pos < areaLen) {
    uint8_t tlv = area[pos++];
    if (tlv == 0x00) continue;
    if (tlv == 0xFE) break;
    if (pos >= areaLen) break;

    size_t len = area[pos++];
    if (len == 0xFF) {
      if (pos + 1 >= areaLen) break;
      len = ((size_t)area[pos] << 8) | area[pos + 1];
      pos += 2;
    }
    if (pos + len > areaLen) break;

    if (tlv == 0x03) {
      ndef = area + pos;
      ndefLen = len;
      break;
    }
    pos += len;
  }

  // _showNdefResult copies short NDEF data into _ndefBuf before area is freed.
  _showNdefResult(_uid, _uidLen, ndef, ndefLen);
  delete[] area;
}

bool PN532I2cScreen::_writeClassicNdefRecord(const uint8_t* ndef, size_t ndefLen) {
  if (!ndef || ndefLen == 0 || ndefLen > MAX_NDEF_BYTES) {
    ShowStatusAction::show("NDEF too large");
    return false;
  }

  if (!_scanCardOrShow(5000)) return false;
  if (_mfDims(_sak).first == 0) {
    ShowStatusAction::show("Not MIFARE Classic");
    return false;
  }

  uint8_t sectors[39] = {};
  size_t sectorCount = 0;
  if (!_classicNdefSectors(sectors, sizeof(sectors), sectorCount)) {
    ShowStatusAction::show("Not NDEF formatted");
    return false;
  }

  size_t capacity = 0;
  for (size_t i = 0; i < sectorCount; i++) {
    capacity += (sectors[i] < 32) ? 48 : 240;
  }

  // Initial implementation targets the NFC Forum Simple Configuration:
  // mandatory NDEF Message TLV begins at byte 0 of the first NFC sector.
  const size_t payloadLen = ndefLen + 3; // 03 LEN <NDEF> FE
  if (payloadLen > capacity) {
    ShowStatusAction::show("NDEF does not fit");
    return false;
  }

  uint8_t* payload = new uint8_t[payloadLen];
  if (!payload) {
    ShowStatusAction::show("Out of memory");
    return false;
  }
  payload[0] = 0x03;
  payload[1] = (uint8_t)ndefLen;
  memcpy(payload + 2, ndef, ndefLen);
  payload[2 + ndefLen] = 0xFE;

  static const uint8_t NFC_KEY_A[6] = {
    0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7
  };

  // First block is committed last with the real length. Until then the tag
  // advertises an empty NDEF TLV, reducing the chance of a torn write exposing
  // a partially-written message.
  uint8_t firstFinal[16] = {};
  uint8_t firstStaged[16] = {};
  bool firstPrepared = false;
  bool success = true;
  size_t offset = 0;

  ProgressView::init();

  for (size_t si = 0; si < sectorCount && offset < payloadLen; si++) {
    uint8_t sector = sectors[si];
    if (!_classicAuthSector(sector, NFC_KEY_A)) {
      success = false;
      break;
    }

    uint16_t firstBlock = (sector < 32)
                            ? (uint16_t)sector * 4
                            : (uint16_t)(128 + (sector - 32) * 16);
    uint8_t dataBlocks = (sector < 32) ? 3 : 15;

    for (uint8_t bi = 0; bi < dataBlocks && offset < payloadLen; bi++) {
      uint8_t blockNo = (uint8_t)(firstBlock + bi);
      uint8_t block[16] = {};
      if (!_nfc->mifareclassic_ReadDataBlock(blockNo, block)) {
        success = false;
        break;
      }

      size_t take = payloadLen - offset;
      if (take > 16) take = 16;
      memcpy(block, payload + offset, take);

      char msg[32];
      snprintf(msg, sizeof(msg), "Writing S%u B%u",
               (unsigned)sector, (unsigned)bi);
      ProgressView::progress(msg, (int)(offset * 100 / payloadLen));

      if (!firstPrepared) {
        memcpy(firstFinal, block, 16);
        memcpy(firstStaged, block, 16);
        firstStaged[1] = 0x00;  // empty NDEF until final commit

        if (!_nfc->mifareclassic_WriteDataBlock(blockNo, firstStaged)) {
          success = false;
          break;
        }
        firstPrepared = true;
      } else {
        if (!_nfc->mifareclassic_WriteDataBlock(blockNo, block)) {
          success = false;
          break;
        }
      }

      offset += take;
    }
    if (!success) break;
  }

  // Re-authenticate first NFC sector and commit the real length last.
  if (success && firstPrepared) {
    if (!_classicAuthSector(sectors[0], NFC_KEY_A) ||
        !_nfc->mifareclassic_WriteDataBlock(
            (uint8_t)((sectors[0] < 32)
                        ? sectors[0] * 4
                        : 128 + (sectors[0] - 32) * 16),
            firstFinal)) {
      success = false;
    }
  }

  ProgressView::finish();
  delete[] payload;

  ShowStatusAction::show(success ? "NDEF written" : "NDEF write failed");
  return success;
}

void PN532I2cScreen::_doEraseClassicNdef() {
  _ndefTarget = NDEF_TARGET_MIFARE_CLASSIC;

  if (!_scanCardOrShow(5000)) {
    _goMifare();
    return;
  }
  if (_mfDims(_sak).first == 0) {
    ShowStatusAction::show("Not MIFARE Classic");
    _goMifare();
    return;
  }

  uint8_t sectors[39] = {};
  size_t sectorCount = 0;
  if (!_classicNdefSectors(sectors, sizeof(sectors), sectorCount)) {
    ShowStatusAction::show("Not NDEF formatted");
    _goMifare();
    return;
  }

  static const uint8_t NFC_KEY_A[6] = {
    0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7
  };

  if (!_classicAuthSector(sectors[0], NFC_KEY_A)) {
    ShowStatusAction::show("NDEF sector locked");
    _goMifare();
    return;
  }

  uint8_t blockNo = (uint8_t)((sectors[0] < 32)
                                ? sectors[0] * 4
                                : 128 + (sectors[0] - 32) * 16);
  uint8_t block[16] = {};
  if (!_nfc->mifareclassic_ReadDataBlock(blockNo, block)) {
    ShowStatusAction::show("Read failed");
    _goMifare();
    return;
  }

  // Logical erase defined for NDEF: empty NDEF Message TLV + Terminator TLV.
  // Bytes after FE are intentionally left untouched; they are no longer active.
  block[0] = 0x03;
  block[1] = 0x00;
  block[2] = 0xFE;

  bool success = _nfc->mifareclassic_WriteDataBlock(blockNo, block);
  _hasNdef = false;
  _ndefLen = 0;
  ShowStatusAction::show(success ? "NDEF erased" : "NDEF erase failed");
  _goMifare();
}


bool PN532I2cScreen::_writeNdefRecord(const uint8_t* ndef, size_t ndefLen) {
  if (_ndefTarget == NDEF_TARGET_MIFARE_CLASSIC) {
    return _writeClassicNdefRecord(ndef, ndefLen);
  }
  return _writeUltralightNdefRecord(ndef, ndefLen);
}

bool PN532I2cScreen::_writeUltralightNdefRecord(const uint8_t* ndef, size_t ndefLen) {
  if (!ndef || ndefLen == 0 || ndefLen > 254) {
    ShowStatusAction::show("NDEF too large");
    return false;
  }

  ShowStatusAction::show("Place UL/NTAG on reader...", 0);

  uint8_t uid[7];
  uint8_t uidLen = 0;
  uint32_t start = millis();
  bool ok = false;

  while (millis() - start < 5000) {
    Uni.update();
    if (Uni.Nav->wasPressed() &&
        Uni.Nav->readDirection() == INavigation::DIR_BACK) {
      return false;
    }

    if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 200)) {
      ok = true;
      break;
    }
    delay(50);
  }

  if (!ok) {
    ShowStatusAction::show("No card");
    return false;
  }

  // Read the NFC Forum Type 2 Capability Container from page 3.
  // CC byte 2 gives the data area size in multiples of 8 bytes.
  uint8_t cc[4] = {};
  if (!_nfc->mifareultralight_ReadPage(3, cc)) {
    ShowStatusAction::show("Failed to read CC");
    return false;
  }

  if (cc[0] != 0xE1) {
    ShowStatusAction::show("Not NDEF formatted");
    return false;
  }

  size_t capacity = (size_t)cc[2] * 8;
  if (capacity == 0) {
    ShowStatusAction::show("Invalid NDEF capacity");
    return false;
  }

  // Type 2 Tag memory layout:
  // 03 <LEN> <NDEF message> FE
  size_t tlvLen = ndefLen + 3;
  size_t paddedLen = (tlvLen + 3) & ~((size_t)3);

  if (paddedLen > capacity) {
    ShowStatusAction::show("NDEF does not fit");
    return false;
  }

  uint8_t* payload = new uint8_t[paddedLen];
  if (!payload) {
    ShowStatusAction::show("Out of memory");
    return false;
  }

  memset(payload, 0x00, paddedLen);
  payload[0] = 0x03;
  payload[1] = (uint8_t)ndefLen;
  memcpy(&payload[2], ndef, ndefLen);
  payload[2 + ndefLen] = 0xFE;

  ProgressView::init();
  bool success = true;

  const size_t totalPages = paddedLen / 4;
  for (size_t offset = 0; offset < paddedLen; offset += 4) {
    uint8_t page = 4 + (offset / 4);
    const size_t currentPage = offset / 4 + 1;

    char msg[32];
    snprintf(msg, sizeof(msg), "Writing %u/%u pages",
             (unsigned)currentPage, (unsigned)totalPages);
    ProgressView::progress(msg, (int)(offset * 100 / paddedLen));

    if (!_nfc->mifareultralight_WritePage(page, &payload[offset])) {
      success = false;
      break;
    }
  }

  ProgressView::finish();
  delete[] payload;

  ShowStatusAction::show(success ? "NDEF written" : "NDEF write failed");
  return success;
}

void PN532I2cScreen::_doWriteNdefText() {
  String text = InputTextAction::popup("Enter text", "");
  if (InputTextAction::wasCancelled() || text.length() == 0) {
    _goNdefWrite();
    return;
  }

  // Short-record NDEF Text:
  // D1 01 <payloadLen> 54 02 'e' 'n' <text>
  size_t payloadLen = 1 + 2 + text.length();
  size_t ndefLen = 4 + payloadLen;

  if (ndefLen > 254 || payloadLen > 255) {
    ShowStatusAction::show("Text too long");
    _goNdefWrite();
    return;
  }

  uint8_t* ndef = new uint8_t[ndefLen];
  if (!ndef) {
    ShowStatusAction::show("Out of memory");
    _goNdefWrite();
    return;
  }

  ndef[0] = 0xD1;                    // MB | ME | SR | TNF=Well Known
  ndef[1] = 0x01;                    // Type length
  ndef[2] = (uint8_t)payloadLen;
  ndef[3] = 'T';
  ndef[4] = 0x02;                    // UTF-8, language code length 2
  ndef[5] = 'e';
  ndef[6] = 'n';
  memcpy(&ndef[7], text.c_str(), text.length());

  _showNdefWritePreview(ndef, ndefLen, false);
  delete[] ndef;
}

void PN532I2cScreen::_doWriteNdefUrl() {
  String url = InputTextAction::popup("Enter URL", "https://");
  if (InputTextAction::wasCancelled() || url.length() == 0) {
    _goNdefWrite();
    return;
  }

  uint8_t prefix = 0x00;
  const char* body = url.c_str();

  if      (url.startsWith("https://www.")) { prefix = 0x02; body += 12; }
  else if (url.startsWith("http://www."))  { prefix = 0x01; body += 11; }
  else if (url.startsWith("https://"))     { prefix = 0x04; body += 8; }
  else if (url.startsWith("http://"))      { prefix = 0x03; body += 7; }

  size_t bodyLen = strlen(body);
  size_t payloadLen = 1 + bodyLen;
  size_t ndefLen = 4 + payloadLen;

  if (ndefLen > 254 || payloadLen > 255) {
    ShowStatusAction::show("URL too long");
    _goNdefWrite();
    return;
  }

  uint8_t* ndef = new uint8_t[ndefLen];
  if (!ndef) {
    ShowStatusAction::show("Out of memory");
    _goNdefWrite();
    return;
  }

  ndef[0] = 0xD1;                    // MB | ME | SR | TNF=Well Known
  ndef[1] = 0x01;                    // Type length
  ndef[2] = (uint8_t)payloadLen;
  ndef[3] = 'U';
  ndef[4] = prefix;
  memcpy(&ndef[5], body, bodyLen);

  _showNdefWritePreview(ndef, ndefLen, false);
  delete[] ndef;
}

void PN532I2cScreen::_doWriteNdefEmail() {
  String email = InputTextAction::popup("Enter email", "");
  if (InputTextAction::wasCancelled() || email.length() == 0) {
    _goNdefWrite();
    return;
  }

  String uri = email.startsWith("mailto:") ? email : ("mailto:" + email);

  uint8_t prefix = 0x06;  // "mailto:"
  const char* body = uri.c_str() + 7;
  size_t bodyLen = strlen(body);
  size_t payloadLen = 1 + bodyLen;
  size_t ndefLen = 4 + payloadLen;

  if (ndefLen > MAX_NDEF_BYTES || payloadLen > 255) {
    ShowStatusAction::show("Email too long");
    _goNdefWrite();
    return;
  }

  uint8_t* ndef = new uint8_t[ndefLen];
  if (!ndef) {
    ShowStatusAction::show("Out of memory");
    _goNdefWrite();
    return;
  }

  ndef[0] = 0xD1;                  // MB | ME | SR | TNF=Well Known
  ndef[1] = 0x01;                  // Type length
  ndef[2] = (uint8_t)payloadLen;
  ndef[3] = 'U';                   // URI record
  ndef[4] = prefix;
  memcpy(&ndef[5], body, bodyLen);

  _showNdefWritePreview(ndef, ndefLen, false);
  delete[] ndef;
}

void PN532I2cScreen::_doWriteNdefPhone() {
  String phone = InputTextAction::popup("Enter phone", "", InputTextAction::INPUT_PHONE);
  if (InputTextAction::wasCancelled() || phone.length() == 0) {
    _goNdefWrite();
    return;
  }

  String uri = phone.startsWith("tel:") ? phone : ("tel:" + phone);

  uint8_t prefix = 0x05;  // "tel:"
  const char* body = uri.c_str() + 4;
  size_t bodyLen = strlen(body);
  size_t payloadLen = 1 + bodyLen;
  size_t ndefLen = 4 + payloadLen;

  if (ndefLen > MAX_NDEF_BYTES || payloadLen > 255) {
    ShowStatusAction::show("Phone too long");
    _goNdefWrite();
    return;
  }

  uint8_t* ndef = new uint8_t[ndefLen];
  if (!ndef) {
    ShowStatusAction::show("Out of memory");
    _goNdefWrite();
    return;
  }

  ndef[0] = 0xD1;                  // MB | ME | SR | TNF=Well Known
  ndef[1] = 0x01;                  // Type length
  ndef[2] = (uint8_t)payloadLen;
  ndef[3] = 'U';                   // URI record
  ndef[4] = prefix;
  memcpy(&ndef[5], body, bodyLen);

  _showNdefWritePreview(ndef, ndefLen, false);
  delete[] ndef;
}




void PN532I2cScreen::_showNdefActions() {
  if (!_hasNdef) return;

  static const InputSelectAction::Option opts[] = {
    {"Write to Tag", "write"},
    {"Save to File", "save"},
  };

  const char* choice = InputSelectAction::popup("NDEF Actions", opts, 2, nullptr);
  if (!choice) {
    render();
    return;
  }

  render();

  if (strcmp(choice, "write") == 0) {
    _doWriteCurrentNdef();
  } else if (strcmp(choice, "save") == 0) {
    _doSaveNdef();
  }
}

void PN532I2cScreen::_doWriteCurrentNdef() {
  if (!_hasNdef || _ndefLen == 0) {
    ShowStatusAction::show("No NDEF loaded");
    render();
    return;
  }

  static const InputSelectAction::Option targets[] = {
    {"Ultralight / NTAG", "ul"},
    {"MIFARE Classic",    "mfc"},
  };

  const char* choice =
      InputSelectAction::popup("Write to Tag", targets, 2, nullptr);

  if (!choice) {
    render();
    return;
  }

  render();

  bool ok = false;
  if (strcmp(choice, "ul") == 0) {
    ok = _writeUltralightNdefRecord(_ndefBuf, _ndefLen);
  } else if (strcmp(choice, "mfc") == 0) {
    ok = _writeClassicNdefRecord(_ndefBuf, _ndefLen);
  }

  if (ok) {
    // Close the Read NDEF result after a successful action. Return to the
    // menu associated with the tag that was originally read.
    if (_ndefTarget == NDEF_TARGET_MIFARE_CLASSIC) _goMifare();
    else _goUltralight();
    return;
  }

  // Failure keeps the preview/result available for another attempt.
  _state = STATE_NDEF_RESULT;
  render();
}

void PN532I2cScreen::_doSaveNdef() {
  if (!_hasNdef || _ndefLen == 0) {
    ShowStatusAction::show(_hasNdef ? "NDEF is empty" : "No NDEF to save");
    render();
    return;
  }
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable");
    render();
    return;
  }

  Uni.Storage->makeDir(_nfcPath);
  Uni.Storage->makeDir(_ndefPath);

  String uid = _hexUid(_uid, _uidLen);
  uid.replace(":", "");
  if (uid.length() == 0) uid = "unknown";
  const char* suffix =
      (_ndefTarget == NDEF_TARGET_MIFARE_CLASSIC) ? "_mifare" : "_ntag";
  String baseName = uid + suffix;

  String name = InputTextAction::popup("File name", baseName.c_str());
  if (InputTextAction::wasCancelled()) { render(); return; }

  name.trim();
  if (name.endsWith(".ndef")) name.remove(name.length() - 5);
  if (name.length() == 0) { render(); return; }

  for (int i = 0; i < (int)name.length(); i++) {
    const char c = name[i];
    const bool ok =
        (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) name.setCharAt(i, '_');
  }
  while (name.indexOf("__") >= 0) name.replace("__", "_");
  while (name.startsWith("_")) name.remove(0, 1);
  while (name.endsWith("_")) name.remove(name.length() - 1);
  if (name.length() == 0) { render(); return; }

  String path = String(_ndefPath) + "/" + name + ".ndef";
  if (Uni.Storage->exists(path.c_str())) {
    for (int n = 2; n < 1000; n++) {
      String candidate = String(_ndefPath) + "/" + name + "_(" + n + ").ndef";
      if (!Uni.Storage->exists(candidate.c_str())) { path = candidate; break; }
    }
  }

  render();
  fs::File f = Uni.Storage->open(path.c_str(), "w");
  if (!f) { ShowStatusAction::show("Save failed"); render(); return; }

  const size_t written = f.write(_ndefBuf, _ndefLen);
  f.close();
  if (written == _ndefLen) {
    const int slash = path.lastIndexOf('/');
    const String saved = (slash >= 0) ? path.substring(slash + 1) : path;
    ShowStatusAction::show(("Saved: " + saved).c_str(), 1500);

    // Successful action closes the NDEF result screen, matching Write to Tag.
    if (_ndefTarget == NDEF_TARGET_MIFARE_CLASSIC) _goMifare();
    else _goUltralight();
    return;
  }

  // Failure keeps the preview/result available for another attempt.
  ShowStatusAction::show("Save failed");
  _state = STATE_NDEF_RESULT;
  render();
}

void PN532I2cScreen::_doWriteNdefVcard() {
  String contact = InputTextAction::popup("Contact name", "");
  if (InputTextAction::wasCancelled() || contact.length() == 0) {
    _goNdefWrite();
    return;
  }

  String company = InputTextAction::popup("Company", "");
  if (InputTextAction::wasCancelled()) { _goNdefWrite(); return; }

  String address = InputTextAction::popup("Address", "");
  if (InputTextAction::wasCancelled()) { _goNdefWrite(); return; }

  String phone = InputTextAction::popup("Phone", "", InputTextAction::INPUT_PHONE);
  if (InputTextAction::wasCancelled()) { _goNdefWrite(); return; }

  String email = InputTextAction::popup("Mail", "");
  if (InputTextAction::wasCancelled()) { _goNdefWrite(); return; }

  String website = InputTextAction::popup("Website", "https://");
  if (InputTextAction::wasCancelled()) { _goNdefWrite(); return; }

  uint8_t ndef[MAX_NDEF_BYTES] = {};
  size_t ndefLen = 0;

  if (!NdefBuilder::buildVcard(contact,
                               company,
                               address,
                               phone,
                               email,
                               website,
                               ndef,
                               ndefLen,
                               MAX_NDEF_BYTES)) {
    ShowStatusAction::show("vCard too large");
    _goNdefWrite();
    return;
  }

  _showNdefWritePreview(ndef, ndefLen, false);
}

void PN532I2cScreen::_showNdefWritePreview(const uint8_t* ndef,
                                               size_t ndefLen,
                                               bool fromFile) {
  _ndefWritePreview = true;
  _ndefWritePreviewFromFile = fromFile;
  _ndefCapacity = 0;
  _showNdefResult(nullptr, 0, ndef, ndefLen);
}

void PN532I2cScreen::_doWriteNdefFromFile() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable");
    _goNdefWrite();
    return;
  }

  Uni.Storage->makeDir(_nfcPath);
  Uni.Storage->makeDir(_ndefPath);

  _state = STATE_NDEF_FILE_SELECT;
  if (_ndefPickDir.length() == 0 || !_ndefPickDir.startsWith(_ndefPath))
    _ndefPickDir = _ndefPath;
  _browser.root = _ndefPath;

  uint8_t n = _browser.load(this, _ndefPickDir, ".ndef");
  setItems(_browser.items(), n);
  render();
}

void PN532I2cScreen::_doWriteNdefFileSelected(uint8_t fileIndex) {
  if (fileIndex >= _browser.count()) return;

  const auto& e = _browser.entry(fileIndex);
  if (e.isDir) {
    _ndefPickDir = e.path;
    _doWriteNdefFromFile();
    return;
  }

  fs::File f = Uni.Storage->open(e.path.c_str(), "r");
  if (!f) {
    ShowStatusAction::show("Read failed");
    _doWriteNdefFromFile();
    return;
  }

  size_t len = f.size();
  if (len == 0 || len > MAX_NDEF_BYTES) {
    f.close();
    ShowStatusAction::show(len == 0 ? "Empty .ndef file" : "NDEF file too large");
    _doWriteNdefFromFile();
    return;
  }

  uint8_t buf[MAX_NDEF_BYTES];
  size_t got = f.read(buf, len);
  f.close();

  if (got != len) {
    ShowStatusAction::show("Read failed");
    _doWriteNdefFromFile();
    return;
  }

  // Preview the selected NDEF using the exact same parser/layout as Read NDEF.
  // _ndefTarget remains unchanged, so confirmation writes to the correct
  // destination: MIFARE Classic or Ultralight / NTAG.
  _showNdefWritePreview(buf, len, true);
}

void PN532I2cScreen::_doEraseNdef() {
  _ndefTarget = NDEF_TARGET_ULTRALIGHT;
  ShowStatusAction::show("Place UL/NTAG on reader...", 0);

  uint8_t uid[7];
  uint8_t uidLen = 0;
  uint32_t start = millis();
  bool ok = false;

  while (millis() - start < 5000) {
    Uni.update();
    if (Uni.Nav->wasPressed() &&
        Uni.Nav->readDirection() == INavigation::DIR_BACK) {
      _goUltralight();
      return;
    }

    if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 200)) {
      ok = true;
      break;
    }
    delay(50);
  }

  if (!ok) {
    ShowStatusAction::show("No card");
    _goUltralight();
    return;
  }

  // Read NFC Forum Type 2 Capability Container from page 3.
  uint8_t cc[4] = {};
  if (!_nfc->mifareultralight_ReadPage(3, cc)) {
    ShowStatusAction::show("Failed to read CC");
    _goUltralight();
    return;
  }

  if (cc[0] != 0xE1) {
    ShowStatusAction::show("Not NDEF formatted");
    _goUltralight();
    return;
  }


  // Logical NDEF erase:
  // 03 = NDEF Message TLV
  // 00 = zero-length NDEF message
  // FE = Terminator TLV
  // The previous bytes after the terminator are no longer part of the active NDEF.
  uint8_t emptyNdef[4] = {0x03, 0x00, 0xFE, 0x00};

  bool success = _nfc->mifareultralight_WritePage(4, emptyNdef);

  ShowStatusAction::show(success ? "NDEF erased" : "NDEF erase failed");
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

  // Restore the Magic Card screen after the UID keyboard before the
  // blocking PN532 exchange.
  render();

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
  if (_dumpLen == 0 || _dumpLen > sizeof(_dumpImg)) {
    f.close();
    ShowStatusAction::show("Invalid dump size");
    render();
    return;
  }
  f.write(_dumpImg, _dumpLen);
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
