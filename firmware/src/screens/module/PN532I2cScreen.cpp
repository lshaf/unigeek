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
#include "ui/views/LogView.h"
#include "../../utils/nfc/NdefBuilder.h"
#include "../../utils/nfc/NdefParser.h"
#include "../../utils/nfc/NfcDumpBuilder.h"

#include "utils/nfc/MfcKeyStore.h"
// ── raw I2C helpers for Gen1a / Gen3 ──────────────────────────────────────
// Adafruit_PN532 exposes sendCommandCheckAck() publicly but readdata() is
#include <mbedtls/md.h>

// private. These helpers build commands in pn532_packetbuffer (the library's
// global) and read the response directly over Wire after the ACK is received.

extern byte pn532_packetbuffer[];

static bool _parseHexKeyI2c(const String& line, uint8_t out[6]);

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

static bool _pn532Type2ReadPage(Adafruit_PN532* nfc, TwoWire* wire,
                                   uint8_t page, uint8_t out[4]) {
  const uint8_t cmd[2] = {0x30, page};
  uint8_t rsp[18] = {};
  uint8_t len = sizeof(rsp);
  if (!_nfcDataExch(nfc, wire, cmd, sizeof(cmd), rsp, len, 500)) {
    len = sizeof(rsp);
    if (!_nfcCommThru(nfc, wire, cmd, sizeof(cmd), rsp, len, 500)) return false;
  }
  if (len < 16) return false;
  memcpy(out, rsp, 4);
  return true;
}

static bool _pn532Type2ReadPageTailSafe(Adafruit_PN532* nfc, TwoWire* wire,
                                           uint16_t page, uint16_t totalPages,
                                           uint8_t out[4]) {
  if (!totalPages || page >= totalPages || totalPages > 256) return false;
  uint16_t start = page;
  uint8_t skip = 0;
  if (page + 3 >= totalPages) {
    start = totalPages >= 4 ? totalPages - 4 : 0;
    skip = (uint8_t)(page - start);
  }
  const uint8_t cmd[2] = {0x30, (uint8_t)start};
  uint8_t rsp[18] = {};
  uint8_t len = sizeof(rsp);
  if (!_nfcDataExch(nfc, wire, cmd, sizeof(cmd), rsp, len, 500)) {
    len = sizeof(rsp);
    if (!_nfcCommThru(nfc, wire, cmd, sizeof(cmd), rsp, len, 500)) return false;
  }
  if (len < 16) return false;
  memcpy(out, rsp + skip * 4u, 4);
  return true;
}

static bool _pn532Type2WritePage(Adafruit_PN532* nfc, TwoWire* wire,
                                    uint8_t page, const uint8_t data[4]) {
  uint8_t cmd[6] = {0xA2, page, data[0], data[1], data[2], data[3]};
  uint8_t rsp[4] = {};
  uint8_t len = sizeof(rsp);
  if (!_nfcDataExch(nfc, wire, cmd, sizeof(cmd), rsp, len, 500)) {
    len = sizeof(rsp);
    if (!_nfcCommThru(nfc, wire, cmd, sizeof(cmd), rsp, len, 500)) return false;
  }
  return len == 0 || (len >= 1 && (rsp[0] & 0x0F) == 0x0A);
}


static bool _type2DefaultCc(const char* typeName, uint8_t cc[4]) {
  if (!typeName || !cc) return false;
  uint8_t size = 0;
  if (strcmp(typeName, "NTAG210") == 0 ||
      strcmp(typeName, "Ultralight EV1 11") == 0 ||
      strcmp(typeName, "Ultralight") == 0) size = 0x06;
  else if (strcmp(typeName, "NTAG212") == 0 ||
           strcmp(typeName, "Ultralight EV1 21") == 0) size = 0x10;
  else if (strcmp(typeName, "NTAG213") == 0 ||
           strcmp(typeName, "Ultralight C") == 0) size = 0x12;
  else if (strcmp(typeName, "NTAG215") == 0) size = 0x3F;
  else if (strcmp(typeName, "NTAG216") == 0) size = 0x6F;
  else return false;
  cc[0] = 0xE1; cc[1] = 0x10; cc[2] = size; cc[3] = 0x00;
  return true;
}

static bool _type2CcIsValid(const uint8_t cc[4]) {
  return cc && cc[0] == 0xE1 && (cc[1] & 0xF0) == 0x10 && cc[2] != 0;
}

static bool _type2CcCanProgramSafely(const uint8_t current[4], const uint8_t desired[4]) {
  // Conservative rule that is also safe for OTP-style page 3: formatting may
  // set previously-clear bits, but never requires clearing an already-set bit.
  for (uint8_t i = 0; i < 4; ++i)
    if ((current[i] & (uint8_t)~desired[i]) != 0) return false;
  return true;
}
static const char* _ultralightSensitivePageLabel(const char* typeName, uint16_t page) {
  if (!typeName) return nullptr;

  uint16_t dynamicLock = 0xFFFF;
  uint16_t config0 = 0xFFFF;

  if (strcmp(typeName, "NTAG210") == 0 ||
      strcmp(typeName, "Ultralight EV1 11") == 0) {
    config0 = 16;
  } else if (strcmp(typeName, "NTAG212") == 0 ||
             strcmp(typeName, "Ultralight EV1 21") == 0) {
    dynamicLock = 36; config0 = 37;
  } else if (strcmp(typeName, "NTAG213") == 0) {
    dynamicLock = 40; config0 = 41;
  } else if (strcmp(typeName, "NTAG215") == 0) {
    dynamicLock = 130; config0 = 131;
  } else if (strcmp(typeName, "NTAG216") == 0) {
    dynamicLock = 226; config0 = 227;
  } else if (strcmp(typeName, "Ultralight C") == 0) {
    if (page >= 40 && page <= 43) return "Security/config page";
    if (page >= 44 && page <= 47) return "3DES key page";
    return nullptr;
  } else {
    return nullptr;
  }

  if (page == dynamicLock) return "Dynamic lock page";
  if (page == config0 || page == config0 + 1) return "Configuration page";
  if (page == config0 + 2) return "Password page";
  if (page == config0 + 3) return "PACK page";
  return nullptr;
}

static bool _confirmUltralightSensitiveWrite(const char* typeName, uint16_t page) {
  const char* label = _ultralightSensitivePageLabel(typeName, page);
  if (!label) return true;

  static const InputSelectAction::Option opts[] = {
    {"Write anyway", "write"},
  };
  String title = String("Warning: ") + label;
  const char* choice = InputSelectAction::popup(title.c_str(), opts, 1, nullptr);
  return choice && strcmp(choice, "write") == 0;
}


static uint16_t _ultralightConfig0(const char* typeName) {
  if (!typeName) return 0xFFFF;
  if (strcmp(typeName, "NTAG210") == 0 || strcmp(typeName, "Ultralight EV1 11") == 0) return 16;
  if (strcmp(typeName, "NTAG212") == 0 || strcmp(typeName, "Ultralight EV1 21") == 0) return 37;
  if (strcmp(typeName, "NTAG213") == 0) return 41;
  if (strcmp(typeName, "NTAG215") == 0) return 131;
  if (strcmp(typeName, "NTAG216") == 0) return 227;
  return 0xFFFF;
}

// 0 = no password protection for this operation, 1 = password required,
// -1 = protection state could not be read (usually because config is protected).
static int _pn532UltralightNeedsAuth(Adafruit_PN532* nfc, TwoWire* wire,
                                     const char* typeName, uint16_t pages,
                                     bool forRead) {
  const uint16_t cfg = _ultralightConfig0(typeName);
  if (cfg == 0xFFFF || cfg + 1 >= pages) return 0; // legacy UL / UL-C handled separately
  uint8_t c0[4] = {}, c1[4] = {};
  if (!_pn532Type2ReadPageTailSafe(nfc, wire, cfg, pages, c0) ||
      !_pn532Type2ReadPageTailSafe(nfc, wire, cfg + 1, pages, c1)) return -1;
  const uint8_t auth0 = c0[3];
  if (auth0 == 0xFF || auth0 >= pages) return 0;
  if (!forRead) return 1;
  return (c1[0] & 0x80) ? 1 : 0; // ACCESS.PROT: 1 = read+write, 0 = write only
}

static bool _ultralightPasswordFromText(const String& text, uint8_t pwd[4]) {
  if (!text.length()) return false;
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_MD5);
  if (!md) return false;
  uint8_t digest[16] = {};
  if (mbedtls_md(md, reinterpret_cast<const unsigned char*>(text.c_str()),
                 text.length(), digest) != 0) return false;
  memcpy(pwd, digest, 4);
  return true;
}

static bool _promptUltralightPassword(uint8_t pwd[4], const char* title = "Password") {
  String text = InputTextAction::popup(title, "", InputTextAction::INPUT_TEXT);
  if (InputTextAction::wasCancelled()) return false;
  if (!_ultralightPasswordFromText(text, pwd)) {
    ShowStatusAction::show("Password required");
    return false;
  }
  return true;
}

static uint16_t _ultralightDynamicLockPage(const char* typeName) {
  if (!typeName) return 0xFFFF;
  if (strcmp(typeName, "NTAG212") == 0 || strcmp(typeName, "Ultralight EV1 21") == 0) return 36;
  if (strcmp(typeName, "NTAG213") == 0) return 40;
  if (strcmp(typeName, "NTAG215") == 0) return 130;
  if (strcmp(typeName, "NTAG216") == 0) return 226;
  return 0xFFFF;
}

static bool _pn532UltralightPwdAuth(Adafruit_PN532* nfc, TwoWire* wire,
                                     const uint8_t pwd[4]) {
  const uint8_t cmd[5] = {0x1B, pwd[0], pwd[1], pwd[2], pwd[3]};
  uint8_t rsp[8] = {};
  uint8_t len = sizeof(rsp);
  if (!_nfcDataExch(nfc, wire, cmd, sizeof(cmd), rsp, len, 500)) {
    len = sizeof(rsp);
    if (!_nfcCommThru(nfc, wire, cmd, sizeof(cmd), rsp, len, 500)) return false;
  }
  return len >= 2; // successful PWD_AUTH returns the 2-byte PACK (+ optional CRC)
}

static bool _pn532EnsureUltralightAuth(Adafruit_PN532* nfc, TwoWire* wire,
                                       const char* typeName, uint16_t pages,
                                       bool forRead) {
  const int need = _pn532UltralightNeedsAuth(nfc, wire, typeName, pages, forRead);
  if (need == 0) return true;
  if (typeName && strcmp(typeName, "Ultralight C") == 0) return false;
  uint8_t pwd[4] = {};
  if (!_promptUltralightPassword(pwd)) return false;
  if (!_pn532UltralightPwdAuth(nfc, wire, pwd)) {
    ShowStatusAction::show("Authentication failed");
    return false;
  }
  return true;
}

static bool _pn532ReadUltralightProtection(Adafruit_PN532* nfc, TwoWire* wire,
                                            const char* typeName, uint16_t pages,
                                            uint8_t& auth0, uint8_t& access) {
  const uint16_t cfg = _ultralightConfig0(typeName);
  if (cfg == 0xFFFF || cfg + 1 >= pages) return false;
  uint8_t c0[4] = {}, c1[4] = {};
  if (!_pn532Type2ReadPageTailSafe(nfc, wire, cfg, pages, c0) ||
      !_pn532Type2ReadPageTailSafe(nfc, wire, cfg + 1, pages, c1)) return false;
  auth0 = c0[3]; access = c1[0]; return true;
}

static bool _pn532EnsureUltralightAuthForRange(Adafruit_PN532* nfc, TwoWire* wire,
                                                const char* typeName, uint16_t pages,
                                                uint16_t firstPage, uint16_t lastPage,
                                                bool forRead) {
  if (_ultralightConfig0(typeName) == 0xFFFF) return true;
  uint8_t auth0 = 0xFF, access = 0;
  const bool readable = _pn532ReadUltralightProtection(nfc, wire, typeName, pages, auth0, access);
  bool need = !readable;
  if (readable) {
    need = auth0 != 0xFF && auth0 < pages && lastPage >= auth0 &&
           (!forRead || (access & 0x80));
  }
  if (!need) return true;
  uint8_t pwd[4] = {};
  if (!_promptUltralightPassword(pwd)) return false;
  if (!_pn532UltralightPwdAuth(nfc, wire, pwd)) {
    ShowStatusAction::show("Authentication failed");
    return false;
  }
  return true;
}

static bool _pn532BuildUltralightLockMasks(const char* typeName, uint16_t first, uint16_t last,
                                            uint8_t staticMask[2], uint8_t dynamicMask[3]) {
  staticMask[0] = staticMask[1] = 0;
  dynamicMask[0] = dynamicMask[1] = dynamicMask[2] = 0;
  if (!typeName || first > last || first < 4) return false;
  for (uint16_t p = first; p <= last && p <= 15; ++p) {
    if (p <= 7) staticMask[0] |= (uint8_t)(1u << p);
    else staticMask[1] |= (uint8_t)(1u << (p - 8));
  }
  if (last <= 15) return true;
  auto setGroup = [&](uint16_t start, uint16_t end, uint8_t byte, uint8_t bit) {
    if (last >= start && first <= end) dynamicMask[byte] |= (uint8_t)(1u << bit);
  };
  if (strcmp(typeName, "NTAG212") == 0 || strcmp(typeName, "Ultralight EV1 21") == 0) {
    for (uint8_t i = 0; i < 10; ++i) setGroup(16 + i * 2, 17 + i * 2, i / 8, i % 8);
    return true;
  }
  if (strcmp(typeName, "NTAG213") == 0) {
    for (uint8_t i = 0; i < 12; ++i) setGroup(16 + i * 2, 17 + i * 2, i / 8, i % 8);
    return true;
  }
  if (strcmp(typeName, "NTAG215") == 0) {
    for (uint8_t i = 0; i < 7; ++i) setGroup(16 + i * 16, 31 + i * 16, 0, i);
    setGroup(128, 129, 0, 7); return true;
  }
  if (strcmp(typeName, "NTAG216") == 0) {
    for (uint8_t i = 0; i < 8; ++i) setGroup(16 + i * 16, 31 + i * 16, 0, i);
    for (uint8_t i = 0; i < 5; ++i) setGroup(144 + i * 16, 159 + i * 16, 1, i);
    setGroup(224, 225, 1, 5); return true;
  }
  if (strcmp(typeName, "NTAG210") == 0 || strcmp(typeName, "Ultralight EV1 11") == 0 ||
      strcmp(typeName, "Ultralight") == 0) return last <= 15;
  return false;
}

// ── title ──────────────────────────────────────────────────────────────────

static void renderTagPrompt(const char* message, int bx, int by, int bw, int bh) {
  auto& lcd = Uni.Lcd;
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString(message, bx + bw / 2, by + bh / 2);
}

static void renderOperationTitle(const char* title) {
  Header header;
  header.render(title);
}

const char* PN532I2cScreen::title() {
  switch (_state) {
    case STATE_MAIN_MENU:       return "PN532 I2C";
    case STATE_INFO:            return "Firmware Info";
    case STATE_SCAN_RESULT:     return "Tag Details";
    case STATE_SCAN_14A:        return "Scan Tag";
    case STATE_MIFARE_MENU:     return "MIFARE Classic";
    case STATE_MIFARE_TAG_MENU: return "Tag Operations";
    case STATE_MIFARE_NDEF_MENU:return "NDEF Operations";
    case STATE_MIFARE_ATTACKS_MENU:return "Attacks";
    case STATE_MIFARE_KEYS_MENU:return "Keys";
    case STATE_MIFARE_KEY_DB_SELECT:return "Dictionaries";
    case STATE_MIFARE_KEY_DB_VIEW:return _keyDbViewTitle.length() ? _keyDbViewTitle.c_str() : "Dictionary";
    case STATE_MIFARE_DUMP:     return "Tag Details";
    case STATE_MIFARE_DUMP_HEX: return "Memory Dump";
    case STATE_MIFARE_KEYS:     return "Check Known Keys";
    case STATE_MIFARE_DUMP_SELECT:return "Dump Files";
    case STATE_MIFARE_WRITE_PREVIEW:return "Write to Tag";
    case STATE_DICT_SELECT:     return "Dictionary Attack";
    case STATE_ULTRALIGHT_MENU: return "Ultralight / NTAG";
    case STATE_ULTRALIGHT_TAG_MENU:return "Tag Operations";
    case STATE_ULTRALIGHT_ADVANCED_MENU:return "Advanced";
    case STATE_ULTRALIGHT_NDEF_MENU:return "NDEF Operations";
    case STATE_MAGIC_MENU:      return "Magic Card";
    case STATE_MAGIC_DETECT:    return "Detect Magic";
    case STATE_RAW_RESULT:      return "Read Memory";
    case STATE_ULTRALIGHT_DUMP: return "Tag Details";
    case STATE_EMULATE:         return "Emulate Card";
    case STATE_NTAG_MENU:       return "Emulate NDEF";
    case STATE_NDEF_WRITE_MENU: return "Write NDEF";
    case STATE_NDEF_RESULT:     return "NDEF Details";
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
  if (_state == STATE_SCAN_14A) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) _goMain();
      else if (dir == INavigation::DIR_PRESS) _doScan14A();
    }
    return;
  }
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
  if (_state == STATE_MAGIC_DETECT) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        _goMagic();
      } else if (dir == INavigation::DIR_PRESS && !_magicDetectDone) {
        _doDetectMagic();
      }
    }
    return;
  }
  if (_state == STATE_MIFARE_DUMP || _state == STATE_MIFARE_DUMP_HEX) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        if (_state == STATE_MIFARE_DUMP_HEX) {
          _showTagDetails();
        } else {
          _hasDump = false;
          _dumpComplete = false;
          _goMifareTag();
        }
      } else if (dir == INavigation::DIR_PRESS && _hasDump) {
        if (_dumpComplete) _showDumpActions();
        else {
          _resumeReadAfterDict = true;
          _doDictionaryPicker();
        }
      } else {
        _scrollView.onNav(dir);
      }
    }
    return;
  }
  if (_state == STATE_MIFARE_WRITE_PREVIEW) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        if (_writePreviewFromFile) _doWriteDumpFromFilePicker();
        else _showTagDetails();
      } else if (dir == INavigation::DIR_PRESS) {
        if (_writePreviewSourceUidKnown) {
          static constexpr InputSelectAction::Option uidOpts[] = {
            {"Replace UID", "replace"},
            {"Preserve UID", "preserve"},
          };
          const char* choice = InputSelectAction::popup(
              "UID", uidOpts, 2, _writePreviewReplaceUid ? "replace" : "preserve");
          if (!choice) { render(); return; }
          _writePreviewReplaceUid = strcmp(choice, "replace") == 0;
          for (uint8_t i = 0; i < _rowCount; ++i) {
            if (_rowLabels[i] == "UID Action") {
              _rowValues[i] = _writePreviewReplaceUid ? "Replace" : "Preserve";
              _rows[i] = { _rowLabels[i].c_str(), _rowValues[i] };
              break;
            }
          }
          render();
        }
        // Preserve the source Tag Details context: writing scans the target and
        // updates _uid/_atqa/_sak. The CU write screen returns to the source
        // Tag Details after a successful in-memory copy, so mirror that here.
        uint8_t sourceContextUid[7] = {};
        const uint8_t sourceContextUidLen = _uidLen;
        const uint8_t sourceContextSak = _sak;
        const uint16_t sourceContextAtqa = _atqa;
        const auto sourceContextKeys = _mfKeys;
        if (sourceContextUidLen) memcpy(sourceContextUid, _uid, sourceContextUidLen);

        const bool ok = _doWriteDumpToTag(
            _dumpImg, _dumpLen,
            _writePreviewSourceUidKnown ? _writePreviewSourceUid : nullptr,
            _writePreviewSourceUidKnown ? _writePreviewSourceUidLen : 0);
        if (!_writePreviewFromFile) {
          memcpy(_uid, sourceContextUid, sourceContextUidLen);
          _uidLen = sourceContextUidLen;
          _sak = sourceContextSak;
          _atqa = sourceContextAtqa;
          _mfKeys = sourceContextKeys;
        }

        if (ok) {
          if (_writePreviewFromFile) _goMifareTag();
          else _showTagDetails();
        } else {
          // Match CU: failed writes remain on the source preview so the user
          // can retry or back out without rebuilding the source selection.
          render();
        }
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
          else _goNdefWrite();
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

  if (_state == STATE_ULTRALIGHT_DUMP) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        _goUltralightTag();
      } else if (dir == INavigation::DIR_PRESS) {
        _showUltralightDumpActions();
      } else {
        _scrollView.onNav(dir);
      }
    }
    return;
  }

  if (_state == STATE_INFO || _state == STATE_MIFARE_KEYS ||
      _state == STATE_MIFARE_KEY_DB_VIEW || _state == STATE_RAW_RESULT) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        if (_state == STATE_MIFARE_KEYS) _goMifareKeys();
        else if (_state == STATE_MIFARE_KEY_DB_VIEW) _openKeyDatabases();
        else if (_state == STATE_RAW_RESULT) _goUltralightAdvanced();
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
  if (_state == STATE_MAGIC_DETECT) {
    _magicLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  if (_state == STATE_INFO || _state == STATE_SCAN_RESULT ||
      _state == STATE_MIFARE_DUMP || _state == STATE_MIFARE_DUMP_HEX ||
      _state == STATE_MIFARE_WRITE_PREVIEW ||
      _state == STATE_MIFARE_KEYS || _state == STATE_MIFARE_KEY_DB_VIEW ||
      _state == STATE_RAW_RESULT || _state == STATE_ULTRALIGHT_DUMP || _state == STATE_NDEF_RESULT ||
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
        case 0: _goMifareTag(); break;
        case 1: _goMifareNdef(); break;
        case 2: _goMifareAttacks(); break;
        case 3: _goMifareKeys(); break;
      }
      break;
    case STATE_MIFARE_ATTACKS_MENU:
      if (index == 0) { _resumeReadAfterDict = false; _doDictionaryPicker(); }
      break;
    case STATE_MIFARE_KEYS_MENU:
      if (index == 0) _doShowKeys();
      else if (index == 1) { _keyDbPickDir = _dictPath; _openKeyDatabases(); }
      break;
    case STATE_MIFARE_KEY_DB_SELECT:
      _openKeyDatabase(index);
      break;
    case STATE_MIFARE_TAG_MENU:
      switch (index) {
        case 0: _doReadTag(); break;
        case 1: _doWriteDumpFromFilePicker(); break;
        case 2: _doEraseTag(); break;
      }
      break;
    case STATE_MIFARE_NDEF_MENU:
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
        case 3:
          _ndefTarget = NDEF_TARGET_MIFARE_CLASSIC;
          // Standalone Format NDEF must establish the same card context as
          // Read/Write NDEF before checking Classic dimensions.
          renderOperationTitle("Format NDEF");
          if (_scanCardOrShow(5000)) _formatClassic1kNdef();
          _goMifareNdef();
          break;
      }
      break;
    case STATE_ULTRALIGHT_MENU:
      if (index == 0) _goUltralightTag();
      else if (index == 1) _goUltralightNdef();
      break;
    case STATE_ULTRALIGHT_TAG_MENU:
      if (index == 0) _doUltralightReadTag();
      else if (index == 1) _doUltralightWriteTag();
      else if (index == 2) _doUltralightEraseTag();
      else if (index == 3) _goUltralightAdvanced();
      break;
    case STATE_ULTRALIGHT_ADVANCED_MENU:
      if (index == 0) _doUltralightReadPages();
      else if (index == 1) _doUltralightWritePage();
      else if (index == 2) _doUltralightSetPassword();
      else if (index == 3) _doUltralightRemovePassword();
      else if (index == 4) _doUltralightConfigureProtection();
      else if (index == 5) _doUltralightLockTag();
      break;
    case STATE_ULTRALIGHT_NDEF_MENU:
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
        case 3:
          _ndefTarget = NDEF_TARGET_ULTRALIGHT;
          _doFormatNdef();
          break;
      }
      break;
    case STATE_MIFARE_WRITE_PREVIEW:
      if (_writePreviewFromFile) _doWriteDumpFromFilePicker();
      else _showTagDetails();
      break;
    case STATE_MIFARE_DUMP_SELECT:
      _doWriteDumpFileSelected(index);
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
        case 0: _goDetectMagic(); break;
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
    case STATE_MAGIC_DETECT:
      _goMagic();
      break;
    case STATE_MIFARE_TAG_MENU:
    case STATE_MIFARE_NDEF_MENU:
    case STATE_MIFARE_ATTACKS_MENU:
    case STATE_MIFARE_KEYS_MENU:
      _goMifare();
      break;
    case STATE_ULTRALIGHT_TAG_MENU:
    case STATE_ULTRALIGHT_NDEF_MENU:
      _goUltralight();
      break;
    case STATE_ULTRALIGHT_ADVANCED_MENU:
      _goUltralightTag();
      break;
    case STATE_MIFARE_DUMP_SELECT:
      if (_dumpPickDir == _dumpPath || _dumpPickDir.length() == 0) {
        _dumpPickDir = "";
        _goMifareTag();
      } else {
        int slash = _dumpPickDir.lastIndexOf('/');
        _dumpPickDir = (slash > 0) ? _dumpPickDir.substring(0, slash) : _dumpPath;
        _doWriteDumpFromFilePicker();
      }
      break;
    case STATE_MIFARE_KEY_DB_SELECT:
      if (_keyDbPickDir == _dictPath || _keyDbPickDir.length() == 0) {
        _keyDbPickDir = "";
        _goMifareKeys();
      } else {
        int slash = _keyDbPickDir.lastIndexOf('/');
        _keyDbPickDir = (slash > 0) ? _keyDbPickDir.substring(0, slash) : _dictPath;
        _openKeyDatabases();
      }
      break;
    case STATE_DICT_SELECT:
      if (_dictPickDir == _dictPath || _dictPickDir.length() == 0) {
        _dictPickDir = "";
        _goMifareAttacks();
      } else {
        int slash = _dictPickDir.lastIndexOf('/');
        _dictPickDir = (slash > 0) ? _dictPickDir.substring(0, slash) : _dictPath;
        _doDictionaryPicker();
      }
      break;
    case STATE_NTAG_MENU:
      _goMain();
      break;
    case STATE_RAW_RESULT:
    case STATE_ULTRALIGHT_DUMP:
      _goUltralightTag();
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
        else _goNdefWrite();
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

void PN532I2cScreen::_goMifareAttacks() {
  _state = STATE_MIFARE_ATTACKS_MENU;
  setItems(_mfAttackItems);
  render();
}

void PN532I2cScreen::_goMifareKeys() {
  _state = STATE_MIFARE_KEYS_MENU;
  setItems(_mfKeysItems);
  render();
}

void PN532I2cScreen::_goScan14A() {
  _state = STATE_SCAN_14A;
  auto& lcd = Uni.Lcd;
  const int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Place tag on reader...", bx + bw / 2, by + bh / 2);
}

void PN532I2cScreen::_goMifareTag() {
  _state = STATE_MIFARE_TAG_MENU;
  setItems(_mfTagItems);
  render();
}

void PN532I2cScreen::_goMifareNdef() {
  _state = STATE_MIFARE_NDEF_MENU;
  setItems(_mfNdefItems);
  render();
}

void PN532I2cScreen::_goUltralight() {
  _state = STATE_ULTRALIGHT_MENU;
  setItems(_ulItems);
  render();
}

void PN532I2cScreen::_goUltralightTag() {
  _state = STATE_ULTRALIGHT_TAG_MENU;
  setItems(_ulTagItems);
  render();
}

void PN532I2cScreen::_goUltralightAdvanced() {
  _state = STATE_ULTRALIGHT_ADVANCED_MENU;
  setItems(_ulAdvancedItems);
  render();
}

void PN532I2cScreen::_goUltralightNdef() {
  _state = STATE_ULTRALIGHT_NDEF_MENU;
  setItems(_ulNdefItems);
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
  if (_ndefTarget == NDEF_TARGET_MIFARE_CLASSIC) _goMifareNdef();
  else _goUltralightNdef();
}

void PN532I2cScreen::_goMagic() {
  _state = STATE_MAGIC_MENU;
  setItems(_magicItems);
  render();
}

void PN532I2cScreen::_goDetectMagic() {
  _state = STATE_MAGIC_DETECT;
  _magicDetectDone = false;
  _magicLog.clear();
  _magicLog.addLine("Detect Magic", TFT_CYAN);
  _magicLog.addLine("[Press] Start", TFT_DARKGREY);
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
  renderTagPrompt("Place tag on reader...", bodyX(), bodyY(), bodyW(), bodyH());
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
  ShowStatusAction::show("No tag detected");
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
  lcd.drawString("Place tag on reader...", bx + bw / 2, by + bh / 2);

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
  if (!ok) { ShowStatusAction::show("No tag detected", 1200); _goMain(); return; }

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
  if (_sak == 0x09 || _sak == 0x08 || _sak == 0x18) {
    const MagicCardType magic = _detectMagicType();
    _pushRow("Magic", magicCardTypeName(magic));
  }
  const bool supported = (_sak == 0x09 || _sak == 0x08 || _sak == 0x18 || _sak == 0x00);
  if (!supported) _pushRow("Status", "Tag not supported");
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

void PN532I2cScreen::_loadSavedKeys() {
  if (!Uni.Storage || !Uni.Storage->isAvailable() || !_hasCard) return;

  String uid = _hexUid(_uid, _uidLen);
  uid.replace(":", "");
  String path = String("/unigeek/nfc/keys/") + uid + ".txt";
  String content = Uni.Storage->readFile(path.c_str());
  if (content.length() == 0) return;

  auto dims = _mfDims(_sak);
  int start = 0;
  while (start < (int)content.length()) {
    int nl = content.indexOf('\n', start);
    if (nl < 0) nl = content.length();
    String line = content.substring(start, nl);
    line.trim();

    int sector = -1;
    char keyType = 0;
    char hex[13] = {};
    if (sscanf(line.c_str(), "S%d %c %12s", &sector, &keyType, hex) == 3 &&
        sector >= 0 && sector < (int)dims.first) {
      uint8_t raw[6];
      if (_parseHexKeyI2c(String(hex), raw)) {
        NFCUtility::MIFARE_Key key(raw[0], raw[1], raw[2], raw[3], raw[4], raw[5]);
        if (keyType == 'A' || keyType == 'a') _mfKeys[sector].first = key;
        else if (keyType == 'B' || keyType == 'b') _mfKeys[sector].second = key;
      }
    }
    start = nl + 1;
  }
}

void PN532I2cScreen::_saveKeys() {
  if (!Uni.Storage || !Uni.Storage->isAvailable() || !_hasCard) return;
  auto dims = _mfDims(_sak);
  if (dims.first == 0) return;

  Uni.Storage->makeDir("/unigeek/nfc/keys");
  String uid = _hexUid(_uid, _uidLen);
  uid.replace(":", "");
  String path = String("/unigeek/nfc/keys/") + uid + ".txt";
  String buf;

  for (size_t sector = 0; sector < dims.first; ++sector) {
    for (uint8_t kt = 0; kt < 2; ++kt) {
      auto& slot = kt ? _mfKeys[sector].second : _mfKeys[sector].first;
      if (!slot) continue;
      const auto kv = slot.value();
      char line[48];
      snprintf(line, sizeof(line), "S%02u %c %02X%02X%02X%02X%02X%02X\n",
               (unsigned)sector, kt ? 'B' : 'A',
               kv[0], kv[1], kv[2], kv[3], kv[4], kv[5]);
      buf += line;
    }
  }
  if (buf.length() > 0) {
    Uni.Storage->writeFile(path.c_str(), buf.c_str());
    MfcKeyStore::updateDiscoveredDictionary(Uni.Storage, buf);
  }
}

bool PN532I2cScreen::_discoverDefaultKeys(bool checkingProgress) {
  if (!_hasCard) return false;
  auto dims = _mfDims(_sak);
  if (dims.first == 0) return false;

  const size_t totalSectors = dims.first;
  _mfKeys.fill({});

  _loadSavedKeys();
  ProgressView::init();
  bool keyFound = false;

  for (size_t sector = 0; sector < totalSectors; sector++) {
    uint32_t trailer = (sector < 32) ? (sector * 4 + 3) : (128 + (sector - 32) * 16 + 15);
    for (uint8_t kt = 0; kt < 2; kt++) {
      bool useKeyB = (kt == 1);
      auto& slot = useKeyB ? _mfKeys[sector].second : _mfKeys[sector].first;
      char msg[48];
      const size_t keyIndex = sector * 2u + kt + 1u;
      const size_t totalKeys = totalSectors * 2u;
      if (checkingProgress) {
        snprintf(msg, sizeof(msg), "Checking keys (%u/%u)...",
                 (unsigned)keyIndex, (unsigned)totalKeys);
      } else {
        snprintf(msg, sizeof(msg), "Authenticating sectors (%u/%u)...",
                 (unsigned)(sector + 1), (unsigned)totalSectors,
                 useKeyB ? "B" : "A");
      }
      int pct = (int)((keyIndex - 1u) * 100u / totalKeys);
      ProgressView::progress(msg, pct);

      // Persisted per-UID keys are tried first, but never trusted blindly.
      if (slot) {
        const auto kv = slot.value();
        if (_nfc->mifareclassic_AuthenticateBlock(
              _uid, _uidLen, trailer, useKeyB ? 1 : 0, (uint8_t*)kv.data())) {
          keyFound = true;
          uint8_t rUid[7]; uint8_t rLen;
          _nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, rUid, &rLen, 200);
          continue;
        }
        slot.reset();
        uint8_t rUid[7]; uint8_t rLen;
        _nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, rUid, &rLen, 200);
      }

      for (const auto& key : NFCUtility::getDefaultKeys()) {
        const auto kv = key.value();
        if (_nfc->mifareclassic_AuthenticateBlock(
              _uid, _uidLen, trailer, useKeyB ? 1 : 0, (uint8_t*)kv.data())) {
          slot = key;
          if (!keyFound) {
            keyFound = true;
            int n = Achievement.inc("nfc_key_found");
            if (n == 1) Achievement.unlock("nfc_key_found");
          }
          break;
        }
        uint8_t rUid[7]; uint8_t rLen;
        _nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, rUid, &rLen, 200);
      }
    }
  }

  ProgressView::finish();
  if (keyFound) _saveKeys();
  return keyFound;
}

bool PN532I2cScreen::_hasReadableKeyForEverySector() const {
  auto dims = _mfDims(_sak);
  if (dims.first == 0) return false;
  for (size_t sector = 0; sector < dims.first; ++sector) {
    if (!_mfKeys[sector].first && !_mfKeys[sector].second) return false;
  }
  return true;
}

void PN532I2cScreen::_doAuthenticate() {
  if (!_hasCard && !_scanCardOrShow(5000)) { _goMifare(); return; }
  if (_mfDims(_sak).first == 0) { ShowStatusAction::show("Not MIFARE Classic"); _goMifare(); return; }
  _discoverDefaultKeys();
  _goMifare();
}

void PN532I2cScreen::_doReadTag() {
  renderOperationTitle("Read Tag");
  if (!_scanCardOrShow(5000)) { _goMifareTag(); return; }
  if (_mfDims(_sak).first == 0) { ShowStatusAction::show("Not MIFARE Classic"); _goMifareTag(); return; }

  _discoverDefaultKeys();
  if (!_hasReadableKeyForEverySector()) {
    static const InputSelectAction::Option opts[] = {
      {"Dictionary Attack", "dict"},
      {"Read Partial",      "partial"},
    };
    const char* r = InputSelectAction::popup("Missing sector keys", opts, 2, nullptr);
    if (!r) { _goMifareTag(); return; }
    if (strcmp(r, "dict") == 0) {
      _resumeReadAfterDict = true;
      _doDictionaryPicker();
      return;
    }
  }
  _doDumpMemory();
}

void PN532I2cScreen::_doDumpMemory() {
  if (!_hasCard) { ShowStatusAction::show("Authenticate first"); _goMifare(); return; }
  auto dims = _mfDims(_sak);
  if (dims.first == 0) { ShowStatusAction::show("Not MIFARE Classic"); _goMifare(); return; }

  _state = STATE_MIFARE_DUMP;
  _resetRows();
  _hasDump = false;
  _dumpComplete = false;
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

  // Keep block 0 as raw memory. SAK and ATQA are anticollision metadata and
  // are not part of the MIFARE Classic manufacturer block. The normal read
  // loop below fills all 16 bytes when block 0 is readable; otherwise the
  // zero-initialized bytes correctly remain unknown instead of fabricating
  // a non-standard manufacturer block.

  int readCount = 0;
  ProgressView::init();

  for (size_t blk = 0; blk < totalBlocks; blk++) {
    size_t sector  = (blk < 128) ? (blk / 4) : ((blk - 128) / 16 + 32);
    uint32_t trailer = (sector < 32) ? (sector * 4 + 3) : (128 + (sector - 32) * 16 + 15);
    int pct = (int)(blk * 100 / totalBlocks);
    char msg[40];
    snprintf(msg, sizeof(msg), "Reading blocks (%u/%u)...",
             (unsigned)(blk + 1), (unsigned)totalBlocks);
    ProgressView::progress(msg, pct);

    auto& slotA = _mfKeys[sector].first;
    auto& slotB = _mfKeys[sector].second;
    bool useKeyB = !slotA && (bool)slotB;
    auto& slot   = useKeyB ? slotB : slotA;
    if (!slot) continue;

    const auto kv = slot.value();
    if (!_nfc->mifareclassic_AuthenticateBlock(
          _uid, _uidLen, trailer, useKeyB ? 1 : 0, (uint8_t*)kv.data())) {
      // Try re-select + re-auth
      uint8_t rUid[7]; uint8_t rLen;
      if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, rUid, &rLen, 200)) {
        if (!_nfc->mifareclassic_AuthenticateBlock(
              _uid, _uidLen, trailer, useKeyB ? 1 : 0, (uint8_t*)kv.data())) {
          continue;
        }
      } else { continue; }
    }

    uint8_t data[16];
    if (!_nfc->mifareclassic_ReadDataBlock((uint8_t)blk, data)) continue;
    readCount++;
    memcpy(&_dumpImg[blk * 16], data, 16);
  }

  // A MIFARE Classic trailer read does not expose Key A as ordinary memory,
  // and Key B may also be hidden by the access conditions. Reinsert keys that
  // UniGeek actually discovered so the saved raw image is useful as a complete
  // Classic dump, matching the Chameleon Ultra dump path. Preserve the access
  // bits and GPB exactly as read.
  for (size_t sector = 0; sector < totalSectors; ++sector) {
    const size_t trailerBlock =
        (sector < 32) ? (sector * 4 + 3)
                      : (128 + (sector - 32) * 16 + 15);
    uint8_t* trailer = &_dumpImg[trailerBlock * 16];
    if (_mfKeys[sector].first) {
      const auto& keyA = _mfKeys[sector].first.value();
      memcpy(trailer, keyA.data(), 6);
    }
    if (_mfKeys[sector].second) {
      const auto& keyB = _mfKeys[sector].second.value();
      memcpy(trailer + 10, keyB.data(), 6);
    }
  }

  _dumpComplete = (readCount == (int)totalBlocks);
  _hasDump = true;

  int n = Achievement.inc("nfc_dump_memory");
  if (n == 1) Achievement.unlock("nfc_dump_memory");

  ProgressView::finish();
  _showTagDetails();
}

void PN532I2cScreen::_showTagDetails() {
  if (!_hasDump) { _goMifareTag(); return; }
  auto dims = _mfDims(_sak);
  if (dims.first == 0) { _goMifareTag(); return; }

  _state = STATE_MIFARE_DUMP;
  _resetRows();
  _pushRow("Type", _inferType(_sak, _atqa));
  _pushRow("UID", _hexUid(_uid, _uidLen));

  char atqa[8];
  snprintf(atqa, sizeof(atqa), "%04X", _atqa);
  _pushRow("ATQA", atqa);
  char sak[6];
  snprintf(sak, sizeof(sak), "%02X", _sak);
  _pushRow("SAK", sak);

  _pushRow("Size", String((unsigned)_dumpLen) + " bytes");
  _pushRow("Sectors", String((unsigned)dims.first));
  _pushRow("Blocks", String((unsigned)dims.second));

  size_t sectorsWithKey = 0;
  for (size_t s = 0; s < dims.first; s++) {
    if (_mfKeys[s].first || _mfKeys[s].second) sectorsWithKey++;
  }
  _pushRow("Keys", String((unsigned)sectorsWithKey) + "/" + String((unsigned)dims.first) + " sectors");
  _pushRow("Status", _dumpComplete ? "Complete" : "Partial");
  if (_dumpComplete) _appendDumpNdefDetails();
  _pushRow("[Press]", _dumpComplete ? "Actions" : "Dictionary Attack");

  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532I2cScreen::_appendDumpNdefDetails() {
  if (!_hasDump || !_dumpComplete) return;

  auto dims = _mfDims(_sak);
  if (dims.first == 0) return;
  _appendDumpNdefDetails(_dumpImg, _dumpLen, dims.first);
}

void PN532I2cScreen::_appendDumpNdefDetails(const uint8_t* dump, size_t dumpLen,
                                              size_t totalSectors) {
  if (!dump || totalSectors == 0) return;
  const size_t totalBlocks = totalSectors == 5 ? 20u :
                             (totalSectors == 40 ? 256u : 64u);
  if (dumpLen < totalBlocks * 16u) return;

  uint8_t sectors[39] = {};
  size_t sectorCount = 0;

  auto addIfNdef = [&](uint8_t sector, uint8_t lo, uint8_t hi) {
    if (sector >= totalSectors || sectorCount >= sizeof(sectors)) return;
    if (lo == 0x03 && hi == 0xE1) sectors[sectorCount++] = sector;
  };

  // MAD1: block 1 maps sectors 1..7; block 2 maps sectors 8..15.
  const uint8_t* b1 = &dump[1u * 16u];
  const uint8_t* b2 = &dump[2u * 16u];
  for (uint8_t sector = 1; sector <= 7; ++sector) {
    const size_t off = 2u + (size_t)(sector - 1u) * 2u;
    addIfNdef(sector, b1[off], b1[off + 1u]);
  }
  for (uint8_t sector = 8; sector <= 15; ++sector) {
    const size_t off = (size_t)(sector - 8u) * 2u;
    addIfNdef(sector, b2[off], b2[off + 1u]);
  }

  // MAD2 lives in sector 16 on Classic 4K and maps sectors 17..39.
  if (totalSectors > 16 && dumpLen >= 67u * 16u) {
    const uint8_t* m0 = &dump[64u * 16u];
    const uint8_t* m1 = &dump[65u * 16u];
    const uint8_t* m2 = &dump[66u * 16u];
    for (uint8_t sector = 17; sector <= 23; ++sector) {
      const size_t off = 2u + (size_t)(sector - 17u) * 2u;
      addIfNdef(sector, m0[off], m0[off + 1u]);
    }
    for (uint8_t sector = 24; sector <= 31; ++sector) {
      const size_t off = (size_t)(sector - 24u) * 2u;
      addIfNdef(sector, m1[off], m1[off + 1u]);
    }
    for (uint8_t sector = 32; sector <= 39; ++sector) {
      const size_t off = (size_t)(sector - 32u) * 2u;
      addIfNdef(sector, m2[off], m2[off + 1u]);
    }
  }

  if (sectorCount == 0) {
    _pushRow("NDEF", "Not found");
    return;
  }

  size_t areaLen = 0;
  for (size_t i = 0; i < sectorCount; ++i)
    areaLen += (sectors[i] < 32) ? 48u : 240u;

  uint8_t* area = new uint8_t[areaLen];
  if (!area) {
    _pushRow("NDEF", "Present");
    return;
  }

  size_t out = 0;
  for (size_t i = 0; i < sectorCount; ++i) {
    const uint8_t sector = sectors[i];
    const size_t firstBlock = (sector < 32)
        ? (size_t)sector * 4u
        : 128u + (size_t)(sector - 32u) * 16u;
    const uint8_t dataBlocks = (sector < 32) ? 3 : 15;
    for (uint8_t bi = 0; bi < dataBlocks; ++bi) {
      memcpy(area + out, &dump[(firstBlock + bi) * 16u], 16u);
      out += 16u;
    }
  }

  const uint8_t* ndef = nullptr;
  size_t ndefLen = 0;
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
    if (tlv == 0x03) {
      ndef = area + pos;
      ndefLen = len;
      break;
    }
    pos += len;
  }

  if (!ndef) {
    _pushRow("NDEF", "Not found");
    delete[] area;
    return;
  }
  if (ndefLen == 0) {
    _pushRow("NDEF", "Empty");
    delete[] area;
    return;
  }

  NdefParser::Result parsed;
  if (!NdefParser::parse(ndef, ndefLen, parsed)) {
    _pushRow("NDEF", "Invalid record");
    delete[] area;
    return;
  }

  switch (parsed.kind) {
    case NdefParser::RECORD_TEXT:
      _pushRow("NDEF", "Text");
      if (parsed.language.length()) _pushRow("Language", parsed.language);
      if (parsed.encoding == "UTF-16") _pushRow("Text", "(UTF-16 raw)");
      else if (parsed.text.length()) _pushWrappedRow("Text", parsed.text);
      break;
    case NdefParser::RECORD_URL:
      _pushRow("NDEF", "URL");
      _pushWrappedRow("URL", parsed.uri);
      break;
    case NdefParser::RECORD_PHONE:
      _pushRow("NDEF", "Phone");
      _pushWrappedRow("Phone", parsed.phone);
      break;
    case NdefParser::RECORD_EMAIL:
      _pushRow("NDEF", "Email");
      _pushWrappedRow("Email", parsed.email);
      break;
    case NdefParser::RECORD_VCARD:
      _pushRow("NDEF", "vCard");
      if (parsed.contact.length()) _pushWrappedRow("Contact", parsed.contact);
      if (parsed.company.length()) _pushWrappedRow("Company", parsed.company);
      if (parsed.address.length()) _pushWrappedRow("Address", parsed.address);
      if (parsed.phone.length()) _pushWrappedRow("Phone", parsed.phone);
      if (parsed.email.length()) _pushWrappedRow("Email", parsed.email);
      if (parsed.website.length()) _pushWrappedRow("Website", parsed.website);
      break;
    default:
      _pushRow("NDEF", "Unsupported");
      break;
  }

  delete[] area;
}

void PN532I2cScreen::_showDumpHex() {
  if (!_hasDump) { _showTagDetails(); return; }

  _state = STATE_MIFARE_DUMP_HEX;
  _resetRows();
  _scrollView.resetScroll();
  const size_t blocks = _dumpLen / 16u;
  for (size_t blk = 0; blk < blocks; blk++) {
    // A full 16-byte block does not fit beside the row label in
    // ScrollListView. Split it into two 8-byte rows so View Dump always
    // shows every byte instead of clipping the left side of the hex string.
    _pushRow("B" + String((unsigned)blk) + " 0-7",
             _hexBlock(&_dumpImg[blk * 16], 8));
    _pushRow("B" + String((unsigned)blk) + " 8-F",
             _hexBlock(&_dumpImg[blk * 16 + 8], 8));
  }
  _pushRow("[Press]", "Actions");
  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532I2cScreen::_doShowKeys() {
  // Known Keys is a read-only view of persisted keys for the scanned UID.
  // No authentication or attack is needed to inspect the saved results.
  if (!_scanCardOrShow(5000)) { _goMifareKeys(); return; }
  auto dims = _mfDims(_sak);
  if (dims.first == 0) { ShowStatusAction::show("Not MIFARE Classic"); _goMifareKeys(); return; }
  _mfKeys.fill({});
  _loadSavedKeys();

  _state = STATE_MIFARE_KEYS;
  _resetRows();
  _pushRow("UID", _hexUid(_uid, _uidLen));
  char sak[8]; snprintf(sak, sizeof(sak), "%02X", _sak);
  _pushRow("SAK", sak);
  size_t known = 0;
  for (size_t sector = 0; sector < dims.first; ++sector) {
    if (_mfKeys[sector].first) ++known;
    if (_mfKeys[sector].second) ++known;
  }
  char count[16]; snprintf(count, sizeof(count), "%u / %u", (unsigned)known, (unsigned)(dims.first * 2u));
  _pushRow("Keys", count);
  for (size_t sector = 0; sector < dims.first; ++sector) {
    String a = "---", b = "---";
    if (_mfKeys[sector].first) a = String(_mfKeys[sector].first.c_str().c_str());
    if (_mfKeys[sector].second) b = String(_mfKeys[sector].second.c_str().c_str());
    _pushRow("S" + String((int)sector) + " A", a);
    _pushRow("S" + String((int)sector) + " B", b);
  }
  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532I2cScreen::_openKeyDatabases() {
  _state = STATE_MIFARE_KEY_DB_SELECT;
  if (!_keyDbPickDir.length()) _keyDbPickDir = _dictPath;
  _browser.root = _dictPath;
  uint8_t n = _browser.load(this, _keyDbPickDir, ".txt", nullptr, BrowseFileView::STEM_CAPITALIZED,
                            _keyDbPickDir == _dictPath ? "discovered.txt" : nullptr);
  setItems(_browser.items(), n);
  render();
  if (!n && _keyDbPickDir == _dictPath) ShowStatusAction::show("No dictionaries");
}

void PN532I2cScreen::_openKeyDatabase(uint8_t index) {
  if (index >= _browser.count()) return;
  const auto& e = _browser.entry(index);
  if (e.isDir) { _keyDbPickDir = e.path; _openKeyDatabases(); return; }
  if (!Uni.Storage || !Uni.Storage->isAvailable()) { ShowStatusAction::show("Storage unavailable"); return; }
  String content = Uni.Storage->readFile(e.path.c_str());
  _resetRows();
  int pos = 0;
  while (pos < (int)content.length() && _rowCount < MAX_ROWS) {
    int nl = content.indexOf('\n', pos);
    if (nl < 0) nl = content.length();
    String line = content.substring(pos, nl); line.trim();
    if (line.length() && !line.startsWith("#")) {
      _pushRow(String((unsigned)(_rowCount + 1u)), line);
    }
    pos = nl + 1;
  }
  if (!_rowCount) { ShowStatusAction::show("No keys in file"); return; }
  _keyDbViewTitle = e.label;
  _state = STATE_MIFARE_KEY_DB_VIEW;
  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532I2cScreen::_doDictionaryPicker() {
  // Standalone Dictionary Attack always scans the tag now. This gives it the
  // same valid UID/SAK context as the Read Tag -> Dictionary Attack path and
  // avoids accidentally reusing a previous tag still cached in _hasCard.
  if (_resumeReadAfterDict) {
    if (!_hasCard && !_scanCardOrShow(5000)) {
      _resumeReadAfterDict = false;
      _goMifareAttacks();
      return;
    }
  } else {
    if (!_scanCardOrShow(5000)) {
      _goMifareAttacks();
      return;
    }
    // Start from the persisted per-UID state.  The standalone attack should
    // test only slots that are not already in Known Keys; clearing the
    // whole table here made the PN532 rediscover and report the same 32 keys
    // on every run.  This is intentionally lighter than the CU pre-check:
    // PN532 authentication is slower and needs frequent PICC re-selection, so
    // persisted slots are used as the baseline and missing slots are attacked.
    _mfKeys.fill({});
    _loadSavedKeys();
  }
  if (_mfDims(_sak).first == 0) {
    _resumeReadAfterDict = false;
    ShowStatusAction::show("Not MIFARE Classic");
    _goMifareAttacks();
    return;
  }

  _state = STATE_DICT_SELECT;
  if (_dictPickDir.length() == 0) _dictPickDir = _dictPath;
  _browser.root = _dictPath;
  uint8_t n = _browser.load(this, _dictPickDir, ".txt");
  if (n == 0 && _dictPickDir == _dictPath) {
    ShowStatusAction::show("No dictionary files");
    _goMifareAttacks();
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

  // Match the Chameleon Ultra dictionary-attack UX: live scrolling key
  // attempts with a status/progress bar, rather than a progress-only screen.
  LogView actionLog;
  actionLog.clear();
  struct DictUiCtx { const char* status; int pct; } ui = {"Starting...", 0};
  auto statusCb = [](Sprite& sp, int barY, int width, void* userData) {
    auto* ctx = static_cast<DictUiCtx*>(userData);
    sp.setTextDatum(TL_DATUM);
    sp.setTextColor(TFT_CYAN);
    sp.drawString(ctx->status, 2, barY);
    char pctBuf[8];
    snprintf(pctBuf, sizeof(pctBuf), "%d%%", ctx->pct);
    sp.setTextDatum(TR_DATUM);
    sp.setTextColor(TFT_WHITE);
    sp.drawString(pctBuf, width - 2, barY);
  };
  char liveStatus[48] = "Starting...";
  ui.status = liveStatus;
  actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), statusCb, &ui);

  for (size_t sector = 0; sector < totalSectors; sector++) {
    uint32_t trailer = (sector < 32) ? (sector * 4 + 3) : (128 + (sector - 32) * 16 + 15);
    for (uint8_t kt = 0; kt < 2; kt++) {
      bool useKeyB = (kt == 1);
      auto& slot = useKeyB ? _mfKeys[sector].second : _mfKeys[sector].first;
      if (slot) continue;

      ui.pct = (int)((sector * 2 + kt) * 100 / (totalSectors * 2));
      bool found = false;
      for (uint8_t k = 0; k < keyCount; k++) {
        snprintf(liveStatus, sizeof(liveStatus), "S%u %c %02X%02X%02X%02X%02X%02X",
                 (unsigned)sector, useKeyB ? 'B' : 'A',
                 keys[k][0], keys[k][1], keys[k][2], keys[k][3], keys[k][4], keys[k][5]);
        actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), statusCb, &ui);

        bool ok = _nfc->mifareclassic_AuthenticateBlock(
              _uid, _uidLen, trailer, useKeyB ? 1 : 0, keys[k]);
        char line[48];
        snprintf(line, sizeof(line), "S%u %c: %02X%02X%02X%02X%02X%02X",
                 (unsigned)sector, useKeyB ? 'B' : 'A',
                 keys[k][0], keys[k][1], keys[k][2], keys[k][3], keys[k][4], keys[k][5]);
        actionLog.addLine(line, ok ? TFT_GREEN : TFT_RED);
        actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), statusCb, &ui);

        if (ok) {
          slot = NFCUtility::MIFARE_Key(keys[k][0], keys[k][1], keys[k][2],
                                        keys[k][3], keys[k][4], keys[k][5]);
          recovered++;
          found = true;
          break;
        }
        uint8_t rUid[7]; uint8_t rLen;
        _nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, rUid, &rLen, 200);
      }
      if (!found) {
        char nf[32];
        snprintf(nf, sizeof(nf), "  S%u %c: not found",
                 (unsigned)sector, useKeyB ? 'B' : 'A');
        actionLog.addLine(nf, TFT_RED);
        actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), statusCb, &ui);
      }
    }
  }

  ui.pct = 100;
  snprintf(liveStatus, sizeof(liveStatus), recovered > 0 ? "Keys updated: %d new" : "No new keys found", recovered);
  actionLog.addLine(liveStatus, recovered > 0 ? TFT_GREEN : TFT_RED);
  actionLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), statusCb, &ui);
  if (recovered > 0) {
    _saveKeys();
    int n = Achievement.inc("nfc_dict_attack");
    if (n == 1) Achievement.unlock("nfc_dict_attack");
  }
  char msg[48];
  if (recovered > 0) snprintf(msg, sizeof(msg), "%d new key%s saved to Known Keys", recovered, recovered == 1 ? "" : "s");
  else snprintf(msg, sizeof(msg), "No new keys found");
  ShowStatusAction::show(msg);
  if (_resumeReadAfterDict) {
    _resumeReadAfterDict = false;
    _doDumpMemory();
  } else {
    _goMifareAttacks();
  }
}


bool PN532I2cScreen::_detectUltralightTag(uint16_t& pages, const char*& typeName) {
  pages = 0;
  typeName = nullptr;
  uint8_t uid[7] = {};
  uint8_t uidLen = 0;
  bool found = false;
  const uint32_t start = millis();
  while (millis() - start < 5000) {
    Uni.update();
    if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 200)) {
      found = true;
      break;
    }
    delay(40);
  }
  if (!found) return false;

  memcpy(_uid, uid, uidLen);
  _uidLen = uidLen;
  _atqa = ((uint16_t)pn532_packetbuffer[9] << 8) | pn532_packetbuffer[10];
  _sak = pn532_packetbuffer[11];
  _hasCard = true;
  if (_sak != 0x00) return false;

  typeName = _inferType2Variant();
  if (!typeName) return false;

  if (strcmp(typeName, "NTAG210") == 0) pages = 20;
  else if (strcmp(typeName, "NTAG212") == 0) pages = 41;
  else if (strcmp(typeName, "NTAG213") == 0) pages = 45;
  else if (strcmp(typeName, "NTAG215") == 0) pages = 135;
  else if (strcmp(typeName, "NTAG216") == 0) pages = 231;
  else if (strcmp(typeName, "Ultralight EV1 11") == 0) pages = 20;
  else if (strcmp(typeName, "Ultralight EV1 21") == 0) pages = 41;
  else if (strcmp(typeName, "Ultralight C") == 0) pages = 48;
  else if (strcmp(typeName, "Ultralight") == 0) pages = 16;
  return pages != 0;
}

bool PN532I2cScreen::_readUltralightDump(uint16_t pages) {
  const size_t bytes = (size_t)pages * 4u;
  if (!pages || bytes > sizeof(_dumpImg)) return false;
  memset(_dumpImg, 0, bytes);

  ProgressView::init();
  for (uint16_t page = 0; page < pages; ++page) {
    char msg[36];
    snprintf(msg, sizeof(msg), "Reading pages (%u/%u)...",
             (unsigned)(page + 1), (unsigned)pages);
    ProgressView::progress(msg, (int)((uint32_t)page * 100u / pages));
    uint8_t data[4] = {};
    if (!_pn532Type2ReadPageTailSafe(_nfc, _wire, page, pages, data)) {
      // Ultralight C pages 44..47 contain the 3DES key and are intentionally
      // unreadable. Preserve a complete image with zeroes in that region.
      if (pages == 48 && page >= 44) continue;
      ProgressView::finish();
      _dumpLen = 0;
      _hasDump = false;
      return false;
    }
    memcpy(_dumpImg + page * 4u, data, 4);
  }
  ProgressView::finish();
  _dumpLen = bytes;
  _hasDump = true;
  _dumpComplete = true;
  return true;
}

void PN532I2cScreen::_showUltralightTagDetails(const char* typeName, uint16_t pages) {
  _ulTypeName = typeName ? typeName : "Ultralight / NTAG";
  _ulPages = pages;
  _state = STATE_ULTRALIGHT_DUMP;
  _resetRows();
  _pushRow("Type", _ulTypeName);
  _pushRow("UID", _hexUid(_uid, _uidLen));
  _pushRow("Pages", String(pages));
  _pushRow("Dump", String(_dumpLen) + " bytes");

  // Match MIFARE Classic Tag Details: identity/dump first, then concise
  // family-specific status, then NDEF information.
  const uint16_t cfg = _ultralightConfig0(_ulTypeName.c_str());
  if (cfg != 0xFFFF && (size_t)(cfg + 1) * 4u + 3u < _dumpLen) {
    const uint8_t auth0 = _dumpImg[(size_t)cfg * 4u + 3u];
    const uint8_t access = _dumpImg[(size_t)(cfg + 1) * 4u];
    if (auth0 == 0xFF || auth0 >= pages) {
      _pushRow("Protection", "None");
    } else {
      _pushRow("Protection", (access & 0x80) ? "Read + Write" : "Write only");
      _pushRow("From Page", String(auth0));
    }
  }

  bool locked = false;
  if (_dumpLen >= 12) locked = (_dumpImg[10] != 0 || _dumpImg[11] != 0);
  uint16_t dyn = 0xFFFF;
  if (_ulTypeName == "NTAG212" || _ulTypeName == "Ultralight EV1 21") dyn = 36;
  else if (_ulTypeName == "NTAG213") dyn = 40;
  else if (_ulTypeName == "NTAG215") dyn = 130;
  else if (_ulTypeName == "NTAG216") dyn = 226;
  if (dyn != 0xFFFF && (size_t)dyn * 4u + 2u < _dumpLen) {
    locked = locked || _dumpImg[(size_t)dyn * 4u] ||
             _dumpImg[(size_t)dyn * 4u + 1u] || _dumpImg[(size_t)dyn * 4u + 2u];
  }
  _pushRow("Lock Status", locked ? "Locked pages" : "Unlocked");

  const uint8_t* ndef = nullptr;
  size_t ndefLen = 0;
  NdefParser::Result parsed;
  if (NdefParser::extractType2Ndef(_dumpImg, _dumpLen, &ndef, &ndefLen) &&
      NdefParser::parse(ndef, ndefLen, parsed)) {
    switch (parsed.kind) {
      case NdefParser::RECORD_TEXT:
        _pushRow("NDEF", "Text");
        if (parsed.language.length()) _pushRow("Language", parsed.language);
        if (parsed.encoding == "UTF-16") _pushRow("Text", "(UTF-16 raw)");
        else if (parsed.text.length()) _pushWrappedRow("Text", parsed.text);
        break;
      case NdefParser::RECORD_URL:
        _pushRow("NDEF", "URL");
        _pushWrappedRow("URL", parsed.uri);
        break;
      case NdefParser::RECORD_PHONE:
        _pushRow("NDEF", "Phone");
        _pushWrappedRow("Phone", parsed.phone);
        break;
      case NdefParser::RECORD_EMAIL:
        _pushRow("NDEF", "Email");
        _pushWrappedRow("Email", parsed.email);
        break;
      case NdefParser::RECORD_VCARD:
        _pushRow("NDEF", "vCard");
        if (parsed.contact.length()) _pushWrappedRow("Contact", parsed.contact);
        if (parsed.company.length()) _pushWrappedRow("Company", parsed.company);
        if (parsed.address.length()) _pushWrappedRow("Address", parsed.address);
        if (parsed.phone.length()) _pushWrappedRow("Phone", parsed.phone);
        if (parsed.email.length()) _pushWrappedRow("Email", parsed.email);
        if (parsed.website.length()) _pushWrappedRow("Website", parsed.website);
        break;
      default: _pushRow("NDEF", "Unsupported"); break;
    }
  } else {
    _pushRow("NDEF", "Not found");
  }
  _pushRow("[Press]", "Actions");
  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532I2cScreen::_saveUltralightDump(const char* typeName) {
  if (!_hasDump || !_dumpLen || !Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable");
    render();
    return;
  }
  Uni.Storage->makeDir("/unigeek/nfc");
  Uni.Storage->makeDir(_dumpPath);
  String uid = _hexUid(_uid, _uidLen); uid.replace(":", "");
  String safeType = typeName ? String(typeName) : String("Ultralight");
  safeType.replace(" / ", "-"); safeType.replace(" ", "-");
  String name = InputTextAction::popup("Save dump", safeType + "_" + uid);
  if (InputTextAction::wasCancelled() || name.length() == 0) { render(); return; }
  if (name.endsWith(".bin")) name.remove(name.length() - 4);
  const String filename = name + ".bin";
  fs::File f = Uni.Storage->open((String(_dumpPath) + "/" + filename).c_str(), "w");
  const bool ok = f && f.write(_dumpImg, _dumpLen) == _dumpLen;
  if (f) f.close();
  ShowStatusAction::show(ok ? (String("Saved: ") + filename).c_str() : "Save failed");
  render();
}

bool PN532I2cScreen::_writeUltralightNtag215Dump(const uint8_t* dump, size_t len) {
  static constexpr uint16_t kBytes = 135u * 4u;
  static constexpr uint8_t kFirst = 4;
  static constexpr uint8_t kLast = 129;
  if (!dump || len != kBytes) return false;

  renderOperationTitle("Write to Tag");
  renderTagPrompt("Place tag on reader...", bodyX(), bodyY(), bodyW(), bodyH());
  uint16_t pages = 0; const char* typeName = nullptr;
  if (!_detectUltralightTag(pages, typeName) || pages != 135 ||
      !typeName || strcmp(typeName, "NTAG215") != 0) {
    ShowStatusAction::show("Tag must be NTAG215");
    return false;
  }
  if (!_pn532EnsureUltralightAuth(_nfc, _wire, typeName, pages, false)) return false;

  ProgressView::init();
  bool ok = true;
  for (uint16_t page = kFirst; page <= kLast; ++page) {
    char msg[36];
    const uint16_t done = page - kFirst + 1;
    snprintf(msg, sizeof(msg), "Writing pages (%u/126)...", (unsigned)done);
    ProgressView::progress(msg, (int)((uint32_t)(done - 1) * 100u / 126u));
    if (!_pn532Type2WritePage(_nfc, _wire, (uint8_t)page, dump + page * 4u)) {
      ok = false;
      break;
    }
  }
  ProgressView::finish();
  ShowStatusAction::show(ok ? "Tag written" : "Tag write failed");
  return ok;
}

void PN532I2cScreen::_showUltralightDumpActions() {
  static const InputSelectAction::Option opts[] = {
    {"Save Dump", "save"},
    {"Write to Tag", "write"},
  };
  const char* r = InputSelectAction::popup("Dump Actions", opts, 2, nullptr);
  if (!r) { render(); return; }
  if (strcmp(r, "save") == 0) {
    _saveUltralightDump(_ulTypeName.c_str());
  } else {
    if (_ulTypeName != "NTAG215" || _ulPages != 135 || _dumpLen != 540) {
      ShowStatusAction::show("Write supports NTAG215");
      render();
      return;
    }
    uint8_t sourceUid[7] = {};
    const uint8_t sourceUidLen = _uidLen;
    const uint16_t sourceAtqa = _atqa;
    const uint8_t sourceSak = _sak;
    if (sourceUidLen) memcpy(sourceUid, _uid, sourceUidLen);
    _writeUltralightNtag215Dump(_dumpImg, _dumpLen);
    memcpy(_uid, sourceUid, sourceUidLen);
    _uidLen = sourceUidLen;
    _atqa = sourceAtqa;
    _sak = sourceSak;
    _showUltralightTagDetails("NTAG215", 135);
  }
}

void PN532I2cScreen::_doUltralightReadTag() {
  renderOperationTitle("Read Tag");
  renderTagPrompt("Place tag on reader...", bodyX(), bodyY(), bodyW(), bodyH());
  uint16_t pages = 0; const char* typeName = nullptr;
  if (!_detectUltralightTag(pages, typeName)) {
    ShowStatusAction::show("Unsupported / no tag");
    _goUltralightTag();
    return;
  }
  if (!_pn532EnsureUltralightAuth(_nfc, _wire, typeName, pages, true)) {
    _goUltralightTag();
    return;
  }
  if (!_readUltralightDump(pages)) {
    ShowStatusAction::show("Read failed");
    _goUltralightTag();
    return;
  }
  _showUltralightTagDetails(typeName, pages);
}

void PN532I2cScreen::_doUltralightWriteTag() {
  const uint8_t n = _browser.load(this, _dumpPath,
      BrowseFileView::Mode(BrowseFileView::Mode::FILE_ONLY, ".bin", 540));
  if (!n) { ShowStatusAction::show("No NTAG215 .bin"); _goUltralightTag(); return; }
  const uint8_t count = min((uint8_t)10, n);
  InputSelectAction::Option opts[10];
  String vals[10];
  for (uint8_t i = 0; i < count; ++i) {
    vals[i] = String(i);
    opts[i] = {_browser.entry(i).name.c_str(), vals[i].c_str()};
  }
  const char* r = InputSelectAction::popup("NTAG215 dump", opts, count, nullptr);
  if (!r) { render(); return; }
  const uint8_t idx = (uint8_t)atoi(r);
  if (idx >= count || !Uni.Storage) { render(); return; }
  fs::File f = Uni.Storage->open(_browser.entry(idx).path.c_str(), "r");
  if (!f || f.size() != 540) { if (f) f.close(); ShowStatusAction::show("Invalid NTAG215 dump"); render(); return; }
  uint8_t dump[540];
  const bool loaded = f.read(dump, sizeof(dump)) == sizeof(dump);
  f.close();
  if (!loaded) { ShowStatusAction::show("Read file failed"); render(); return; }
  _writeUltralightNtag215Dump(dump, sizeof(dump));
  _goUltralightTag();
}

void PN532I2cScreen::_doUltralightEraseTag() {
  renderOperationTitle("Erase Tag");
  renderTagPrompt("Place tag on reader...", bodyX(), bodyY(), bodyW(), bodyH());
  uint16_t pages = 0; const char* typeName = nullptr;
  if (!_detectUltralightTag(pages, typeName) || pages != 135 ||
      !typeName || strcmp(typeName, "NTAG215") != 0) {
    ShowStatusAction::show("Tag must be NTAG215");
    _goUltralightTag();
    return;
  }
  if (!_pn532EnsureUltralightAuth(_nfc, _wire, typeName, pages, false)) {
    _goUltralightTag();
    return;
  }

  uint8_t image[NfcDumpBuilder::NTAG215_SIZE] = {};
  size_t imageLen = 0;
  if (!NfcDumpBuilder::buildNtag215(_uid, nullptr, 0, image, imageLen,
                                    NfcDumpBuilder::NTAG215_SIZE) || imageLen != 540) {
    ShowStatusAction::show("Cannot build empty tag");
    _goUltralightTag();
    return;
  }

  ProgressView::init();
  bool ok = true;
  for (uint16_t page = 4; page <= 129; ++page) {
    char msg[36];
    const uint16_t done = page - 3;
    snprintf(msg, sizeof(msg), "Erasing pages (%u/126)...", (unsigned)done);
    ProgressView::progress(msg, (int)((uint32_t)(done - 1) * 100u / 126u));
    if (!_pn532Type2WritePage(_nfc, _wire, (uint8_t)page, image + page * 4u)) {
      ok = false;
      break;
    }
  }
  ProgressView::finish();
  _hasDump = false; _dumpLen = 0;
  ShowStatusAction::show(ok ? "Tag erased" : "Erase failed");
  _goUltralightTag();
}

void PN532I2cScreen::_doUltralightReadPages() {
  renderOperationTitle("Read Memory");
  renderTagPrompt("Place tag on reader...", bodyX(), bodyY(), bodyW(), bodyH());
  uint16_t pages = 0; const char* typeName = nullptr;
  if (!_detectUltralightTag(pages, typeName)) {
    ShowStatusAction::show("Unsupported / no tag");
    _goUltralightAdvanced();
    return;
  }

  if (!_pn532EnsureUltralightAuthForRange(_nfc, _wire, typeName, pages, 0, pages - 1, true)) {
    _goUltralightAdvanced();
    return;
  }

  _state = STATE_RAW_RESULT;
  _resetRows();
  _pushRow("Type", typeName);
  _pushRow("UID", _hexUid(_uid, _uidLen));
  _pushRow("Pages", String(pages));

  ProgressView::init();
  for (uint16_t page = 0; page < pages; ++page) {
    char msg[36];
    snprintf(msg, sizeof(msg), "Reading pages (%u/%u)...",
             (unsigned)(page + 1), (unsigned)pages);
    ProgressView::progress(msg, (int)((uint32_t)page * 100u / pages));
    uint8_t data[4] = {};
    if (!_pn532Type2ReadPageTailSafe(_nfc, _wire, page, pages, data)) {
      if (pages == 48 && page >= 44) {
        _pushRow("P" + String(page), "Unreadable (key)");
        continue;
      }
      _pushRow("P" + String(page), "Read failed");
      break;
    }
    _pushRow("P" + String(page), _hexBlock(data, 4));
  }
  ProgressView::finish();
  _scrollView.setRows(_rows, _rowCount);
  render();
}

void PN532I2cScreen::_doUltralightWritePage() {
  renderOperationTitle("Write Page");
  renderTagPrompt("Place tag on reader...", bodyX(), bodyY(), bodyW(), bodyH());
  uint16_t pages = 0; const char* typeName = nullptr;
  if (!_detectUltralightTag(pages, typeName)) {
    ShowStatusAction::show("Unsupported / no tag");
    _goUltralightAdvanced();
    return;
  }
  if (pages <= 4) { ShowStatusAction::show("No writable pages"); _goUltralightAdvanced(); return; }

  const int page = InputNumberAction::popup(
      (String("Page (4..") + String(pages - 1) + ")").c_str(), 4, pages - 1, 4);
  if (InputNumberAction::wasCancelled()) { _goUltralightAdvanced(); return; }

  if (!_confirmUltralightSensitiveWrite(typeName, (uint16_t)page)) {
    _goUltralightAdvanced();
    return;
  }

  String hex = InputTextAction::popup("Page data (8 hex)", "", InputTextAction::INPUT_HEX);
  if (InputTextAction::wasCancelled()) { _goUltralightAdvanced(); return; }
  hex.replace(" ", ""); hex.replace(":", "");
  if (hex.length() != 8) { ShowStatusAction::show("Need 8 hex chars"); _goUltralightAdvanced(); return; }

  uint8_t data[4] = {};
  for (int i = 0; i < 4; ++i) {
    char b[3] = {hex[i * 2], hex[i * 2 + 1], 0};
    char* e = nullptr; unsigned long v = strtoul(b, &e, 16);
    if (!e || *e) { ShowStatusAction::show("Bad hex"); _goUltralightAdvanced(); return; }
    data[i] = (uint8_t)v;
  }
  if (!_pn532EnsureUltralightAuthForRange(_nfc, _wire, typeName, pages, page, page, false)) {
    _goUltralightAdvanced();
    return;
  }
  render();
  const bool ok = _pn532Type2WritePage(_nfc, _wire, (uint8_t)page, data);
  ShowStatusAction::show(ok ? "Page written" : "Write failed");
  _goUltralightAdvanced();
}


void PN532I2cScreen::_doUltralightLockTag() {
  renderOperationTitle("Lock Tag");
  renderTagPrompt("Place tag on reader...", bodyX(), bodyY(), bodyW(), bodyH());
  uint16_t pages = 0; const char* typeName = nullptr;
  if (!_detectUltralightTag(pages, typeName) || !typeName ||
      strcmp(typeName, "Ultralight C") == 0) {
    ShowStatusAction::show("Unsupported tag type"); _goUltralightAdvanced(); return;
  }

  uint16_t lastUser = _ultralightDynamicLockPage(typeName);
  if (lastUser != 0xFFFF) --lastUser; else lastUser = 15;
  if (lastUser < 4) { ShowStatusAction::show("No lockable memory"); _goUltralightAdvanced(); return; }

  static const InputSelectAction::Option warn[] = {{"Lock permanently", "lock"}};
  if (!InputSelectAction::popup("Make tag read-only?", warn, 1, nullptr)) {
    _goUltralightAdvanced(); return;
  }

  uint8_t sm[2] = {}, dm[3] = {};
  if (!_pn532BuildUltralightLockMasks(typeName, 4, lastUser, sm, dm)) {
    ShowStatusAction::show("Unsupported lock layout"); _goUltralightAdvanced(); return;
  }
  const uint16_t dyn = _ultralightDynamicLockPage(typeName);
  const bool needsDyn = dyn != 0xFFFF && (dm[0] || dm[1] || dm[2]);
  const uint16_t authPage = needsDyn ? dyn : 2;
  if (!_pn532EnsureUltralightAuthForRange(_nfc, _wire, typeName, pages,
                                           authPage, authPage, false)) {
    _goUltralightAdvanced(); return;
  }

  bool ok = true;
  if (sm[0] || sm[1]) {
    uint8_t p2[4] = {0, 0, sm[0], sm[1]};
    ok = _pn532Type2WritePage(_nfc, _wire, 2, p2);
  }
  if (ok && needsDyn) {
    uint8_t cur[4] = {};
    if (!_pn532Type2ReadPageTailSafe(_nfc, _wire, dyn, pages, cur)) ok = false;
    else {
      cur[0] |= dm[0]; cur[1] |= dm[1]; cur[2] |= dm[2];
      ok = _pn532Type2WritePage(_nfc, _wire, (uint8_t)dyn, cur);
    }
  }
  ShowStatusAction::show(ok ? "Tag locked" : "Lock failed");
  _goUltralightAdvanced();
}

void PN532I2cScreen::_doUltralightSetPassword() {
  renderOperationTitle("Set Password");
  renderTagPrompt("Place tag on reader...", bodyX(), bodyY(), bodyW(), bodyH());
  uint16_t pages = 0; const char* typeName = nullptr;
  if (!_detectUltralightTag(pages, typeName) || _ultralightConfig0(typeName) == 0xFFFF) {
    ShowStatusAction::show("Password not supported"); _goUltralightAdvanced(); return;
  }
  const uint16_t cfg = _ultralightConfig0(typeName);

  uint8_t auth0 = 0xFF, access = 0;
  bool protectionReadable = _pn532ReadUltralightProtection(
      _nfc, _wire, typeName, pages, auth0, access);
  const bool wasProtected = !protectionReadable || (auth0 != 0xFF && auth0 < pages);

  // If the tag is already protected, authenticate with the current text
  // password before changing PWD or configuration pages.
  if (wasProtected && !_pn532EnsureUltralightAuthForRange(
          _nfc, _wire, typeName, pages, cfg, cfg + 2, false)) {
    _goUltralightAdvanced(); return;
  }

  uint8_t c0[4] = {}, c1[4] = {};
  if (!_pn532Type2ReadPageTailSafe(_nfc, _wire, cfg, pages, c0) ||
      !_pn532Type2ReadPageTailSafe(_nfc, _wire, cfg + 1, pages, c1)) {
    ShowStatusAction::show("Read config failed"); _goUltralightAdvanced(); return;
  }
  if (c1[0] & 0x40) {
    // CFGLCK does not prevent changing PWD itself, but on an unprotected tag
    // it would prevent us from enabling the expected write-only protection.
    if (!wasProtected) {
      ShowStatusAction::show("Configuration locked"); _goUltralightAdvanced(); return;
    }
  }

  uint8_t newPwd[4] = {};
  if (!_promptUltralightPassword(newPwd, "New Password")) {
    _goUltralightAdvanced(); return;
  }

  bool ok = _pn532Type2WritePage(_nfc, _wire, (uint8_t)(cfg + 2), newPwd);
  if (ok && !wasProtected) {
    // Match the user-facing semantics of NFC Tools: setting a password on an
    // open tag makes the user area write-protected while reads stay public.
    c1[0] &= (uint8_t)~0x80u; // PROT=0: write-only protection
    c0[3] = 4;               // protect user memory from page 4
    ok = _pn532Type2WritePage(_nfc, _wire, (uint8_t)(cfg + 1), c1);
    if (ok) ok = _pn532Type2WritePage(_nfc, _wire, (uint8_t)cfg, c0);
  }

  ShowStatusAction::show(ok ? "Password set" : "Password write failed");
  _goUltralightAdvanced();
}

void PN532I2cScreen::_doUltralightConfigureProtection() {
  renderOperationTitle("Configure Protection");
  renderTagPrompt("Place tag on reader...", bodyX(), bodyY(), bodyW(), bodyH());
  uint16_t pages = 0; const char* typeName = nullptr;
  if (!_detectUltralightTag(pages, typeName) || _ultralightConfig0(typeName) == 0xFFFF) {
    ShowStatusAction::show("Protection not supported"); _goUltralightAdvanced(); return;
  }
  const uint16_t cfg = _ultralightConfig0(typeName);
  uint8_t auth0Before = 0xFF, accessBefore = 0;
  const bool protectionReadable = _pn532ReadUltralightProtection(
      _nfc, _wire, typeName, pages, auth0Before, accessBefore);
  const bool alreadyProtected = protectionReadable && auth0Before != 0xFF && auth0Before < pages;

  if (alreadyProtected) {
    if (!_pn532EnsureUltralightAuthForRange(_nfc, _wire, typeName, pages, cfg, cfg + 1, false)) {
      _goUltralightAdvanced(); return;
    }
  } else {
    // Before enabling protection on an open tag, verify that the user knows
    // the current text password. Otherwise AUTH0 could be enabled with an
    // unknown/default PWD and make subsequent writes inaccessible.
    uint8_t pwd[4] = {};
    if (!_promptUltralightPassword(pwd, "Password") ||
        !_pn532UltralightPwdAuth(_nfc, _wire, pwd)) {
      if (!InputTextAction::wasCancelled()) ShowStatusAction::show("Authentication failed");
      _goUltralightAdvanced(); return;
    }
  }
  uint8_t c0[4] = {}, c1[4] = {};
  if (!_pn532Type2ReadPageTailSafe(_nfc, _wire, cfg, pages, c0) ||
      !_pn532Type2ReadPageTailSafe(_nfc, _wire, cfg + 1, pages, c1)) {
    ShowStatusAction::show("Read config failed"); _goUltralightAdvanced(); return;
  }
  if (c1[0] & 0x40) { ShowStatusAction::show("Configuration locked"); _goUltralightAdvanced(); return; }

  const int first = InputNumberAction::popup(
      (String("Protect from (4..") + String(pages - 1) + ")").c_str(), 4, pages - 1, 4);
  if (InputNumberAction::wasCancelled()) { _goUltralightAdvanced(); return; }
  static const InputSelectAction::Option modes[] = {
    {"Write only", "w"}, {"Read + Write", "rw"},
  };
  const char* mode = InputSelectAction::popup("Protection", modes, 2, nullptr);
  if (!mode) { _goUltralightAdvanced(); return; }
  const int lim = InputNumberAction::popup("Auth limit (0..7)", 0, 7, 0);
  if (InputNumberAction::wasCancelled()) { _goUltralightAdvanced(); return; }
  if (lim > 0) {
    static const InputSelectAction::Option warn[] = {{"Use auth limit", "yes"}};
    if (!InputSelectAction::popup("Warning: may lock access", warn, 1, nullptr)) {
      _goUltralightAdvanced(); return;
    }
  }
  c1[0] = (uint8_t)((c1[0] & ~0x87u) |
                    (strcmp(mode, "rw") == 0 ? 0x80u : 0u) | (lim & 0x07));
  c0[3] = (uint8_t)first;
  // ACCESS first, AUTH0 last: protection becomes effective only after field
  // reset/POR on supported NXP tags, and this order avoids changing AUTH0 early.
  bool ok = _pn532Type2WritePage(_nfc, _wire, (uint8_t)(cfg + 1), c1);
  if (ok) ok = _pn532Type2WritePage(_nfc, _wire, (uint8_t)cfg, c0);
  ShowStatusAction::show(ok ? "Protection configured" : "Protection failed");
  _goUltralightAdvanced();
}

void PN532I2cScreen::_doUltralightRemovePassword() {
  renderOperationTitle("Remove Password");
  renderTagPrompt("Place tag on reader...", bodyX(), bodyY(), bodyW(), bodyH());
  uint16_t pages = 0; const char* typeName = nullptr;
  if (!_detectUltralightTag(pages, typeName) || _ultralightConfig0(typeName) == 0xFFFF) {
    ShowStatusAction::show("Password not supported"); _goUltralightAdvanced(); return;
  }
  const uint16_t cfg = _ultralightConfig0(typeName);

  uint8_t auth0 = 0xFF, access = 0;
  const bool readable = _pn532ReadUltralightProtection(
      _nfc, _wire, typeName, pages, auth0, access);
  const bool protectedNow = !readable || (auth0 != 0xFF && auth0 < pages);
  if (protectedNow && !_pn532EnsureUltralightAuthForRange(
          _nfc, _wire, typeName, pages, cfg, cfg + 3, false)) {
    _goUltralightAdvanced(); return;
  }

  uint8_t c0[4] = {}, c1[4] = {};
  if (!_pn532Type2ReadPageTailSafe(_nfc, _wire, cfg, pages, c0) ||
      !_pn532Type2ReadPageTailSafe(_nfc, _wire, cfg + 1, pages, c1)) {
    ShowStatusAction::show("Read config failed"); _goUltralightAdvanced(); return;
  }
  if (c1[0] & 0x40) {
    ShowStatusAction::show("Configuration locked"); _goUltralightAdvanced(); return;
  }

  // Restore the password-protection fields to their NXP delivery state.
  // No PACK is exposed in the UI; it is reset internally together with PWD.
  c1[0] &= (uint8_t)~0x87u; // PROT=0, AUTHLIM=0; preserve unrelated bits
  c0[3] = 0xFF;             // AUTH0=FF: password protection disabled
  const uint8_t defaultPwd[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  const uint8_t defaultPack[4] = {0x00, 0x00, 0x00, 0x00};

  bool ok = _pn532Type2WritePage(_nfc, _wire, (uint8_t)(cfg + 1), c1);
  if (ok) ok = _pn532Type2WritePage(_nfc, _wire, (uint8_t)cfg, c0);
  if (ok) ok = _pn532Type2WritePage(_nfc, _wire, (uint8_t)(cfg + 2), defaultPwd);
  if (ok) ok = _pn532Type2WritePage(_nfc, _wire, (uint8_t)(cfg + 3), defaultPack);

  ShowStatusAction::show(ok ? "Password removed" : "Remove failed");
  _goUltralightAdvanced();
}

void PN532I2cScreen::_doReadNdef() {
  renderOperationTitle("Read NDEF");
  _ndefTarget = NDEF_TARGET_ULTRALIGHT;
  _hasNdef = false;
  _ndefLen = 0;
  _ndefCapacity = 0;
  renderTagPrompt("Place tag on reader...", bodyX(), bodyY(), bodyW(), bodyH());

  uint16_t pages = 0;
  const char* typeName = nullptr;
  if (!_detectUltralightTag(pages, typeName)) {
    ShowStatusAction::show("No Type 2 tag");
    _goUltralightNdef();
    return;
  }

  if (!_pn532EnsureUltralightAuth(_nfc, _wire, typeName, pages, true)) {
    _goUltralightNdef();
    return;
  }

  // NFC Forum Type 2 Capability Container (page 3). Byte 2 gives the
  // NDEF data-area capacity in units of 8 bytes, so use the tag's real
  // capacity rather than a fixed page range.
  uint8_t cc[4] = {};
  if (!_pn532Type2ReadPage(_nfc, _wire, 3, cc)) {
    ShowStatusAction::show("Failed to read CC");
    _goUltralightNdef();
    return;
  }
  if (cc[0] != 0xE1 || cc[2] == 0) {
    ShowStatusAction::show("Not NDEF formatted");
    _goUltralightNdef();
    return;
  }

  _ndefCapacity = (size_t)cc[2] * 8u;
  const size_t maxUserBytes = pages > 4 ? ((size_t)pages - 4u) * 4u : 0u;
  const size_t userBytes = (_ndefCapacity < maxUserBytes) ? _ndefCapacity : maxUserBytes;
  if (!userBytes) {
    ShowStatusAction::show("Invalid NDEF capacity");
    _goUltralightNdef();
    return;
  }

  uint8_t* user = new uint8_t[userBytes];
  if (!user) {
    ShowStatusAction::show("Out of memory");
    _goUltralightNdef();
    return;
  }
  memset(user, 0, userBytes);

  const size_t totalPages = (userBytes + 3u) / 4u;
  size_t userLen = 0;
  bool readOk = true;
  ProgressView::init();
  for (size_t i = 0; i < totalPages; ++i) {
    const uint16_t page16 = 4u + (uint16_t)i;
    if (page16 >= pages || page16 > 0xFFu) break;

    char msg[36];
    snprintf(msg, sizeof(msg), "Reading pages (%u/%u)...",
             (unsigned)(i + 1u), (unsigned)totalPages);
    ProgressView::progress(msg, (int)(i * 100u / totalPages));

    uint8_t data[4] = {};
    if (!_pn532Type2ReadPageTailSafe(_nfc, _wire, (uint8_t)page16,
                                     pages, data)) {
      readOk = false;
      break;
    }
    const size_t copy = (userBytes - userLen < 4u) ? userBytes - userLen : 4u;
    memcpy(user + userLen, data, copy);
    userLen += copy;
  }
  ProgressView::finish();

  if (!readOk) {
    delete[] user;
    ShowStatusAction::show("Read failed");
    _goUltralightNdef();
    return;
  }

  const uint8_t* ndef = nullptr;
  size_t ndefLen = 0;
  size_t pos = 0;
  while (pos < userLen) {
    const uint8_t tlv = user[pos++];
    if (tlv == 0x00) continue;
    if (tlv == 0xFE) break;
    if (pos >= userLen) break;

    size_t len = user[pos++];
    if (len == 0xFF) {
      if (pos + 1 >= userLen) break;
      len = ((size_t)user[pos] << 8) | user[pos + 1];
      pos += 2;
    }
    if (pos + len > userLen) break;
    if (tlv == 0x03) {
      ndef = user + pos;
      ndefLen = len;
      break;
    }
    pos += len;
  }

  _showNdefResult(_uid, _uidLen, ndef, ndefLen);
  delete[] user;
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

  // Keep NDEF Details aligned with the Chameleon Ultra presentation.
  // Transport/parser metadata (TNF, raw type and encoding) is intentionally
  // omitted here: the user-facing result focuses on the decoded record.
  switch (parsed.kind) {
    case NdefParser::RECORD_TEXT:
      _pushRow("Record", "Text");
      if (parsed.language.length()) _pushRow("Language", parsed.language);
      _pushWrappedRow("Text", parsed.text);
      break;

    case NdefParser::RECORD_URL:
      _pushRow("Record", "URL");
      _pushWrappedRow("URL", parsed.uri);
      break;

    case NdefParser::RECORD_PHONE:
      _pushRow("Record", "Phone");
      _pushWrappedRow("Phone", parsed.phone);
      break;

    case NdefParser::RECORD_EMAIL:
      _pushRow("Record", "Email");
      _pushWrappedRow("Email", parsed.email);
      break;

    case NdefParser::RECORD_VCARD:
      _pushRow("Record", "vCard");
      if (parsed.contact.length()) _pushWrappedRow("Contact", parsed.contact);
      if (parsed.company.length()) _pushWrappedRow("Company", parsed.company);
      if (parsed.address.length()) _pushWrappedRow("Address", parsed.address);
      if (parsed.phone.length()) _pushWrappedRow("Phone", parsed.phone);
      if (parsed.email.length()) _pushWrappedRow("Email", parsed.email);
      if (parsed.website.length()) _pushWrappedRow("Website", parsed.website);
      break;

    default:
      _pushRow("Record", "Unsupported");
      break;
  }

  if (_hasNdef) {
    _pushRow("[Press]", _ndefWritePreview ? "Write to Tag" : "Actions");
  }

  _scrollView.setRows(_rows, _rowCount);
  render();
}

namespace {
static uint8_t _classicMadCrc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0xC7;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x1D) : (uint8_t)(crc << 1);
  }
  return crc;
}
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

  static const uint8_t madKeys[][6] = {
    {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5},
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
    {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7},
  };

  auto readMadBlock = [&](uint16_t block, uint8_t out[16]) {
    const uint8_t sector = (block < 128) ? (uint8_t)(block / 4u)
                                         : (uint8_t)(32u + (block - 128u) / 16u);
    for (const auto& key : madKeys) {
      if (_classicAuthSector(sector, key) && _nfc->mifareclassic_ReadDataBlock(block, out))
        return true;
    }
    return false;
  };

  uint8_t b1[16] = {};
  uint8_t b2[16] = {};
  if (!readMadBlock(1, b1) || !readMadBlock(2, b2)) return false;

  auto addIfNdef = [&](uint8_t sector, uint8_t application, uint8_t cluster) {
    if (sector >= dims.first || count >= maxSectors) return;
    if ((application == 0x03 && cluster == 0xE1) ||
        (application == 0xE1 && cluster == 0x03))
      sectors[count++] = sector;
  };

  for (uint8_t sec = 1; sec <= 7; ++sec) {
    const size_t off = 2u + (size_t)(sec - 1u) * 2u;
    addIfNdef(sec, b1[off], b1[off + 1u]);
  }
  for (uint8_t sec = 8; sec <= 15; ++sec) {
    const size_t off = (size_t)(sec - 8u) * 2u;
    addIfNdef(sec, b2[off], b2[off + 1u]);
  }

  if (dims.first > 16) {
    uint8_t m0[16] = {};
    uint8_t m1[16] = {};
    uint8_t m2[16] = {};
    if (readMadBlock(64, m0) && readMadBlock(65, m1) && readMadBlock(66, m2)) {
      for (uint8_t sec = 17; sec <= 23; ++sec) {
        const size_t off = 2u + (size_t)(sec - 17u) * 2u;
        addIfNdef(sec, m0[off], m0[off + 1u]);
      }
      for (uint8_t sec = 24; sec <= 31; ++sec) {
        const size_t off = (size_t)(sec - 24u) * 2u;
        addIfNdef(sec, m1[off], m1[off + 1u]);
      }
      for (uint8_t sec = 32; sec <= 39; ++sec) {
        const size_t off = (size_t)(sec - 32u) * 2u;
        addIfNdef(sec, m2[off], m2[off + 1u]);
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
  renderOperationTitle("Read NDEF");
  _ndefTarget = NDEF_TARGET_MIFARE_CLASSIC;
  _hasNdef = false;
  _ndefLen = 0;
  _ndefCapacity = 0;

  if (!_scanCardOrShow(5000)) {
    _goMifareNdef();
    return;
  }

  auto dims = _mfDims(_sak);
  if (dims.first == 0) {
    ShowStatusAction::show("Not MIFARE Classic");
    _goMifareNdef();
    return;
  }

  uint8_t sectors[39] = {};
  size_t sectorCount = 0;
  if (!_classicNdefSectors(sectors, sizeof(sectors), sectorCount)) {
    ShowStatusAction::show("No NDEF sectors in MAD");
    _goMifareNdef();
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
    _goMifareNdef();
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

bool PN532I2cScreen::_formatClassic1kNdef() {
  auto dims = _mfDims(_sak);
  if (dims.first != 16) {
    ShowStatusAction::show("Format supports Classic 1K");
    return false;
  }

  // NFC Forum INITIALISED layout (AN1305): sectors 1 and 2 are NFC
  // sectors, all remaining sectors stay free.
  uint8_t madPayload[31] = {};
  madPayload[0] = 0x01;
  madPayload[1] = 0x03; madPayload[2] = 0xE1;
  madPayload[3] = 0x03; madPayload[4] = 0xE1;

  uint8_t mad1[16] = {};
  uint8_t mad2[16] = {};
  mad1[0] = _classicMadCrc8(madPayload, sizeof(madPayload));
  memcpy(mad1 + 1, madPayload, 15);
  memcpy(mad2, madPayload + 15, 16);

  static const uint8_t madTrailer[16] = {
    0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,
    0x78,0x77,0x88,0xC1,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
  };
  static const uint8_t nfcTrailer[16] = {
    0xD3,0xF7,0xD3,0xF7,0xD3,0xF7,
    0x7F,0x07,0x88,0x40,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
  };
  // Keep formatter authentication aligned with the physically validated
  // Chameleon Ultra path instead of limiting it to the four NFC defaults.
  static const uint8_t candidates[][6] = {
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5},
    {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7}, {0x00,0x00,0x00,0x00,0x00,0x00},
    {0xB0,0xB1,0xB2,0xB3,0xB4,0xB5}, {0x4D,0x3A,0x99,0xC3,0x51,0xDD},
    {0x1A,0x98,0x2C,0x7E,0x45,0x9A}, {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF},
    {0x71,0x4C,0x5C,0x88,0x6E,0x97}, {0x58,0x7E,0xE5,0xF9,0x35,0x0F},
    {0xA0,0x47,0x8C,0xC3,0x90,0x91}, {0x53,0x3C,0xB6,0xC7,0x23,0xF6},
    {0x8F,0xD0,0xA4,0xF2,0x56,0xE9}, {0x00,0x00,0x00,0x00,0x00,0x01},
    {0x11,0x22,0x33,0x44,0x55,0x66}, {0x26,0x97,0x34,0x3B,0x00,0x00},
    {0x12,0x34,0x56,0x78,0x9A,0xBC}, {0xBD,0x49,0x3A,0x39,0x62,0xB6},
  };

  // Prefer the current tag's persisted known keys, then preflight the
  // same candidate set used by Chameleon Ultra. Formatting touches only
  // sectors 0, 1 and 2, so validate one credential per sector up front.
  _mfKeys.fill({});
  _loadSavedKeys();

  auto authSectorKey = [&](uint8_t block, const uint8_t key[6], bool useKeyB) {
    // Failed MIFARE authentication can halt the PICC. Start every probe from
    // a fresh selection, just like _tryWriteMifareBlock() does for writes.
    uint8_t uid[7] = {};
    uint8_t uidLen = 0;
    if (!_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 300)) {
      return false;
    }
    if (uidLen != _uidLen || memcmp(uid, _uid, uidLen) != 0) return false;
    return _nfc->mifareclassic_AuthenticateBlock(
        _uid, _uidLen, block, useKeyB ? 1 : 0, const_cast<uint8_t*>(key)) != 0;
  };

  uint8_t sectorKey[3][6] = {};
  bool sectorKeyB[3] = {};
  for (uint8_t sector = 0; sector < 3; ++sector) {
    const uint8_t block = (uint8_t)(sector * 4u);
    bool found = false;

    auto& savedA = _mfKeys[sector].first;
    auto& savedB = _mfKeys[sector].second;
    if (savedA) {
      const auto ka = savedA.value();
      if (authSectorKey(block, (const uint8_t*)ka.data(), false)) {
        memcpy(sectorKey[sector], ka.data(), 6);
        sectorKeyB[sector] = false;
        found = true;
      }
    }
    if (!found && savedB) {
      const auto kb = savedB.value();
      if (authSectorKey(block, (const uint8_t*)kb.data(), true)) {
        memcpy(sectorKey[sector], kb.data(), 6);
        sectorKeyB[sector] = true;
        found = true;
      }
    }

    for (uint8_t kt = 0; kt < 2 && !found; ++kt) {
      for (const auto& key : candidates) {
        if (authSectorKey(block, key, kt != 0)) {
          memcpy(sectorKey[sector], key, 6);
          sectorKeyB[sector] = (kt != 0);
          found = true;
          break;
        }
      }
    }
    if (!found) {
      ShowStatusAction::show("Format: unknown sector key");
      return false;
    }
  }

  uint8_t zero[16] = {};
  uint8_t emptyNdef[16] = {0x03,0x00,0xFE};
  uint16_t done = 0;
  static constexpr uint16_t total = 11;
  ProgressView::init();

  auto writeWithKnownKey = [&](uint16_t block, const uint8_t data[16]) {
    char msg[40];
    snprintf(msg, sizeof(msg), "Formatting blocks (%u/%u)...",
             (unsigned)(done + 1u), (unsigned)total);
    ProgressView::progress(msg, (int)((uint32_t)done * 100u / total));

    const uint8_t sector = (uint8_t)(block / 4u); // Classic 1K only here.
    auto& keyA = _mfKeys[sector].first;
    auto& keyB = _mfKeys[sector].second;
    const bool madDataBlock = (sector == 0 && (block == 1 || block == 2));

    // MAD1 data blocks use NFC Forum access conditions where authentication
    // with Key A may succeed even though Key A is not permitted to write.
    // On PN532 that can look like a successful write while leaving the MAD
    // unchanged, so exhaust every Key B credential before trying Key A.
    if (madDataBlock) {
      if (keyB) {
        const auto kb = keyB.value();
        if (_tryWriteMifareBlock(block, data, (const uint8_t*)kb.data(), true)) {
          ++done;
          return true;
        }
      }
      if (sectorKeyB[sector] &&
          _tryWriteMifareBlock(block, data, sectorKey[sector], true)) {
        ++done;
        return true;
      }
      for (const auto& key : candidates) {
        if (_tryWriteMifareBlock(block, data, key, true)) {
          ++done;
          return true;
        }
      }
    }

    if (keyA) {
      const auto ka = keyA.value();
      if (_tryWriteMifareBlock(block, data, (const uint8_t*)ka.data(), false)) {
        ++done;
        return true;
      }
    }
    if (!madDataBlock && keyB) {
      const auto kb = keyB.value();
      if (_tryWriteMifareBlock(block, data, (const uint8_t*)kb.data(), true)) {
        ++done;
        return true;
      }
    }
    if (!madDataBlock &&
        _tryWriteMifareBlock(block, data, sectorKey[sector], sectorKeyB[sector])) {
      ++done;
      return true;
    }

    for (const auto& key : candidates) {
      if (_tryWriteMifareBlock(block, data, key, false) ||
          (!madDataBlock && _tryWriteMifareBlock(block, data, key, true))) {
        ++done;
        return true;
      }
    }
    return false;
  };

  bool ok = writeWithKnownKey(1, mad1) && writeWithKnownKey(2, mad2);

  for (uint8_t sector = 1; sector <= 2 && ok; ++sector) {
    const uint16_t first = (uint16_t)sector * 4u;
    for (uint8_t bi = 0; bi < 3 && ok; ++bi) {
      const uint8_t* data = (sector == 1 && bi == 0) ? emptyNdef : zero;
      ok = writeWithKnownKey(first + bi, data);
    }
  }

  if (ok) ok = writeWithKnownKey(3, madTrailer);
  if (ok) ok = writeWithKnownKey(7, nfcTrailer);
  if (ok) ok = writeWithKnownKey(11, nfcTrailer);

  if (ok) ProgressView::progress("Format complete", 100);
  ProgressView::finish();
  ShowStatusAction::show(ok ? "NDEF formatted" : "NDEF format failed");

  if (ok) {
    // Trailer writes change authentication state. Force a fresh select before
    // the caller verifies MAD and continues with the requested NDEF write.
    uint8_t uid[7] = {};
    uint8_t uidLen = 0;
    _nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 400);
  }
  return ok;
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
    static const InputSelectAction::Option opts[] = {
      {"Format NDEF", "format"},
    };
    const char* choice = InputSelectAction::popup("Not NDEF formatted", opts, 1, nullptr);
    render();
    if (!choice || strcmp(choice, "format") != 0 || !_formatClassic1kNdef()) {
      return false;
    }
    // Formatting changes the sector trailers and therefore invalidates the
    // PN532 Crypto1/authentication state. Re-select the tag and then return to
    // the same MAD discovery + write path used by the original, physically
    // validated Write NDEF implementation.
    if (!_scanCardOrShow(5000) ||
        !_classicNdefSectors(sectors, sizeof(sectors), sectorCount)) {
      ShowStatusAction::show("Format verification failed");
      return false;
    }
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
  renderOperationTitle("Erase NDEF");
  _ndefTarget = NDEF_TARGET_MIFARE_CLASSIC;

  if (!_scanCardOrShow(5000)) {
    _goMifareNdef();
    return;
  }
  if (_mfDims(_sak).first == 0) {
    ShowStatusAction::show("Not MIFARE Classic");
    _goMifareNdef();
    return;
  }

  uint8_t sectors[39] = {};
  size_t sectorCount = 0;
  if (!_classicNdefSectors(sectors, sizeof(sectors), sectorCount)) {
    ShowStatusAction::show("Not NDEF formatted");
    _goMifareNdef();
    return;
  }

  static const uint8_t NFC_KEY_A[6] = {
    0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7
  };

  if (!_classicAuthSector(sectors[0], NFC_KEY_A)) {
    ShowStatusAction::show("NDEF sector locked");
    _goMifareNdef();
    return;
  }

  uint8_t blockNo = (uint8_t)((sectors[0] < 32)
                                ? sectors[0] * 4
                                : 128 + (sectors[0] - 32) * 16);
  uint8_t block[16] = {};
  if (!_nfc->mifareclassic_ReadDataBlock(blockNo, block)) {
    ShowStatusAction::show("Read failed");
    _goMifareNdef();
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
  _goMifareNdef();
}


bool PN532I2cScreen::_writeNdefRecord(const uint8_t* ndef, size_t ndefLen) {
  renderOperationTitle("Write NDEF");
  if (_ndefTarget == NDEF_TARGET_MIFARE_CLASSIC) {
    return _writeClassicNdefRecord(ndef, ndefLen);
  }
  return _writeUltralightNdefRecord(ndef, ndefLen);
}

bool PN532I2cScreen::_writeUltralightNdefRecord(const uint8_t* ndef, size_t ndefLen) {
  renderOperationTitle("Write NDEF");
  if (!ndef || ndefLen == 0 || ndefLen > 254) {
    ShowStatusAction::show("NDEF too large");
    return false;
  }

  renderTagPrompt("Place tag on reader...", bodyX(), bodyY(), bodyW(), bodyH());

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
    ShowStatusAction::show("No tag detected");
    return false;
  }

  uint16_t authPages = 0; const char* authType = nullptr;
  if (_detectUltralightTag(authPages, authType) &&
      !_pn532EnsureUltralightAuth(_nfc, _wire, authType, authPages, false)) {
    return false;
  }

  // Read the NFC Forum Type 2 Capability Container from page 3.
  // CC byte 2 gives the data area size in multiples of 8 bytes.
  uint8_t cc[4] = {};
  if (!_pn532Type2ReadPage(_nfc, _wire, 3, cc)) {
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
    snprintf(msg, sizeof(msg), "Writing pages (%u/%u)...",
             (unsigned)currentPage, (unsigned)totalPages);
    ProgressView::progress(msg, (int)(offset * 100 / paddedLen));

    if (!_pn532Type2WritePage(_nfc, _wire, page, &payload[offset])) {
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
  renderOperationTitle("Erase NDEF");
  _ndefTarget = NDEF_TARGET_ULTRALIGHT;
  renderTagPrompt("Place tag on reader...", bodyX(), bodyY(), bodyW(), bodyH());

  uint16_t pages = 0;
  const char* typeName = nullptr;
  if (!_detectUltralightTag(pages, typeName)) {
    ShowStatusAction::show("No Type 2 tag");
    _goUltralightNdef();
    return;
  }
  if (!_pn532EnsureUltralightAuth(_nfc, _wire, typeName, pages, false)) {
    _goUltralightNdef();
    return;
  }

  uint8_t cc[4] = {};
  if (!_pn532Type2ReadPage(_nfc, _wire, 3, cc)) {
    ShowStatusAction::show("Failed to read CC");
    _goUltralightNdef();
    return;
  }
  if (cc[0] != 0xE1) {
    ShowStatusAction::show("Not NDEF formatted");
    _goUltralightNdef();
    return;
  }

  // Logical NDEF erase: zero-length NDEF Message TLV followed by Terminator.
  // Bytes after the terminator remain physically present but are no longer
  // part of the active NDEF message.
  uint8_t emptyNdef[4] = {0x03, 0x00, 0xFE, 0x00};
  const bool success = _pn532Type2WritePage(_nfc, _wire, 4, emptyNdef);

  ShowStatusAction::show(success ? "NDEF erased" : "NDEF erase failed");
  _goUltralightNdef();
}

void PN532I2cScreen::_doFormatNdef() {
  renderOperationTitle("Format NDEF");
  _ndefTarget = NDEF_TARGET_ULTRALIGHT;
  renderTagPrompt("Place tag on reader...", bodyX(), bodyY(), bodyW(), bodyH());

  uint16_t pages = 0;
  const char* typeName = nullptr;
  if (!_detectUltralightTag(pages, typeName)) {
    ShowStatusAction::show("No Type 2 tag");
    _goUltralightNdef();
    return;
  }
  if (!_pn532EnsureUltralightAuth(_nfc, _wire, typeName, pages, false)) {
    _goUltralightNdef();
    return;
  }

  uint8_t cc[4] = {}, desired[4] = {};
  if (!_pn532Type2ReadPage(_nfc, _wire, 3, cc) || !_type2DefaultCc(typeName, desired)) {
    ShowStatusAction::show("Format unsupported");
    _goUltralightNdef();
    return;
  }

  if (!_type2CcIsValid(cc)) {
    if (!_type2CcCanProgramSafely(cc, desired)) {
      ShowStatusAction::show("CC cannot be safely formatted");
      _goUltralightNdef();
      return;
    }
    if (!_pn532Type2WritePage(_nfc, _wire, 3, desired)) {
      ShowStatusAction::show("CC write failed");
      _goUltralightNdef();
      return;
    }
  }

  const uint8_t emptyNdef[4] = {0x03, 0x00, 0xFE, 0x00};
  const bool ok = _pn532Type2WritePage(_nfc, _wire, 4, emptyNdef);
  ShowStatusAction::show(ok ? "NDEF formatted" : "Format failed");
  _goUltralightNdef();
}

bool PN532I2cScreen::_resetAndReselect() {
  if (!_nfc || !_nfc->SAMConfig()) return false;
  uint8_t uid[7] = {}, uidLen = 0;
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 250))
      return true;
    delay(30);
  }
  return false;
}

MagicCardType PN532I2cScreen::_detectMagicType() {
  // Probe Gen3 first. Gen3 / APDU cards accept a direct READ of block 0
  // without MIFARE authentication; a prior Gen1A unlock would make the same
  // read possible and could cause a false Gen3 positive.
  {
    static const uint8_t readBlock0[] = {0x30, 0x00};
    uint8_t resp[20] = {};
    uint8_t rlen = sizeof(resp);
    if (_nfcDataExch(_nfc, _wire, readBlock0, sizeof(readBlock0), resp, rlen, 500) &&
        rlen >= 16) {
      _resetAndReselect();
      return MagicCardType::GEN3;
    }
  }

  if (!_resetAndReselect()) return MagicCardType::NONE;

  // Gen1A backdoor sequence. After normal ISO14443A activation the PICC is
  // ACTIVE, but the 0x40(7-bit) wakeup is expected from HALT. Put the card in
  // HALT first, with CRC handled explicitly, then send 0x40 / 0x43 with CRC
  // disabled. This mirrors the established PN532 Gen1A raw-command sequence.
  bool gen1a = false;
  uint8_t resp[8] = {};
  uint8_t rlen = sizeof(resp);

  const bool rawMode =
      _nfcWriteReg(_nfc, _wire, 0x6302, 0x00) && // TxMode: CRC off, 106A
      _nfcWriteReg(_nfc, _wire, 0x6303, 0x00);   // RxMode: CRC off, 106A

  if (rawMode) {
    // HLTA has CRC_A 0x57CD. A halted card intentionally sends no response;
    // _nfcCommThru still consumes the PN532 response/status, so ignore its
    // boolean result here.
    static const uint8_t halt[] = {0x50, 0x00, 0x57, 0xCD};
    (void)_nfcCommThru(_nfc, _wire, halt, sizeof(halt), resp, rlen, 200);

    if (_nfcWriteReg(_nfc, _wire, 0x633D, 0x07)) {
      static const uint8_t wake[] = {0x40};
      rlen = sizeof(resp);
      const bool ack1 = _nfcCommThru(_nfc, _wire, wake, sizeof(wake),
                                      resp, rlen, 250) &&
                        rlen >= 1 && (resp[0] & 0x0F) == 0x0A;

      _nfcWriteReg(_nfc, _wire, 0x633D, 0x00);

      if (ack1) {
        static const uint8_t unlock[] = {0x43};
        rlen = sizeof(resp);
        gen1a = _nfcCommThru(_nfc, _wire, unlock, sizeof(unlock),
                             resp, rlen, 250) &&
                rlen >= 1 && (resp[0] & 0x0F) == 0x0A;
      }
    }
  }

  // Restore normal ISO14443A CRC/framing before leaving the raw probe.
  _nfcWriteReg(_nfc, _wire, 0x633D, 0x00);
  _nfcWriteReg(_nfc, _wire, 0x6302, 0x80);
  _nfcWriteReg(_nfc, _wire, 0x6303, 0x80);
  _resetAndReselect();
  return gen1a ? MagicCardType::GEN1A : MagicCardType::NONE;
}


bool PN532I2cScreen::_writeMagicUid(MagicCardType type, const uint8_t* sourceUid,
                                       uint8_t sourceUidLen, const uint8_t block0[16]) {
  if (!sourceUid || !_nfc || !_wire ||
      (sourceUidLen != 4 && sourceUidLen != 7)) return false;
  if (type == MagicCardType::GEN1A && (!block0 || sourceUidLen != 4)) return false;

  if (!_resetAndReselect()) return false;

  if (type == MagicCardType::GEN3) {
    // Reuse the same command family already used by Magic -> Gen3 Set UID.
    uint8_t cmd[13] = {0x90, 0xFB, 0xCC, 0xCC, sourceUidLen};
    memcpy(cmd + 5, sourceUid, sourceUidLen);
    cmd[5 + sourceUidLen] = 0x00;
    const uint8_t cmdLen = (uint8_t)(6u + sourceUidLen);
    uint8_t resp[8] = {};
    uint8_t rlen = sizeof(resp);
    if (!_nfcDataExch(_nfc, _wire, cmd, cmdLen, resp, rlen, 500)) return false;
  } else if (type == MagicCardType::GEN1A) {
    const uint8_t bcc = (uint8_t)(sourceUid[0] ^ sourceUid[1] ^ sourceUid[2] ^ sourceUid[3]);

    // Enter the Gen1A backdoor from HALT, using the same raw sequence as
    // detection. Gen1A 0x40 must be sent as 7 bits with CRC disabled.
    uint8_t resp[8] = {};
    uint8_t rlen = sizeof(resp);
    const bool rawMode =
        _nfcWriteReg(_nfc, _wire, 0x6302, 0x00) &&
        _nfcWriteReg(_nfc, _wire, 0x6303, 0x00);
    if (!rawMode) {
      _nfcWriteReg(_nfc, _wire, 0x6302, 0x80);
      _nfcWriteReg(_nfc, _wire, 0x6303, 0x80);
      return false;
    }

    static const uint8_t halt[] = {0x50, 0x00, 0x57, 0xCD};
    (void)_nfcCommThru(_nfc, _wire, halt, sizeof(halt), resp, rlen, 200);

    if (!_nfcWriteReg(_nfc, _wire, 0x633D, 0x07)) {
      _nfcWriteReg(_nfc, _wire, 0x6302, 0x80);
      _nfcWriteReg(_nfc, _wire, 0x6303, 0x80);
      return false;
    }
    static const uint8_t wake[] = {0x40};
    rlen = sizeof(resp);
    const bool ack1 = _nfcCommThru(_nfc, _wire, wake, sizeof(wake),
                                    resp, rlen, 250) &&
                      rlen >= 1 && (resp[0] & 0x0F) == 0x0A;
    _nfcWriteReg(_nfc, _wire, 0x633D, 0x00);
    if (!ack1) {
      _nfcWriteReg(_nfc, _wire, 0x6302, 0x80);
      _nfcWriteReg(_nfc, _wire, 0x6303, 0x80);
      _resetAndReselect();
      return false;
    }

    static const uint8_t unlock[] = {0x43};
    rlen = sizeof(resp);
    const bool ack2 = _nfcCommThru(_nfc, _wire, unlock, sizeof(unlock),
                                    resp, rlen, 250) &&
                      rlen >= 1 && (resp[0] & 0x0F) == 0x0A;
    // MIFARE WRITE command/data frames need CRC-A on TX, but the PICC ACKs
    // are only 4 bits and carry no CRC. Keep RX CRC disabled until both ACKs
    // have been consumed; otherwise the PN532 can report a CRC/framing error
    // even though the card accepted the transmitted frame.
    _nfcWriteReg(_nfc, _wire, 0x6302, 0x80); // TxMode: CRC-A enabled
    _nfcWriteReg(_nfc, _wire, 0x6303, 0x00); // RxMode: raw 4-bit ACK
    if (!ack2) {
      _nfcWriteReg(_nfc, _wire, 0x6303, 0x80);
      _resetAndReselect();
      return false;
    }

    // MIFARE WRITE is a two-frame exchange. Sending A0 00 and the 16-byte
    // manufacturer block in one InDataExchange frame is not equivalent and
    // leaves block 0 unchanged on Gen1A cards. Keep the backdoor session open
    // and perform both RF exchanges explicitly.
    uint8_t write0[2] = {0xA0, 0x00};
    rlen = sizeof(resp);
    const bool writeAck = _nfcCommThru(_nfc, _wire, write0, sizeof(write0),
                                       resp, rlen, 400) &&
                          rlen >= 1 && (resp[0] & 0x0F) == 0x0A;
    if (!writeAck) {
      _nfcWriteReg(_nfc, _wire, 0x6303, 0x80);
      _resetAndReselect();
      return false;
    }

    uint8_t safeBlock0[16];
    memcpy(safeBlock0, block0, sizeof(safeBlock0));
    memcpy(safeBlock0, sourceUid, 4);
    safeBlock0[4] = bcc;
    rlen = sizeof(resp);
    const bool dataAck = _nfcCommThru(_nfc, _wire, safeBlock0, sizeof(safeBlock0),
                                      resp, rlen, 700) &&
                         rlen >= 1 && (resp[0] & 0x0F) == 0x0A;
    // Restore normal CRC handling before leaving the raw MIFARE exchange.
    _nfcWriteReg(_nfc, _wire, 0x6303, 0x80);
    if (!dataAck) {
      _resetAndReselect();
      return false;
    }
  } else {
    return false;
  }

  // Force a fresh activation and verify that the source UID is now presented.
  if (!_nfc->SAMConfig()) return false;
  uint8_t uid[7] = {};
  uint8_t uidLen = 0;
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 300))
      return uidLen == sourceUidLen && memcmp(uid, sourceUid, sourceUidLen) == 0;
    delay(30);
  }
  return false;
}

void PN532I2cScreen::_doDetectMagic() {
  _magicLog.addLine("Scanning tag...", TFT_WHITE);
  render();

  uint8_t uid[7] = {};
  uint8_t uidLen = 0;
  uint32_t start = millis();
  bool ok = false;
  while (millis() - start < 5000) {
    Uni.update();
    if (Uni.Nav->wasPressed() &&
        Uni.Nav->readDirection() == INavigation::DIR_BACK) {
      _goMagic();
      return;
    }
    if (_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 200)) {
      ok = true;
      break;
    }
    delay(50);
  }

  if (!ok) {
    _magicLog.addLine("No tag detected", TFT_DARKGREY);
    _magicDetectDone = true;
    render();
    return;
  }

  const uint8_t sak = pn532_packetbuffer[11];
  if (sak != 0x09 && sak != 0x08 && sak != 0x18) {
    _magicLog.addLine("Not MIFARE Classic", TFT_DARKGREY);
    _magicDetectDone = true;
    render();
    return;
  }

  _magicLog.addLine("Checking Magic type...", TFT_WHITE);
  render();

  const MagicCardType magic = _detectMagicType();
  _magicLog.addLine("Magic type:", TFT_CYAN);
  _magicLog.addLine(magicCardTypeName(magic),
                    magic == MagicCardType::NONE ? TFT_DARKGREY : TFT_GREEN);

  if (magic != MagicCardType::NONE) {
    int n = Achievement.inc("pn532_magic_detect");
    if (n == 1) Achievement.unlock("pn532_magic_detect");
  }

  _magicDetectDone = true;
  render();
}

void PN532I2cScreen::_doGen3SetUid() {
  ShowStatusAction::show("Place Gen3 tag...", 0);
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
  if (!ok) { ShowStatusAction::show("No tag detected"); _goMagic(); return; }

  // Fail early instead of asking for a UID that cannot be applied to this tag.
  if (_detectMagicType() != MagicCardType::GEN3) {
    ShowStatusAction::show("Tag is not Gen3");
    _goMagic();
    return;
  }

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

  // Use the same verified Gen3 UID writer as Write to Tag. It performs a
  // fresh activation before the command and verifies the new UID afterwards.
  render();
  const bool ok2 = _writeMagicUid(MagicCardType::GEN3, newUid, newUidLen, nullptr);
  ShowStatusAction::show(ok2 ? "Gen3 UID set" : "Set UID failed");
  _goMagic();
}

void PN532I2cScreen::_doGen3LockUid() {
  ShowStatusAction::show("Place Gen3 tag...", 0);
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
  if (!ok) { ShowStatusAction::show("No tag detected"); _goMagic(); return; }

  if (_detectMagicType() != MagicCardType::GEN3) {
    ShowStatusAction::show("Tag is not Gen3");
    _goMagic();
    return;
  }

  static const InputSelectAction::Option opts[] = {
    {"Lock UID permanently", "lock"},
  };
  const char* choice = InputSelectAction::popup("Permanent UID lock", opts, 1, nullptr);
  render();
  if (!choice || strcmp(choice, "lock") != 0) { _goMagic(); return; }

  // The confirmation overlay may leave the card idle; use a fresh activation
  // before issuing the irreversible Gen3 lock command.
  if (!_resetAndReselect()) {
    ShowStatusAction::show("Tag lost");
    _goMagic();
    return;
  }

  static const uint8_t cmd[] = {0x90, 0xFD, 0x11, 0x11, 0x00};
  uint8_t resp[8]; uint8_t rlen = sizeof(resp);
  bool locked = _nfcDataExch(_nfc, _wire, cmd, sizeof(cmd), resp, rlen);
  ShowStatusAction::show(locked ? "Gen3 UID locked" : "Lock failed");
  _goMagic();
}

void PN532I2cScreen::_showDumpActions() {
  static const InputSelectAction::Option opts[] = {
    {"View Dump",    "view"},
    {"Save Dump",    "save"},
    {"Write to Tag", "write"},
  };
  const char* r = InputSelectAction::popup("Dump Actions", opts, 3, nullptr);
  if (!r) { render(); return; }
  if (strcmp(r, "view") == 0) {
    _showDumpHex();
    return;
  }

  // Restore Tag Details before opening another modal/progress screen. The
  // popup only clears its own rectangle, so launching Save/Write immediately
  // could leave fragments of Dump Actions visible underneath the next UI.
  render();
  if (strcmp(r, "save") == 0) _doSaveDump();
  else _showWriteDumpPreview(_dumpImg, _dumpLen, _uid, _uidLen, false);
}

void PN532I2cScreen::_doWriteDumpFromFilePicker() {
  _state = STATE_MIFARE_DUMP_SELECT;
  if (_dumpPickDir.length() == 0) _dumpPickDir = _dumpPath;
  _browser.root = _dumpPath;

  uint8_t n = _browser.load(this, _dumpPickDir, BrowseFileView::Mode(".bin", 320, 1024, 4096));
  if (n == 0 && _dumpPickDir == _dumpPath) {
    ShowStatusAction::show("No compatible Classic .bin");
    _goMifareTag();
    return;
  }
  setItems(_browser.items(), n);
}

void PN532I2cScreen::_doWriteDumpFileSelected(uint8_t fileIndex) {
  if (fileIndex >= _browser.count()) return;
  const auto& e = _browser.entry(fileIndex);
  if (e.isDir) {
    _dumpPickDir = e.path;
    _doWriteDumpFromFilePicker();
    return;
  }
  if (!Uni.Storage) { ShowStatusAction::show("Storage unavailable"); _goMifareTag(); return; }
  fs::File f = Uni.Storage->open(e.path.c_str(), "r");
  if (!f) { ShowStatusAction::show("Open failed"); _goMifareTag(); return; }
  size_t len = f.size();
  if (len != 320 && len != 1024 && len != 4096) {
    f.close(); ShowStatusAction::show("Unsupported dump size"); _goMifareTag(); return;
  }
  uint8_t* dump = (uint8_t*)malloc(len);
  if (!dump) { f.close(); ShowStatusAction::show("Out of memory"); _goMifareTag(); return; }
  size_t got = f.read(dump, len); f.close();
  if (got != len) { free(dump); ShowStatusAction::show("Read failed"); _goMifareTag(); return; }

  // A raw MIFARE Classic dump carries the original 4-byte UID in block 0.
  // Trust it only when the manufacturer-block BCC is valid; otherwise keep
  // the conservative file-source behaviour (UID Unknown / target preserved).
  uint8_t sourceUid[4] = { dump[0], dump[1], dump[2], dump[3] };
  const bool sourceUidKnown =
      dump[4] == (uint8_t)(sourceUid[0] ^ sourceUid[1] ^ sourceUid[2] ^ sourceUid[3]);

  _showWriteDumpPreview(dump, len,
                        sourceUidKnown ? sourceUid : nullptr,
                        sourceUidKnown ? 4 : 0,
                        true);
  free(dump);
}


void PN532I2cScreen::_showWriteDumpPreview(const uint8_t* dump, size_t len,
                                             const uint8_t* sourceUid, uint8_t sourceUidLen,
                                             bool fromFile) {
  if (!dump || (len != 320 && len != 1024 && len != 4096)) {
    ShowStatusAction::show("Invalid dump");
    return;
  }

  memcpy(_dumpImg, dump, len);
  _dumpLen = len;
  _writePreviewFromFile = fromFile;
  _writePreviewSourceUidKnown = sourceUid && (sourceUidLen == 4 || sourceUidLen == 7);
  _writePreviewSourceUidLen = _writePreviewSourceUidKnown ? sourceUidLen : 0;
  _writePreviewReplaceUid = _writePreviewSourceUidKnown;
  memset(_writePreviewSourceUid, 0, sizeof(_writePreviewSourceUid));
  if (_writePreviewSourceUidKnown) memcpy(_writePreviewSourceUid, sourceUid, sourceUidLen);

  _state = STATE_MIFARE_WRITE_PREVIEW;
  _resetRows();
  _pushRow("Source", fromFile ? "File" : "Read Tag");
  const char* type = len == 320 ? "MIFARE Classic Mini" :
                     (len == 4096 ? "MIFARE Classic 4K" : "MIFARE Classic 1K");
  _pushRow("Type", type);
  _pushRow("UID", _writePreviewSourceUidKnown ?
                    _hexUid(_writePreviewSourceUid, _writePreviewSourceUidLen) : String("Unknown"));
  _pushRow("UID Action", _writePreviewSourceUidKnown ? (_writePreviewReplaceUid ? "Replace" : "Preserve") : "Preserved");
  _pushRow("Blocks", String((unsigned)(len / 16u)));
  _pushRow("Dump", String((unsigned)len) + " bytes");

  // Use the same parsed NDEF presentation as Tag Details / Chameleon Ultra.
  const size_t totalSectors = len == 320 ? 5u : (len == 4096 ? 40u : 16u);
  _appendDumpNdefDetails(_dumpImg, len, totalSectors);

  _pushRow("[Press]", "Write to Tag");
  _scrollView.setRows(_rows, _rowCount);
  render();
}


bool PN532I2cScreen::_tryWriteMifareBlock(uint16_t block, const uint8_t data[16],
                                           const uint8_t key[6], bool useKeyB) {
  if (!data || !key) return false;

  // Start every write attempt from a freshly selected PICC.  A previous
  // authenticate/write leaves the card in an active Crypto1 session; trying
  // to authenticate a second block without re-selecting is unreliable on the
  // PN532.  Re-selecting also gives us a clean retry path with Key B when the
  // access conditions do not permit writes with Key A.
  uint8_t uid[7] = {};
  uint8_t uidLen = 0;
  if (!_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 300)) {
    return false;
  }
  if (uidLen != _uidLen || memcmp(uid, _uid, uidLen) != 0) return false;

  if (!_nfc->mifareclassic_AuthenticateBlock(
        _uid, _uidLen, (uint8_t)block, useKeyB ? 1 : 0,
        const_cast<uint8_t*>(key))) {
    return false;
  }

  return _nfc->mifareclassic_WriteDataBlock(
      (uint8_t)block, const_cast<uint8_t*>(data));
}

bool PN532I2cScreen::_doWriteDumpToTag(const uint8_t* dump, size_t len,
                                         const uint8_t* sourceUid, uint8_t sourceUidLen) {
  // Copy source identity before scanning the destination: _scanCardOrShow()
  // updates the screen's _uid/_uidLen members with the target tag.
  uint8_t sourceUidCopy[7] = {};
  const bool sourceUidKnown = sourceUid && (sourceUidLen == 4 || sourceUidLen == 7);
  if (sourceUidKnown) memcpy(sourceUidCopy, sourceUid, sourceUidLen);

  renderOperationTitle("Write to Tag");
  if (!dump || (len != 320 && len != 1024 && len != 4096)) {
    ShowStatusAction::show("Invalid dump"); return false;
  }
  renderTagPrompt("Place tag on reader...", bodyX(), bodyY(), bodyW(), bodyH());
  if (!_scanCardOrShow(5000)) { render(); return false; }
  auto dims = _mfDims(_sak);
  if (dims.second * 16u != len) { ShowStatusAction::show("Tag size mismatch"); render(); return false; }

  MagicCardType magic = MagicCardType::NONE;
  bool restoreUid = false;
  bool uidDiffers = false;
  const bool replaceUidRequested = sourceUidKnown && _writePreviewReplaceUid;
  if (replaceUidRequested) {
    magic = _detectMagicType();
    uidDiffers = _uidLen != sourceUidLen ||
                 (_uidLen == sourceUidLen && memcmp(_uid, sourceUidCopy, _uidLen) != 0);
    restoreUid = uidDiffers &&
                 ((magic == MagicCardType::GEN1A && sourceUidLen == 4) ||
                  (magic == MagicCardType::GEN3 && (sourceUidLen == 4 || sourceUidLen == 7)));
  }

  const auto defaults = NFCUtility::getDefaultKeys();
  uint8_t zeroKey[6] = {};
  ProgressView::init();
  size_t written = 0;
  const size_t totalWritableBlocks = dims.second - 1u;

  for (size_t sector = 0; sector < dims.first; ++sector) {
    const uint16_t first = (sector < 32) ? (uint16_t)(sector * 4) : (uint16_t)(128 + (sector - 32) * 16);
    const uint16_t trailer = (sector < 32) ? (uint16_t)(first + 3) : (uint16_t)(first + 15);
    const uint8_t* srcTrailer = dump + (size_t)trailer * 16u;

    for (uint16_t block = first; block <= trailer; ++block) {
      if (block == 0) continue; // manufacturer block is not normally writable
      const size_t currentWrite = written + 1u;
      int pct = (int)((currentWrite - 1u) * 100u / totalWritableBlocks);
      char msg[40];
      snprintf(msg, sizeof(msg), "Writing blocks (%u/%u)...",
               (unsigned)currentWrite, (unsigned)totalWritableBlocks);
      ProgressView::progress(msg, pct);

      const uint8_t* blockData = dump + (size_t)block * 16u;
      bool ok = false;

      // MAD directory blocks on NFC Forum-formatted Classic tags are
      // normally writable with Key B.  The PN532/Adafruit path can report a
      // successful write after Key-A authentication even when the access bits
      // deny the actual write, leaving MAD unchanged and making copied NDEF
      // data invisible. Prefer Key B for MAD1 (1/2) and MAD2 (64..66), just as
      // Erase Tag already does for MAD1, and only fall back to Key A last.
      const bool madWrite = (block == 1u || block == 2u) ||
                            (dims.first > 16 && block >= 64u && block <= 66u);

      auto tryDefaults = [&](bool useKeyB) {
        for (const auto& k : defaults) {
          auto kv = k.value();
          const uint8_t* key = (const uint8_t*)kv.data();
          if (_tryWriteMifareBlock(block, blockData, key, useKeyB)) return true;
        }
        return false;
      };

      if (madWrite) {
        ok = _tryWriteMifareBlock(block, blockData, srcTrailer + 10, true) ||
             tryDefaults(true) ||
             _tryWriteMifareBlock(block, blockData, srcTrailer, false) ||
             tryDefaults(false);
      } else {
        ok = _tryWriteMifareBlock(block, blockData, srcTrailer, false) ||
             _tryWriteMifareBlock(block, blockData, srcTrailer + 10, true) ||
             tryDefaults(false) ||
             tryDefaults(true);
      }

      if (!ok) {
        ProgressView::finish();
        Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
        char err[32]; snprintf(err, sizeof(err), "Write failed: block %u", (unsigned)block);
        ShowStatusAction::show(err);
        render();
        return false;
      }
      written++;
    }
  }
  ProgressView::finish();

  // Magic Gen1A/Gen3 can reproduce the source identity as well as blocks
  // 1..N. Do this last: a failed data write must never change the target UID.
  if (restoreUid) {
    Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    ShowStatusAction::show("Writing source UID...", 0);
    if (!_writeMagicUid(magic, sourceUidCopy, sourceUidLen, dump)) {
      ShowStatusAction::show("UID write failed");
      render();
      return false;
    }
  }

  // Do not leave the completed progress view behind the modal status box.
  // ShowStatusAction wipes only its own rectangle on dismissal, which made
  // remnants of the progress UI briefly visible during Write to Tag.
  Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  const char* status = restoreUid ? "Tag + UID written" :
                       (replaceUidRequested && uidDiffers ? "Tag written; UID preserved" : "Tag written");
  ShowStatusAction::show(status);
  render();
  return true;
}

void PN532I2cScreen::_doEraseTag() {
  renderOperationTitle("Erase Tag");
  if (!_scanCardOrShow(5000)) { _goMifareTag(); return; }
  auto dims = _mfDims(_sak);
  if (dims.first == 0) { ShowStatusAction::show("Not MIFARE Classic"); _goMifareTag(); return; }
  _discoverDefaultKeys(true);
  if (!_hasReadableKeyForEverySector()) {
    ShowStatusAction::show("Erase failed: missing key"); _goMifareTag(); return;
  }

  uint8_t zero[16] = {};
  size_t erased = 0;
  const size_t totalDataBlocks = dims.second - dims.first - 1u;
  ProgressView::init();
  for (size_t sector = 0; sector < dims.first; ++sector) {
    uint16_t first = (sector < 32) ? (uint16_t)(sector * 4) : (uint16_t)(128 + (sector - 32) * 16);
    uint16_t trailer = (sector < 32) ? (uint16_t)(first + 3) : (uint16_t)(first + 15);
    auto& a = _mfKeys[sector].first;
    auto& b = _mfKeys[sector].second;
    for (uint16_t block = first; block < trailer; ++block) {
      if (block == 0) continue;
      const size_t currentErase = erased + 1u;
      char msg[40];
      snprintf(msg, sizeof(msg), "Erasing blocks (%u/%u)...",
               (unsigned)currentErase, (unsigned)totalDataBlocks);
      ProgressView::progress(
          msg, (int)((currentErase - 1u) * 100u / totalDataBlocks));

      bool ok = false;
      // NFC Forum MAD1 uses access bits 78 77 88.  On formatted cards Key A
      // can authenticate/read the MAD data blocks, while write permission is
      // normally granted through Key B.  The CU command path reports denied
      // writes reliably; the PN532/Adafruit path can acknowledge the command
      // after a Key-A authentication without the MAD bytes actually changing.
      // Prefer Key B specifically for MAD1 blocks 1/2, then fall back to A.
      const bool madWrite = (sector == 0 && (block == 1 || block == 2));
      if (madWrite && b) {
        auto kb = b.value();
        ok = _tryWriteMifareBlock(block, zero, (const uint8_t*)kb.data(), true);
      }
      if (!ok && a) {
        auto ka = a.value();
        ok = _tryWriteMifareBlock(block, zero, (const uint8_t*)ka.data(), false);
      }
      if (!ok && !madWrite && b) {
        auto kb = b.value();
        ok = _tryWriteMifareBlock(block, zero, (const uint8_t*)kb.data(), true);
      }
      if (!ok) {
        ProgressView::finish();
        Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
        char err[32]; snprintf(err, sizeof(err), "Erase failed: block %u", (unsigned)block);
        ShowStatusAction::show(err);
        _goMifareTag();
        return;
      }
      erased++;
    }
  }
  ProgressView::finish();

  // The CU path validated on hardware defines Erase Tag as clearing every
  // data block while preserving block 0 and all sector trailers.  Blocks 1
  // and 2 are MAD1 on Classic 1K; if either one is not actually zero after
  // the writes above, PN532 Read NDEF can still see the old NDEF allocation.
  // Read them back before reporting success so a transport-level "write OK"
  // cannot leave a silently formatted tag behind.
  if (dims.first == 16) {
    auto verifyZeroBlock = [&](uint8_t block) -> bool {
      auto& a = _mfKeys[0].first;
      auto& b = _mfKeys[0].second;
      uint8_t data[16] = {};

      auto tryRead = [&](const NFCUtility::mfKey& key, bool useKeyB) -> bool {
        uint8_t uid[7] = {};
        uint8_t uidLen = 0;
        if (!_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 300))
          return false;
        if (uidLen != _uidLen || memcmp(uid, _uid, uidLen) != 0) return false;
        if (!_nfc->mifareclassic_AuthenticateBlock(
              _uid, _uidLen, block, useKeyB ? 1 : 0,
              const_cast<uint8_t*>(key.data())))
          return false;
        return _nfc->mifareclassic_ReadDataBlock(block, data);
      };

      bool read = false;
      if (a) read = tryRead(a.value(), false);
      if (!read && b) read = tryRead(b.value(), true);
      if (!read) return false;
      for (uint8_t v : data) if (v != 0) return false;
      return true;
    };

    if (!verifyZeroBlock(1) || !verifyZeroBlock(2)) {
      Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
      ShowStatusAction::show("Erase failed: MAD remains");
      _goMifareTag();
      return;
    }
  }

  Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  _hasCard = false;
  _mfKeys.fill({});
  ShowStatusAction::show("Tag erased");
  _goMifareTag();
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

  // Match the Chameleon Classic dump naming convention exactly:
  // MF-Mini_<UID>, MF-1K_<UID>, or MF-4K_<UID>.
  const char* typeName = (_sak == 0x09) ? "MF-Mini"
                       : (_sak == 0x18) ? "MF-4K"
                                        : "MF-1K";
  String suggested = String(typeName) + "_" + uid;

  String name = InputTextAction::popup("Save dump", suggested);
  if (InputTextAction::wasCancelled() || name.length() == 0) {
    render();
    return;
  }

  // Keep the editor focused on the basename, as in the Chameleon dump flow.
  // The storage format remains a raw .bin file.
  if (name.endsWith(".bin")) name.remove(name.length() - 4);
  String filename = name + ".bin";
  String path = String(_dumpPath) + "/" + filename;

  fs::File f = Uni.Storage->open(path.c_str(), "w");
  if (!f) { ShowStatusAction::show("Save failed"); render(); return; }
  if (_dumpLen == 0 || _dumpLen > sizeof(_dumpImg)) {
    f.close();
    ShowStatusAction::show("Invalid dump size");
    render();
    return;
  }
  const bool ok = f.write(_dumpImg, _dumpLen) == _dumpLen;
  f.close();

  if (ok) {
    String msg = String("Saved: ") + filename;
    ShowStatusAction::show(msg.c_str());
  } else {
    ShowStatusAction::show("Save failed");
  }
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
