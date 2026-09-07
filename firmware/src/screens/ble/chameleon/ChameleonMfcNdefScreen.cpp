#include "ChameleonMfcNdefScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "utils/nfc/NdefBuilder.h"
#include "utils/nfc/NdefParser.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/components/Header.h"
#include "ui/views/ProgressView.h"

#include <cstring>
#include <ctype.h>

namespace {
static void renderOperationTitle(const char* title) { Header header; header.render(title); }
static constexpr uint8_t MAD_KEY_A[6] = {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5};
static constexpr uint8_t NFC_KEY_A[6] = {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7};

static uint8_t madCrc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0xC7;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x1D) : (uint8_t)(crc << 1);
  }
  return crc;
}
}

const char* ChameleonMfcNdefScreen::title() {
  switch (_state) {
    case STATE_WRITE_MENU: return "Write NDEF";
    case STATE_RESULT: return "NDEF Details";
    case STATE_FILE_SELECT: return "NDEF Files";
    default: return "NDEF Operations";
  }
}

void ChameleonMfcNdefScreen::onInit() { _goMenu(); }

void ChameleonMfcNdefScreen::_goMenu() {
  _state = STATE_MENU;
  _writePreview = false;
  _writePreviewFromFile = false;
  setItems(_menuItems);
  render();
}

void ChameleonMfcNdefScreen::_goWriteMenu() {
  _state = STATE_WRITE_MENU;
  _writePreview = false;
  _writePreviewFromFile = false;
  setItems(_writeItems);
  render();
}

void ChameleonMfcNdefScreen::onUpdate() {
  if (_state == STATE_RESULT) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        const bool fromFile = _writePreviewFromFile;
        const bool wasPreview = _writePreview;
        _writePreview = false;
        _writePreviewFromFile = false;
        if (wasPreview && fromFile) _loadFilePicker();
        else if (wasPreview) _goWriteMenu();
        else _goMenu();
      } else if (dir == INavigation::DIR_PRESS && _hasNdef) {
        if (_writePreview) {
          if (_writeNdefRecord(_ndef, _ndefLen)) _goMenu();
          else render();
        } else {
          _showActions();
        }
      } else {
        _scrollView.onNav(dir);
      }
    }
    return;
  }
  ListScreen::onUpdate();
}

void ChameleonMfcNdefScreen::onRender() {
  if (_state == STATE_RESULT) {
    _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  ListScreen::onRender();
}

void ChameleonMfcNdefScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_MENU) {
    if (index == 0) _doRead();
    else if (index == 1) _goWriteMenu();
    else if (index == 2) _doErase();
    else if (index == 3) {
      _running = true;
      renderOperationTitle("Format NDEF");
      if (_scanClassic()) _formatClassic1kNdef();
      _running = false;
      ChameleonClient::get().setMode(0);
      _goMenu();
    }
    return;
  }
  if (_state == STATE_WRITE_MENU) {
    if (index == 0) _writeText();
    else if (index == 1) _writeUrl();
    else if (index == 2) _writePhone();
    else if (index == 3) _writeEmail();
    else if (index == 4) _writeVcard();
    else if (index == 5) _loadFilePicker();
    return;
  }
  if (_state == STATE_FILE_SELECT) _selectFile(index);
}

void ChameleonMfcNdefScreen::onBack() {
  if (_state == STATE_WRITE_MENU) { _goMenu(); return; }
  if (_state == STATE_RESULT) {
    const bool fromFile = _writePreviewFromFile;
    const bool wasPreview = _writePreview;
    _writePreview = false;
    _writePreviewFromFile = false;
    if (wasPreview && fromFile) _loadFilePicker();
    else if (wasPreview) _goWriteMenu();
    else _goMenu();
    return;
  }
  if (_state == STATE_FILE_SELECT) {
    if (_pickDir.length() == 0 || _pickDir == NDEF_DIR) {
      _pickDir = "";
      _goWriteMenu();
    } else {
      int slash = _pickDir.lastIndexOf('/');
      _pickDir = (slash > 0) ? _pickDir.substring(0, slash) : String(NDEF_DIR);
      if (!_pickDir.startsWith(NDEF_DIR)) _pickDir = NDEF_DIR;
      _loadFilePicker();
    }
    return;
  }
  Screen.goBack();
}

uint8_t ChameleonMfcNdefScreen::_trailerBlock(uint8_t sector) const {
  return (sector < 32) ? (uint8_t)(sector * 4u + 3u)
                       : (uint8_t)(128u + (sector - 32u) * 16u + 15u);
}

uint16_t ChameleonMfcNdefScreen::_firstBlock(uint8_t sector) const {
  return (sector < 32) ? (uint16_t)sector * 4u
                       : (uint16_t)(128u + (sector - 32u) * 16u);
}

bool ChameleonMfcNdefScreen::_scanClassic() {
  auto& c = ChameleonClient::get();
  c.setMode(1);
  uint8_t atqa[2] = {}, sak = 0;
  if (!c.scan14A(_uid, &_uidLen, atqa, &sak)) {
    c.setMode(0);
    ShowStatusAction::show("No card detected");
    return false;
  }
  if (!c.mf1Support()) {
    c.setMode(0);
    ShowStatusAction::show("Not MIFARE Classic");
    return false;
  }
  _sak = sak;
  memcpy(_atqa, atqa, 2);
  if (sak == 0x18) _sectors = 40;
  else if (sak == 0x01) _sectors = 5;
  else _sectors = 16;
  return true;
}

bool ChameleonMfcNdefScreen::_readNdefSectors(uint8_t* sectors,
                                               size_t maxSectors,
                                               size_t& count) {
  count = 0;
  auto& c = ChameleonClient::get();

  static const uint8_t madKeys[][6] = {
    {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5},
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
    {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7},
  };

  auto readMadBlock = [&](uint8_t block, uint8_t out[16]) {
    for (const auto& key : madKeys) {
      if (c.mf1ReadBlock(block, 0x60, key, out) ||
          c.mf1ReadBlock(block, 0x61, key, out)) return true;
    }
    return false;
  };

  uint8_t b1[16] = {}, b2[16] = {};
  if (!readMadBlock(1, b1) || !readMadBlock(2, b2)) return false;

  auto addIfNdef = [&](uint8_t sector, uint8_t application, uint8_t cluster) {
    if (sector >= _sectors || count >= maxSectors) return;
    // MAD stores application_code first and function_cluster_code second.
    // Accept the reversed byte order defensively for non-standard dumps.
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

  if (_sectors > 16) {
    uint8_t m0[16] = {}, m1[16] = {}, m2[16] = {};
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

bool ChameleonMfcNdefScreen::_readNdefArea(const uint8_t* sectors,
                                            size_t sectorCount,
                                            uint8_t*& area,
                                            size_t& areaLen) {
  area = nullptr;
  areaLen = 0;
  if (!sectors || !sectorCount) return false;
  size_t capacity = 0;
  for (size_t i = 0; i < sectorCount; ++i)
    capacity += sectors[i] < 32 ? 48u : 240u;
  area = (uint8_t*)malloc(capacity);
  if (!area) return false;

  auto& c = ChameleonClient::get();
  size_t out = 0;
  size_t totalBlocks = 0;
  for (size_t i = 0; i < sectorCount; ++i) totalBlocks += sectors[i] < 32 ? 3u : 15u;
  size_t done = 0;
  ProgressView::init();
  for (size_t i = 0; i < sectorCount; ++i) {
    const uint8_t sector = sectors[i];
    const uint16_t first = _firstBlock(sector);
    const uint8_t blocks = sector < 32 ? 3 : 15;
    for (uint8_t bi = 0; bi < blocks; ++bi) {
      char msg[36];
      snprintf(msg, sizeof(msg), "Reading blocks (%u/%u)...",
               (unsigned)(done + 1u), (unsigned)totalBlocks);
      ProgressView::progress(msg, (int)(done * 100u / totalBlocks));
      if (!c.mf1ReadBlock((uint8_t)(first + bi), 0x60, NFC_KEY_A, area + out)) {
        ProgressView::finish();
        free(area); area = nullptr; areaLen = 0;
        return false;
      }
      out += 16;
      ++done;
    }
  }
  ProgressView::progress("Read complete", 100);
  ProgressView::finish();
  areaLen = out;
  return true;
}

void ChameleonMfcNdefScreen::_doRead() {
  renderOperationTitle("Read NDEF");
  _running = true;
  _hasNdef = false;
  _ndefLen = 0;
  _capacity = 0;
  if (!_scanClassic()) { _running = false; _goMenu(); return; }

  uint8_t sectors[39] = {};
  size_t count = 0;
  if (!_readNdefSectors(sectors, sizeof(sectors), count)) {
    ChameleonClient::get().setMode(0); _running = false;
    ShowStatusAction::show("No NDEF sectors in MAD"); _goMenu(); return;
  }
  for (size_t i = 0; i < count; ++i) _capacity += sectors[i] < 32 ? 48u : 240u;

  uint8_t* area = nullptr; size_t areaLen = 0;
  if (!_readNdefArea(sectors, count, area, areaLen)) {
    ChameleonClient::get().setMode(0); _running = false;
    ShowStatusAction::show("Failed to read NDEF sectors"); _goMenu(); return;
  }

  const uint8_t* ndef = nullptr; size_t ndefLen = 0; size_t pos = 0;
  while (pos < areaLen) {
    const uint8_t tlv = area[pos++];
    if (tlv == 0x00) continue;
    if (tlv == 0xFE || pos >= areaLen) break;
    size_t len = area[pos++];
    if (len == 0xFF) {
      if (pos + 1 >= areaLen) break;
      len = ((size_t)area[pos] << 8) | area[pos + 1u]; pos += 2;
    }
    if (pos + len > areaLen) break;
    if (tlv == 0x03) { ndef = area + pos; ndefLen = len; break; }
    pos += len;
  }
  _showResult(ndef, ndefLen);
  free(area);
  ChameleonClient::get().setMode(0);
  _running = false;
}

bool ChameleonMfcNdefScreen::_formatClassic1kNdef() {
  if (_sectors != 16) {
    ShowStatusAction::show("Format supports Classic 1K");
    return false;
  }

  // NFC Forum INITIALISED layout (AN1305): sectors 1 and 2 are NFC
  // sectors, the remaining sectors stay free.
  uint8_t madPayload[31] = {};
  madPayload[0] = 0x01; // MAD1 info byte / version 1
  madPayload[1] = 0x03; madPayload[2] = 0xE1; // sector 1
  madPayload[3] = 0x03; madPayload[4] = 0xE1; // sector 2

  uint8_t mad1[16] = {};
  uint8_t mad2[16] = {};
  mad1[0] = madCrc8(madPayload, sizeof(madPayload));
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
  // Erase Tag key probe instead of limiting it to the four NFC defaults.
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

  // Prefer per-UID Discovered Keys.  Formatting may be requested on a tag
  // whose current keys are not part of the public/default candidate list.
  uint8_t savedA[3][6] = {};
  uint8_t savedB[3][6] = {};
  bool hasSavedA[3] = {};
  bool hasSavedB[3] = {};
  if (Uni.Storage && Uni.Storage->isAvailable() && _uidLen > 0) {
    String uid = _uidString();
    uid.replace(":", "");
    String content = Uni.Storage->readFile((String("/unigeek/nfc/keys/") + uid + ".txt").c_str());
    auto hexNibble = [](char ch) -> int {
      if (ch >= '0' && ch <= '9') return ch - '0';
      if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
      if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
      return -1;
    };
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
          sector >= 0 && sector < 3 && strlen(hex) == 12) {
        uint8_t raw[6] = {};
        bool valid = true;
        for (uint8_t i = 0; i < 6; ++i) {
          const int hi = hexNibble(hex[i * 2]);
          const int lo = hexNibble(hex[i * 2 + 1]);
          if (hi < 0 || lo < 0) { valid = false; break; }
          raw[i] = (uint8_t)((hi << 4) | lo);
        }
        if (valid && (keyType == 'A' || keyType == 'a')) {
          memcpy(savedA[sector], raw, 6); hasSavedA[sector] = true;
        } else if (valid && (keyType == 'B' || keyType == 'b')) {
          memcpy(savedB[sector], raw, 6); hasSavedB[sector] = true;
        }
      }
      start = nl + 1;
    }
  }

  auto& c = ChameleonClient::get();
  uint8_t sectorKey[3][6] = {};
  uint8_t sectorKeyType[3] = {};

  // Only sectors 0, 1 and 2 are modified by the standard initialised format.
  for (uint8_t sector = 0; sector < 3; ++sector) {
    const uint8_t block = _trailerBlock(sector);
    bool found = false;

    // Persisted keys are tag-specific and therefore take priority.
    if (hasSavedA[sector] && c.mf1CheckKey(block, 0x60, savedA[sector])) {
      memcpy(sectorKey[sector], savedA[sector], 6);
      sectorKeyType[sector] = 0x60;
      found = true;
    }
    if (!found && hasSavedB[sector] && c.mf1CheckKey(block, 0x61, savedB[sector])) {
      memcpy(sectorKey[sector], savedB[sector], 6);
      sectorKeyType[sector] = 0x61;
      found = true;
    }

    for (uint8_t kt = 0; kt < 2 && !found; ++kt) {
      const uint8_t keyType = kt == 0 ? 0x60 : 0x61;
      for (const auto& key : candidates) {
        if (c.mf1CheckKey(block, keyType, key)) {
          memcpy(sectorKey[sector], key, 6);
          sectorKeyType[sector] = keyType;
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
  bool ok = true;
  uint16_t done = 0;
  static constexpr uint16_t total = 11;
  ProgressView::init();

  auto writeOld = [&](uint8_t sector, uint8_t block, const uint8_t data[16]) {
    char msg[40];
    snprintf(msg, sizeof(msg), "Formatting blocks (%u/%u)...",
             (unsigned)(done + 1u), (unsigned)total);
    ProgressView::progress(msg, (int)((uint32_t)done * 100u / total));
    // A key that authenticates is not necessarily permitted to write this
    // block by the current access bits. Try every persisted credential for
    // this sector before falling back to the preflight/default candidates.
    if (hasSavedA[sector] && c.mf1WriteBlock(block, 0x60, savedA[sector], data)) {
      ++done;
      return true;
    }
    if (hasSavedB[sector] && c.mf1WriteBlock(block, 0x61, savedB[sector], data)) {
      ++done;
      return true;
    }
    if (c.mf1WriteBlock(block, sectorKeyType[sector], sectorKey[sector], data)) {
      ++done;
      return true;
    }
    for (const auto& key : candidates) {
      if (c.mf1WriteBlock(block, 0x60, key, data) ||
          c.mf1WriteBlock(block, 0x61, key, data)) {
        ++done;
        return true;
      }
    }
    return false;
  };

  ok = writeOld(0, 1, mad1) && writeOld(0, 2, mad2);

  for (uint8_t sector = 1; sector <= 2 && ok; ++sector) {
    const uint8_t first = (uint8_t)_firstBlock(sector);
    for (uint8_t bi = 0; bi < 3 && ok; ++bi) {
      const uint8_t* data = (sector == 1 && bi == 0) ? emptyNdef : zero;
      ok = writeOld(sector, first + bi, data);
    }
  }

  if (ok) ok = writeOld(0, 3, madTrailer);
  if (ok) ok = writeOld(1, _trailerBlock(1), nfcTrailer);
  if (ok) ok = writeOld(2, _trailerBlock(2), nfcTrailer);

  if (ok) ProgressView::progress("Format complete", 100);
  ProgressView::finish();
  ShowStatusAction::show(ok ? "NDEF formatted" : "NDEF format failed");
  return ok;
}

bool ChameleonMfcNdefScreen::_writeNdefRecord(const uint8_t* ndef, size_t ndefLen) {
  renderOperationTitle("Write NDEF");
  if (!ndef || !ndefLen || ndefLen > MAX_NDEF_BYTES) {
    ShowStatusAction::show("NDEF too large"); return false;
  }
  _running = true;
  if (!_scanClassic()) { _running = false; return false; }

  uint8_t sectors[39] = {}; size_t count = 0;
  if (!_readNdefSectors(sectors, sizeof(sectors), count)) {
    static const InputSelectAction::Option opts[] = {
      {"Format NDEF", "format"},
      {"Cancel",      "cancel"},
    };
    const char* choice = InputSelectAction::popup("Not NDEF formatted", opts, 2, nullptr);
    render();
    if (!choice || strcmp(choice, "format") != 0 || !_formatClassic1kNdef()) {
      ChameleonClient::get().setMode(0); _running = false;
      return false;
    }
    // Successful formatting has just assigned sectors 1 and 2 to NDEF.
    // Continue with that known layout; Read NDEF remains the independent
    // physical verification after the requested write.
    sectors[0] = 1;
    sectors[1] = 2;
    count = 2;
  }
  size_t capacity = 0;
  for (size_t i = 0; i < count; ++i) capacity += sectors[i] < 32 ? 48u : 240u;
  const size_t payloadLen = ndefLen + 3u;
  if (payloadLen > capacity || ndefLen > 254u) {
    ChameleonClient::get().setMode(0); _running = false;
    ShowStatusAction::show("NDEF does not fit"); return false;
  }

  uint8_t* payload = (uint8_t*)malloc(payloadLen);
  if (!payload) {
    ChameleonClient::get().setMode(0); _running = false;
    ShowStatusAction::show("Out of memory"); return false;
  }
  payload[0] = 0x03; payload[1] = (uint8_t)ndefLen;
  memcpy(payload + 2, ndef, ndefLen); payload[2 + ndefLen] = 0xFE;

  auto& c = ChameleonClient::get();
  uint8_t firstFinal[16] = {}, firstStaged[16] = {};
  uint8_t firstBlockNo = 0; bool firstPrepared = false; bool ok = true;
  size_t offset = 0;
  ProgressView::init();
  for (size_t si = 0; si < count && offset < payloadLen && ok; ++si) {
    const uint8_t sector = sectors[si];
    const uint16_t first = _firstBlock(sector);
    const uint8_t blocks = sector < 32 ? 3 : 15;
    for (uint8_t bi = 0; bi < blocks && offset < payloadLen; ++bi) {
      const uint8_t blockNo = (uint8_t)(first + bi);
      uint8_t block[16] = {};
      if (!c.mf1ReadBlock(blockNo, 0x60, NFC_KEY_A, block)) { ok = false; break; }
      size_t take = payloadLen - offset; if (take > 16) take = 16;
      memcpy(block, payload + offset, take);
      char msg[36];
      snprintf(msg, sizeof(msg), "Writing bytes (%u/%u)...",
               (unsigned)(offset + take), (unsigned)payloadLen);
      ProgressView::progress(msg, (int)(offset * 100u / payloadLen));
      if (!firstPrepared) {
        memcpy(firstFinal, block, 16); memcpy(firstStaged, block, 16);
        firstStaged[1] = 0x00; firstBlockNo = blockNo;
        ok = c.mf1WriteBlock(blockNo, 0x60, NFC_KEY_A, firstStaged);
        firstPrepared = ok;
      } else {
        ok = c.mf1WriteBlock(blockNo, 0x60, NFC_KEY_A, block);
      }
      offset += take;
    }
  }
  if (ok && firstPrepared) ok = c.mf1WriteBlock(firstBlockNo, 0x60, NFC_KEY_A, firstFinal);
  if (ok) ProgressView::progress("NDEF written", 100);
  ProgressView::finish();
  free(payload);
  c.setMode(0); _running = false;
  ShowStatusAction::show(ok ? "NDEF written" : "NDEF write failed");
  return ok;
}

void ChameleonMfcNdefScreen::_doErase() {
  renderOperationTitle("Erase NDEF");
  _running = true;
  if (!_scanClassic()) { _running = false; _goMenu(); return; }
  uint8_t sectors[39] = {}; size_t count = 0;
  if (!_readNdefSectors(sectors, sizeof(sectors), count)) {
    ChameleonClient::get().setMode(0); _running = false;
    ShowStatusAction::show("Not NDEF formatted"); _goMenu(); return;
  }
  const uint8_t blockNo = (uint8_t)_firstBlock(sectors[0]);
  uint8_t block[16] = {};
  auto& c = ChameleonClient::get();
  bool ok = c.mf1ReadBlock(blockNo, 0x60, NFC_KEY_A, block);
  if (ok) {
    block[0] = 0x03; block[1] = 0x00; block[2] = 0xFE;
    ok = c.mf1WriteBlock(blockNo, 0x60, NFC_KEY_A, block);
  }
  c.setMode(0); _running = false;
  _hasNdef = false; _ndefLen = 0;
  ShowStatusAction::show(ok ? "NDEF erased" : "NDEF erase failed");
  _goMenu();
}

void ChameleonMfcNdefScreen::_resetRows() {
  _rowCount = 0; _scrollView.resetScroll();
}

void ChameleonMfcNdefScreen::_addRow(const String& label, const String& value) {
  if (_rowCount >= MAX_ROWS) return;
  _labels[_rowCount] = label; _values[_rowCount] = value;
  _rows[_rowCount] = {_labels[_rowCount].c_str(), _values[_rowCount]};
  ++_rowCount;
}

void ChameleonMfcNdefScreen::_addWrapped(const String& label, const String& value) {
  String s = value; s.replace("\r\n", "\n"); s.replace("\r", "\n");
  int pos = 0; bool first = true;
  while (pos <= (int)s.length() && _rowCount < MAX_ROWS) {
    int nl = s.indexOf('\n', pos); if (nl < 0) nl = s.length();
    String line = s.substring(pos, nl);
    if (!line.length()) { _addRow(first ? label : "", ""); first = false; }
    else {
      for (int off = 0; off < (int)line.length() && _rowCount < MAX_ROWS; off += 28) {
        _addRow(first ? label : "", line.substring(off, min(off + 28, (int)line.length())));
        first = false;
      }
    }
    if (nl >= (int)s.length()) break;
    pos = nl + 1;
  }
}

String ChameleonMfcNdefScreen::_uidString() const {
  String out;
  for (uint8_t i = 0; i < _uidLen; ++i) {
    char b[4]; snprintf(b, sizeof(b), "%02X%s", _uid[i], i + 1 < _uidLen ? ":" : ""); out += b;
  }
  return out;
}

void ChameleonMfcNdefScreen::_showResult(const uint8_t* ndef, size_t ndefLen) {
  _state = STATE_RESULT; _resetRows(); _hasNdef = false; _ndefLen = 0;
  if (_uidLen) _addRow("UID", _uidString());
  if (!ndef) {
    _addRow("NDEF", "Not found");
    _scrollView.setRows(_rows, _rowCount); render(); return;
  }
  if (ndefLen <= MAX_NDEF_BYTES) {
    memcpy(_ndef, ndef, ndefLen); _ndefLen = ndefLen; _hasNdef = true;
  }
  _addRow("NDEF Size", String(ndefLen) + " bytes");
  if (_capacity) {
    _addRow("Capacity", String(_capacity) + " bytes");
    _addRow("Free", String(_capacity > ndefLen ? _capacity - ndefLen : 0) + " bytes");
  }
  if (!ndefLen) {
    _addRow("NDEF", "Empty"); _addRow("[Press]", "Actions");
    _scrollView.setRows(_rows, _rowCount); render(); return;
  }
  NdefParser::Result parsed;
  if (!NdefParser::parse(ndef, ndefLen, parsed)) {
    _addRow("NDEF", "Invalid record"); _scrollView.setRows(_rows, _rowCount); render(); return;
  }
  switch (parsed.kind) {
    case NdefParser::RECORD_TEXT:
      _addRow("Record", "Text"); if (parsed.language.length()) _addRow("Language", parsed.language); _addWrapped("Text", parsed.text); break;
    case NdefParser::RECORD_URL:
      _addRow("Record", "URI"); _addWrapped("URI", parsed.uri); break;
    case NdefParser::RECORD_PHONE:
      _addRow("Record", "Phone"); _addWrapped("Phone", parsed.phone); break;
    case NdefParser::RECORD_EMAIL:
      _addRow("Record", "Email"); _addWrapped("Mail", parsed.email); break;
    case NdefParser::RECORD_VCARD:
      _addRow("Record", "vCard");
      if (parsed.contact.length()) _addWrapped("Contact", parsed.contact);
      if (parsed.company.length()) _addWrapped("Company", parsed.company);
      if (parsed.address.length()) _addWrapped("Address", parsed.address);
      if (parsed.phone.length()) _addWrapped("Phone", parsed.phone);
      if (parsed.email.length()) _addWrapped("Mail", parsed.email);
      if (parsed.website.length()) _addWrapped("Website", parsed.website);
      break;
    default: _addRow("Record", "Unsupported"); break;
  }
  if (_hasNdef) _addRow("[Press]", _writePreview ? "Write to Tag" : "Actions");
  _scrollView.setRows(_rows, _rowCount); render();
}

void ChameleonMfcNdefScreen::_showActions() {
  static const InputSelectAction::Option opts[] = {
    {"Write to Tag", "write"}, {"Save to File", "save"},
  };
  const char* r = InputSelectAction::popup("NDEF Actions", opts, 2, nullptr);
  if (!r) { render(); return; }
  render();
  if (strcmp(r, "write") == 0) {
    if (_writeNdefRecord(_ndef, _ndefLen)) _goMenu(); else render();
  } else _saveCurrent();
}

void ChameleonMfcNdefScreen::_saveCurrent() {
  if (!_hasNdef || !_ndefLen || !Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Save failed"); render(); return;
  }
  Uni.Storage->makeDir("/unigeek"); Uni.Storage->makeDir("/unigeek/nfc"); Uni.Storage->makeDir(NDEF_DIR);
  String base = _uidString(); base.replace(":", ""); if (!base.length()) base = "unknown"; base += "_mifare";
  String name = InputTextAction::popup("File name", base);
  if (InputTextAction::wasCancelled()) { render(); return; }
  name.trim(); if (name.endsWith(".ndef")) name.remove(name.length() - 5);
  if (!name.length()) { render(); return; }
  for (int i = 0; i < (int)name.length(); ++i) {
    const char ch = name[i];
    const bool safe = isalnum((unsigned char)ch) || ch == '-' || ch == '_';
    if (!safe) name.setCharAt(i, '_');
  }
  String path = String(NDEF_DIR) + "/" + name + ".ndef";
  fs::File f = Uni.Storage->open(path.c_str(), "w"); bool ok = false;
  if (f) { ok = f.write(_ndef, _ndefLen) == _ndefLen; f.close(); }
  render();
  ShowStatusAction::show(ok ? "NDEF saved" : "Save failed");
  if (ok) { _goMenu(); return; }
  render();
}

void ChameleonMfcNdefScreen::_showWritePreview(const uint8_t* ndef, size_t ndefLen, bool fromFile) {
  _writePreview = true; _writePreviewFromFile = fromFile; _capacity = 0; _uidLen = 0;
  _showResult(ndef, ndefLen);
}

void ChameleonMfcNdefScreen::_writeText() {
  String v = InputTextAction::popup("Text", ""); if (InputTextAction::wasCancelled() || !v.length()) { _goWriteMenu(); return; }
  uint8_t b[MAX_NDEF_BYTES] = {}; size_t n = 0;
  if (!NdefBuilder::buildText(v, b, n, sizeof(b))) { ShowStatusAction::show("Text too large"); _goWriteMenu(); return; }
  _showWritePreview(b, n, false);
}
void ChameleonMfcNdefScreen::_writeUrl() {
  String v = InputTextAction::popup("URL", "https://"); if (InputTextAction::wasCancelled() || !v.length()) { _goWriteMenu(); return; }
  uint8_t b[MAX_NDEF_BYTES] = {}; size_t n = 0;
  if (!NdefBuilder::buildUrl(v, b, n, sizeof(b))) { ShowStatusAction::show("URL too large"); _goWriteMenu(); return; }
  _showWritePreview(b, n, false);
}
void ChameleonMfcNdefScreen::_writePhone() {
  String v = InputTextAction::popup("Phone", "", InputTextAction::INPUT_PHONE); if (InputTextAction::wasCancelled() || !v.length()) { _goWriteMenu(); return; }
  uint8_t b[MAX_NDEF_BYTES] = {}; size_t n = 0;
  if (!NdefBuilder::buildPhone(v, b, n, sizeof(b))) { ShowStatusAction::show("Phone too large"); _goWriteMenu(); return; }
  _showWritePreview(b, n, false);
}
void ChameleonMfcNdefScreen::_writeEmail() {
  String v = InputTextAction::popup("Email", ""); if (InputTextAction::wasCancelled() || !v.length()) { _goWriteMenu(); return; }
  uint8_t b[MAX_NDEF_BYTES] = {}; size_t n = 0;
  if (!NdefBuilder::buildEmail(v, b, n, sizeof(b))) { ShowStatusAction::show("Email too large"); _goWriteMenu(); return; }
  _showWritePreview(b, n, false);
}
void ChameleonMfcNdefScreen::_writeVcard() {
  String contact = InputTextAction::popup("Contact name", ""); if (InputTextAction::wasCancelled() || !contact.length()) { _goWriteMenu(); return; }
  String company = InputTextAction::popup("Company", ""); if (InputTextAction::wasCancelled()) { _goWriteMenu(); return; }
  String address = InputTextAction::popup("Address", ""); if (InputTextAction::wasCancelled()) { _goWriteMenu(); return; }
  String phone = InputTextAction::popup("Phone", "", InputTextAction::INPUT_PHONE); if (InputTextAction::wasCancelled()) { _goWriteMenu(); return; }
  String email = InputTextAction::popup("Mail", ""); if (InputTextAction::wasCancelled()) { _goWriteMenu(); return; }
  String website = InputTextAction::popup("Website", "https://"); if (InputTextAction::wasCancelled()) { _goWriteMenu(); return; }
  uint8_t b[MAX_NDEF_BYTES] = {}; size_t n = 0;
  if (!NdefBuilder::buildVcard(contact, company, address, phone, email, website, b, n, sizeof(b))) {
    ShowStatusAction::show("vCard too large"); _goWriteMenu(); return;
  }
  _showWritePreview(b, n, false);
}

void ChameleonMfcNdefScreen::_loadFilePicker() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) { ShowStatusAction::show("Storage unavailable"); _goWriteMenu(); return; }
  Uni.Storage->makeDir("/unigeek"); Uni.Storage->makeDir("/unigeek/nfc"); Uni.Storage->makeDir(NDEF_DIR);
  if (_pickDir.length() == 0 || !_pickDir.startsWith(NDEF_DIR)) _pickDir = NDEF_DIR;
  _browser.root = NDEF_DIR;
  _state = STATE_FILE_SELECT;
  uint8_t n = _browser.load(this, _pickDir, ".ndef"); setItems(_browser.items(), n); render();
}

void ChameleonMfcNdefScreen::_selectFile(uint8_t index) {
  if (index >= _browser.count()) return;
  const auto& e = _browser.entry(index);
  if (e.isDir) { _pickDir = e.path; _loadFilePicker(); return; }
  if (!Uni.Storage) return;
  fs::File f = Uni.Storage->open(e.path.c_str(), "r");
  if (!f || f.size() == 0 || f.size() > MAX_NDEF_BYTES) {
    if (f) f.close(); ShowStatusAction::show("Invalid NDEF file"); _loadFilePicker(); return;
  }
  const size_t len = f.size(); uint8_t b[MAX_NDEF_BYTES] = {};
  const bool ok = f.read(b, len) == (int)len; f.close();
  if (!ok) { ShowStatusAction::show("Read failed"); _loadFilePicker(); return; }
  _showWritePreview(b, len, true);
}
