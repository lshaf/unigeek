#include "ChameleonSlotContentScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "utils/nfc/NdefParser.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"

void ChameleonSlotContentScreen::_addRow(const char* label, const String& value) {
  if (_rowCount >= MAX_ROWS) return;
  _labels[_rowCount] = label;
  _values[_rowCount] = value;
  _rows[_rowCount] = {_labels[_rowCount].c_str(), _values[_rowCount].c_str()};
  ++_rowCount;
}

void ChameleonSlotContentScreen::_freeDump() {
  if (_dump) {
    free(_dump);
    _dump = nullptr;
  }
  _dumpLen = 0;
  _blocks = 0;
  _pages = 0;
}

void ChameleonSlotContentScreen::_restoreActiveSlot() {
  if (_restoreSlot) {
    ChameleonClient::get().setActiveSlot(_previousSlot);
    _restoreSlot = false;
  }
}

bool ChameleonSlotContentScreen::_extractClassicNdef(uint8_t** ndef,
                                                      size_t* ndefLen) const {
  if (ndef) *ndef = nullptr;
  if (ndefLen) *ndefLen = 0;
  if (!_dump || !_dumpLen || !ndef || !ndefLen) return false;

  const uint8_t sectors = (_hfType == 1000) ? 5
                        : (_hfType == 1003) ? 40
                        : (_hfType == 1002) ? 32
                                           : 16;

  uint8_t ndefSectors[39] = {};
  size_t sectorCount = 0;

  auto addIfNdef = [&](uint8_t sector, uint8_t lo, uint8_t hi) {
    if (sector >= sectors || sectorCount >= sizeof(ndefSectors)) return;
    if (lo == 0x03 && hi == 0xE1) ndefSectors[sectorCount++] = sector;
  };

  // MAD1: sector 0, blocks 1 and 2.
  if (_dumpLen >= 48) {
    const uint8_t* b1 = _dump + 16;
    const uint8_t* b2 = _dump + 32;
    for (uint8_t s = 1; s <= 7 && s < sectors; ++s) {
      size_t off = 2 + (size_t)(s - 1) * 2;
      addIfNdef(s, b1[off], b1[off + 1]);
    }
    for (uint8_t s = 8; s <= 15 && s < sectors; ++s) {
      size_t off = (size_t)(s - 8) * 2;
      addIfNdef(s, b2[off], b2[off + 1]);
    }
  }

  // MAD2 for larger Classic layouts: sector 16, blocks 64..66.
  if (sectors > 16 && _dumpLen >= (67u * 16u)) {
    const uint8_t* m0 = _dump + 64u * 16u;
    const uint8_t* m1 = _dump + 65u * 16u;
    const uint8_t* m2 = _dump + 66u * 16u;

    for (uint8_t s = 17; s <= 23 && s < sectors; ++s) {
      size_t off = 2 + (size_t)(s - 17) * 2;
      addIfNdef(s, m0[off], m0[off + 1]);
    }
    for (uint8_t s = 24; s <= 31 && s < sectors; ++s) {
      size_t off = (size_t)(s - 24) * 2;
      addIfNdef(s, m1[off], m1[off + 1]);
    }
    for (uint8_t s = 32; s <= 39 && s < sectors; ++s) {
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

void ChameleonSlotContentScreen::_buildPreview() {
  _rowCount = 0;

  _addRow("Type", ChameleonClient::tagTypeName(_hfType));
  _addRow("Blocks", String(_blocks));
  _addRow("Dump", String(_dumpLen) + " bytes");

  uint8_t* ndef = nullptr;
  size_t ndefLen = 0;
  NdefParser::Result parsed;

  if (_extractClassicNdef(&ndef, &ndefLen) &&
      NdefParser::parse(ndef, ndefLen, parsed)) {
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
      case NdefParser::RECORD_TEXT:
        _addRow("NDEF", "Text");
        if (parsed.language.length()) _addRow("Language", parsed.language);
        if (parsed.text.length()) addWrappedRow("Text", parsed.text);
        break;

      case NdefParser::RECORD_URL:
        _addRow("NDEF", "URL");
        _addRow("URL", parsed.uri);
        break;

      case NdefParser::RECORD_PHONE:
        _addRow("NDEF", "Phone");
        _addRow("Phone", parsed.phone);
        break;

      case NdefParser::RECORD_EMAIL:
        _addRow("NDEF", "Email");
        _addRow("Email", parsed.email);
        break;

      case NdefParser::RECORD_VCARD:
        _addRow("NDEF", "vCard");
        if (parsed.contact.length()) _addRow("Contact", parsed.contact);
        if (parsed.company.length()) _addRow("Company", parsed.company);
        if (parsed.address.length()) _addRow("Address", parsed.address);
        if (parsed.phone.length()) _addRow("Phone", parsed.phone);
        if (parsed.email.length()) _addRow("Email", parsed.email);
        if (parsed.website.length()) _addRow("Website", parsed.website);
        break;

      default:
        _addRow("NDEF", "Unsupported");
        break;
    }
  } else {
    _addRow("NDEF", "Not found");
  }

  if (ndef) free(ndef);
  _scrollView.setRows(_rows, _rowCount);
}


void ChameleonSlotContentScreen::_buildMfuPreview() {
  _rowCount = 0;
  _addRow("Type", ChameleonClient::tagTypeName(_hfType));
  _addRow("Pages", String(_pages));
  _addRow("Dump", String(_dumpLen) + " bytes");

  const uint8_t* ndef = nullptr;
  size_t ndefLen = 0;
  NdefParser::Result parsed;

  if (NdefParser::extractType2Ndef(_dump, _dumpLen, &ndef, &ndefLen) &&
      NdefParser::parse(ndef, ndefLen, parsed)) {
    switch (parsed.kind) {
      case NdefParser::RECORD_TEXT:
        _addRow("NDEF", "Text");
        if (parsed.language.length()) _addRow("Language", parsed.language);
        if (parsed.text.length()) _addRow("Text", parsed.text);
        break;
      case NdefParser::RECORD_URL:
        _addRow("NDEF", "URL");
        _addRow("URL", parsed.uri);
        break;
      case NdefParser::RECORD_PHONE:
        _addRow("NDEF", "Phone");
        _addRow("Phone", parsed.phone);
        break;
      case NdefParser::RECORD_EMAIL:
        _addRow("NDEF", "Email");
        _addRow("Email", parsed.email);
        break;
      case NdefParser::RECORD_VCARD:
        _addRow("NDEF", "vCard");
        if (parsed.contact.length()) _addRow("Contact", parsed.contact);
        if (parsed.company.length()) _addRow("Company", parsed.company);
        if (parsed.address.length()) _addRow("Address", parsed.address);
        if (parsed.phone.length()) _addRow("Phone", parsed.phone);
        if (parsed.email.length()) _addRow("Email", parsed.email);
        if (parsed.website.length()) _addRow("Website", parsed.website);
        break;
      default:
        _addRow("NDEF", "Unsupported");
        break;
    }
  } else {
    _addRow("NDEF", "Not found");
  }

  _scrollView.setRows(_rows, _rowCount);
}

void ChameleonSlotContentScreen::_run() {
  auto& c = ChameleonClient::get();

  ChameleonClient::SlotTypes types[8] = {};
  if (!c.getSlotTypes(types)) {
    _addRow("Error", "Could not read slot type");
    return;
  }

  _hfType = types[_slot].hfType;

  const bool isClassic = _hfType >= 1000 && _hfType <= 1003;
  const bool isMfu = _hfType >= ChameleonClient::MFU_NTAG213 &&
                     _hfType <= ChameleonClient::MFU_NTAG212;

  if (!isClassic && !isMfu) {
    _addRow("Type", ChameleonClient::tagTypeName(_hfType));
    _addRow("Content", "Not supported yet");
    return;
  }

  // Emulator-memory commands operate on the active slot, so temporarily select
  // the slot chosen in Slot Manager. onUpdate() restores the previous active slot.
  if (!c.setActiveSlot(_slot)) {
    _addRow("Error", "Could not select slot");
    return;
  }
  delay(50);

  if (isMfu) {
    uint8_t pageCount = 0;
    if (!c.mfuGetPageCount(&pageCount) || pageCount == 0) {
      _addRow("Error", "Could not read page count");
      return;
    }

    _pages = pageCount;
    _dumpLen = (uint16_t)_pages * 4u;
    _dump = (uint8_t*)malloc(_dumpLen);
    if (!_dump) {
      _dumpLen = 0;
      _addRow("Error", "Out of memory");
      return;
    }

    ProgressView::init();
    uint16_t done = 0;
    // Keep each response comfortably below the BLE notification buffer.
    const uint8_t maxChunkPages = 32;
    while (done < _pages) {
      uint8_t count = (uint8_t)min<uint16_t>(maxChunkPages, _pages - done);
      char msg[32];
      snprintf(msg, sizeof(msg), "Reading %u/%u pages",
               (unsigned)(done + count), (unsigned)_pages);
      int pct = (int)(((uint32_t)(done + count) * 100u) / _pages);
      ProgressView::progress(msg, pct);

      uint16_t st = 0, rlen = 0;
      if (!c.mfuGetPageData((uint8_t)done, count,
                            _dump + (size_t)done * 4u, &st, &rlen)) {
        ProgressView::finish();
        char diag[36];
        snprintf(diag, sizeof(diag), "Page %u failed", (unsigned)done);
        _addRow("Error", diag);
        _freeDump();
        return;
      }
      done += count;
    }
    ProgressView::finish();
    _buildMfuPreview();
    return;
  }

  _blocks = (_hfType == 1000) ? 20
          : (_hfType == 1002) ? 128
          : (_hfType == 1003) ? 256
                              : 64;
  _dumpLen = (uint16_t)(_blocks * 16u);
  _dump = (uint8_t*)malloc(_dumpLen);
  if (!_dump) {
    _dumpLen = 0;
    _addRow("Error", "Out of memory");
    return;
  }

  ProgressView::init();
  uint8_t block[16];
  for (uint16_t b = 0; b < _blocks; ++b) {
    char msg[32];
    snprintf(msg, sizeof(msg), "Reading %u/%u blocks",
             (unsigned)(b + 1), (unsigned)_blocks);
    const int pct = (int)(((uint32_t)(b + 1) * 100u) / _blocks);
    ProgressView::progress(msg, pct);

    uint16_t st = 0, rlen = 0;
    if (!c.mf1GetBlockData((uint8_t)b, 1, block, &st, &rlen)) {
      ProgressView::finish();
      char diag[36];
      snprintf(diag, sizeof(diag), "Block %u failed", (unsigned)b);
      _addRow("Error", diag);
      _freeDump();
      return;
    }
    memcpy(_dump + (size_t)b * 16u, block, 16);
  }
  ProgressView::finish();

  _buildPreview();
}

void ChameleonSlotContentScreen::onInit() {
  snprintf(_title, sizeof(_title), "Slot %d Content", _slot + 1);

  auto& c = ChameleonClient::get();
  _restoreSlot = c.getActiveSlot(&_previousSlot) && _previousSlot != _slot;

  _rowCount = 0;
  _loading = true;
  ShowStatusAction::show("Loading...", 0);
  _run();

  _scrollView.setRows(_rows, _rowCount);
  _loading = false;
  render();
}

void ChameleonSlotContentScreen::onUpdate() {
  if (_loading) return;

  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _restoreActiveSlot();
      _freeDump();
      Screen.goBack();
      return;
    }
    _scrollView.onNav(dir);
  }
}

void ChameleonSlotContentScreen::onRender() {
  _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
}
