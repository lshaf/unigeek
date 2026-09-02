#include "ChameleonMfcWriteScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"
#include "utils/nfc/NdefParser.h"

namespace {
static constexpr uint16_t kClassic1KBytes = 1024;
static constexpr uint8_t kSectors = 16;
static constexpr uint8_t kBlocks = 64;

// Keep this list aligned with the built-in Classic dictionary used by the
// reader. Source trailer keys are added dynamically before this list.
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
static constexpr uint8_t kBuiltinCount = sizeof(kBuiltinKeys) / sizeof(kBuiltinKeys[0]);

static uint8_t trailerBlock(uint8_t sector) { return sector * 4u + 3u; }

}

ChameleonMfcWriteScreen::ChameleonMfcWriteScreen(const uint8_t* dump, uint16_t dumpLen)
    : _source(SOURCE_MEMORY) {
  if (dump && dumpLen) {
    _dump = (uint8_t*)malloc(dumpLen);
    if (_dump) { memcpy(_dump, dump, dumpLen); _dumpLen = dumpLen; }
  }
}

String ChameleonMfcWriteScreen::_uidString(const uint8_t* uid, uint8_t len) {
  String s;
  for (uint8_t i = 0; i < len; ++i) {
    char b[4];
    snprintf(b, sizeof(b), "%02X%s", uid[i], (i + 1 < len) ? ":" : "");
    s += b;
  }
  return s;
}

void ChameleonMfcWriteScreen::_addRow(const char* label, const String& value) {
  if (_rowCount >= kMaxRows) return;
  _labels[_rowCount] = label; _values[_rowCount] = value;
  _rows[_rowCount] = {_labels[_rowCount].c_str(), _values[_rowCount].c_str()};
  ++_rowCount;
}

void ChameleonMfcWriteScreen::_freeDump() {
  if (_dump) free(_dump);
  _dump = nullptr; _dumpLen = 0;
}

void ChameleonMfcWriteScreen::_restoreContext() {
  auto& c = ChameleonClient::get();
  if (_restoreSlot) { c.setActiveSlot(_previousSlot); _restoreSlot = false; }
  if (_restoreMode) { c.setMode(_previousMode); _restoreMode = false; }
}

bool ChameleonMfcWriteScreen::_loadFile() {
  if (!Uni.Storage) return false;
  fs::File f = Uni.Storage->open(_path.c_str(), "r");
  if (!f || f.size() != kClassic1KBytes) { if (f) f.close(); return false; }
  _dump = (uint8_t*)malloc(kClassic1KBytes);
  if (!_dump) { f.close(); return false; }
  int n = f.read(_dump, kClassic1KBytes);
  f.close();
  if (n != kClassic1KBytes) { _freeDump(); return false; }
  _dumpLen = kClassic1KBytes;
  return true;
}

bool ChameleonMfcWriteScreen::_loadSlot() {
  auto& c = ChameleonClient::get();
  ChameleonClient::SlotTypes types[8] = {};
  if (!c.getSlotTypes(types) || types[_slot].hfType != 1001) return false;
  if (c.getActiveSlot(&_previousSlot)) _restoreSlot = true;
  if (!c.setActiveSlot(_slot)) return false;
  _dump = (uint8_t*)malloc(kClassic1KBytes);
  if (!_dump) { _restoreContext(); return false; }
  uint8_t block = 0;
  while (block < kBlocks) {
    uint8_t count = (uint8_t)min((uint8_t)8, (uint8_t)(kBlocks - block));
    uint16_t got = 0;
    if (!c.mf1GetBlockData(block, count, _dump + (size_t)block * 16u, nullptr, &got) ||
        got < (uint16_t)count * 16u) {
      _freeDump(); _restoreContext(); return false;
    }
    block += count;
  }
  _dumpLen = kClassic1KBytes;
  _restoreContext();
  return true;
}

bool ChameleonMfcWriteScreen::_loadMemory() {
  return _dump && _dumpLen == kClassic1KBytes;
}

bool ChameleonMfcWriteScreen::_loadSource() {
  if (_source == SOURCE_FILE) return _loadFile();
  if (_source == SOURCE_SLOT) return _loadSlot();
  return _loadMemory();
}

bool ChameleonMfcWriteScreen::_extractNdef(uint8_t** ndef, size_t* ndefLen) const {
  if (ndef) *ndef = nullptr; if (ndefLen) *ndefLen = 0;
  if (!_dump || _dumpLen != kClassic1KBytes || !ndef || !ndefLen) return false;
  uint8_t sectors[15] = {}; size_t count = 0;
  const uint8_t* b1 = _dump + 16; const uint8_t* b2 = _dump + 32;
  for (uint8_t s = 1; s <= 7; ++s) {
    size_t off = 2u + (size_t)(s - 1u) * 2u;
    if (b1[off] == 0x03 && b1[off + 1] == 0xE1) sectors[count++] = s;
  }
  for (uint8_t s = 8; s <= 15; ++s) {
    size_t off = (size_t)(s - 8u) * 2u;
    if (b2[off] == 0x03 && b2[off + 1] == 0xE1) sectors[count++] = s;
  }
  if (!count) return false;
  uint8_t area[15 * 48] = {}; size_t out = 0;
  for (size_t i = 0; i < count; ++i) {
    uint16_t first = (uint16_t)sectors[i] * 4u;
    for (uint8_t b = 0; b < 3; ++b) {
      memcpy(area + out, _dump + (size_t)(first + b) * 16u, 16); out += 16;
    }
  }
  size_t pos = 0;
  while (pos < out) {
    uint8_t tlv = area[pos++];
    if (tlv == 0x00) continue; if (tlv == 0xFE || pos >= out) break;
    size_t len = area[pos++];
    if (len == 0xFF) { if (pos + 1 >= out) break; len = ((size_t)area[pos] << 8) | area[pos+1]; pos += 2; }
    if (pos + len > out) break;
    if (tlv == 0x03 && len) {
      uint8_t* p = (uint8_t*)malloc(len); if (!p) return false;
      memcpy(p, area + pos, len); *ndef = p; *ndefLen = len; return true;
    }
    pos += len;
  }
  return false;
}

void ChameleonMfcWriteScreen::_buildSourcePreview() {
  _rowCount = 0;
  String source = "Read Tag";
  if (_source == SOURCE_FILE) source = "File"; else if (_source == SOURCE_SLOT) source = String("Slot ") + (_slot + 1);
  _addRow("Source", source);
  _addRow("Type", "MIFARE Classic 1K");
  _addRow("UID", _uidString(_dump, 4));
  _addRow("Blocks", "64");
  _addRow("Dump", String(_dumpLen) + " bytes");
  uint8_t* ndef = nullptr; size_t ndefLen = 0; NdefParser::Result parsed;
  if (_extractNdef(&ndef, &ndefLen) && NdefParser::parse(ndef, ndefLen, parsed)) {
    auto addWrappedRow = [&](const String& label, const String& value) {
      if (value.length() == 0) {
        _addRow(label.c_str(), "");
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
          _addRow(first ? label.c_str() : "", "");
          first = false;
        } else {
          static constexpr int kChunk = 28;
          int off = 0;
          while (off < (int)line.length()) {
            int end = min(off + kChunk, (int)line.length());
            _addRow(first ? label.c_str() : "", line.substring(off, end));
            first = false;
            off = end;
          }
        }
        if (nl >= (int)normalized.length()) break;
        pos = nl + 1;
      }
    };

    switch (parsed.kind) {
      case NdefParser::RECORD_TEXT: _addRow("NDEF", "Text"); if (parsed.text.length()) addWrappedRow("Text", parsed.text); break;
      case NdefParser::RECORD_URL: _addRow("NDEF", "URL"); _addRow("URL", parsed.uri); break;
      case NdefParser::RECORD_PHONE: _addRow("NDEF", "Phone"); _addRow("Phone", parsed.phone); break;
      case NdefParser::RECORD_EMAIL: _addRow("NDEF", "Email"); _addRow("Email", parsed.email); break;
      case NdefParser::RECORD_VCARD:
        _addRow("NDEF", "vCard");
        if (parsed.contact.length()) _addRow("Contact", parsed.contact);
        if (parsed.phone.length()) _addRow("Phone", parsed.phone);
        if (parsed.email.length()) _addRow("Email", parsed.email);
        break;
      default: _addRow("NDEF", "Unsupported"); break;
    }
  } else _addRow("NDEF", "Not found");
  if (ndef) free(ndef);
  _addRow("[Press]", "Write to Tag");
  _scrollView.setRows(_rows, _rowCount);
}

bool ChameleonMfcWriteScreen::_resolveTargetKeys(
    uint8_t keysA[16][6], bool foundA[16],
    uint8_t keysB[16][6], bool foundB[16]) {
  uint8_t candidates[83][6] = {};
  uint8_t count = 0;
  auto addKey = [&](const uint8_t* key) {
    for (uint8_t i = 0; i < count; ++i) {
      if (memcmp(candidates[i], key, 6) == 0) return;
    }
    if (count < 83) memcpy(candidates[count++], key, 6);
  };

  // Prefer keys carried by the source dump, then fall back to the same
  // built-in candidates used by the Classic reader.
  for (uint8_t s = 0; s < kSectors; ++s) {
    const uint8_t* tr = _dump + (size_t)trailerBlock(s) * 16u;
    addKey(tr);
    addKey(tr + 10);
  }
  for (uint8_t i = 0; i < kBuiltinCount; ++i) addKey(kBuiltinKeys[i]);

  if (!count) {
    return false;
  }

  auto checkKeyType = [&](uint8_t block, uint8_t keyType, uint8_t outKey[6]) {
    // CMD 2015 responses are small and reliable over BLE. Keep each request
    // to the same practical batch size already used by the client.
    static constexpr uint8_t kBatch = 32;
    for (uint8_t off = 0; off < count; off = (uint8_t)(off + kBatch)) {
      uint8_t n = (uint8_t)min((uint8_t)kBatch, (uint8_t)(count - off));
      if (ChameleonClient::get().mf1CheckKeysOfBlock(
              block, keyType, &candidates[off][0], n, outKey)) {
        return true;
      }
    }
    return false;
  };

  for (uint8_t s = 0; s < kSectors; ++s) {
    const uint8_t block = (uint8_t)(s * 4u);
    foundA[s] = checkKeyType(block, 0x60, keysA[s]);
    foundB[s] = checkKeyType(block, 0x61, keysB[s]);

    if (!foundA[s] && !foundB[s]) {
      return false;
    }
  }
  return true;
}

bool ChameleonMfcWriteScreen::_writeTarget(
    const uint8_t keysA[16][6], const bool foundA[16],
    const uint8_t keysB[16][6], const bool foundB[16]) {
  auto& c = ChameleonClient::get();
  uint16_t written = 0;
  static constexpr uint16_t totalWrites = 63; // all blocks except immutable block 0
  ProgressView::init(); ProgressView::progress("Writing 0/63 blocks", 0);
  for (uint8_t s = 0; s < kSectors; ++s) {
    const uint8_t first = s * 4u; const uint8_t trailer = trailerBlock(s);
    for (uint8_t b = first; b < trailer; ++b) {
      if (b == 0) continue;
      char msg[36]; snprintf(msg, sizeof(msg), "Writing %u/%u blocks", (unsigned)(written + 1u), (unsigned)totalWrites);
      ProgressView::progress(msg, (int)((uint32_t)written * 100u / totalWrites));
      const uint8_t* data = _dump + (size_t)b * 16u; bool ok = false;
      if (foundA[s]) ok = c.mf1WriteBlock(b, 0x60, keysA[s], data);
      if (!ok && foundB[s]) ok = c.mf1WriteBlock(b, 0x61, keysB[s], data);
      if (!ok) {
        ProgressView::finish();
        return false;
      }
      ++written;
    }
    // Trailer last so changing keys/access bits cannot lock us out before the
    // sector data blocks have been written.
    const uint8_t* data = _dump + (size_t)trailer * 16u; bool ok = false;
    char msg[36]; snprintf(msg, sizeof(msg), "Writing %u/%u blocks", (unsigned)(written + 1u), (unsigned)totalWrites);
    ProgressView::progress(msg, (int)((uint32_t)written * 100u / totalWrites));
    if (foundA[s]) ok = c.mf1WriteBlock(trailer, 0x60, keysA[s], data);
    if (!ok && foundB[s]) ok = c.mf1WriteBlock(trailer, 0x61, keysB[s], data);
    if (!ok) {
      ProgressView::finish();
      return false;
    }
    ++written;
  }
  ProgressView::progress("Writing 63/63 blocks", 100); ProgressView::finish();
  return true;
}

void ChameleonMfcWriteScreen::_write() {
  _busy = true; auto& c = ChameleonClient::get();
  if (!_restoreMode && c.getMode(&_previousMode)) _restoreMode = true;
  c.setMode(1);
  ShowStatusAction::show("Place target tag...", 0);

  uint8_t uid[7] = {}, uidLen = 0, atqa[2] = {}, sak = 0;
  bool targetOk = c.scan14A(uid, &uidLen, atqa, &sak) && sak == 0x08 && c.mf1Support();
  uint8_t keysA[16][6] = {}, keysB[16][6] = {}; bool foundA[16] = {}, foundB[16] = {};
  bool ok = targetOk && _resolveTargetKeys(keysA, foundA, keysB, foundB);
  if (ok) ok = _writeTarget(keysA, foundA, keysB, foundB);
  _busy = false; _restoreContext();

  if (ok) {
    ShowStatusAction::show("Tag written", 1600);
    _freeDump(); Screen.goBack(); return;
  }
  _buildSourcePreview(); render();
  ShowStatusAction::show("Tag write failed", 1600); render();
}

void ChameleonMfcWriteScreen::onInit() {
  _busy = true; ShowStatusAction::show("Loading...", 0);
  bool ok = _loadSource(); _busy = false;
  if (!ok) {
    render();
    const char* msg = _source == SOURCE_FILE ? "Invalid Classic 1K dump" :
                      (_source == SOURCE_SLOT ? "Slot is not Classic 1K" : "Read tag is not Classic 1K");
    ShowStatusAction::show(msg, 1600); Screen.goBack(); return;
  }
  _buildSourcePreview();
}

void ChameleonMfcWriteScreen::onUpdate() {
  if (_busy || !Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();
  if (dir == INavigation::DIR_BACK) { _restoreContext(); _freeDump(); Screen.goBack(); return; }
  if (dir == INavigation::DIR_PRESS) { _write(); return; }
  _scrollView.onNav(dir);
}

void ChameleonMfcWriteScreen::onRender() { _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH()); }
