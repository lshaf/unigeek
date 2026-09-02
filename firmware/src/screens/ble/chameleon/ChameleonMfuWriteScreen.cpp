#include "ChameleonMfuWriteScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"
#include "utils/nfc/NdefParser.h"

namespace {
static constexpr uint16_t kNtag215Bytes = 135u * 4u;
static constexpr uint16_t kWritablePages = 126u; // pages 4..129

void _mfuWriteProgress(uint16_t done, uint16_t total) {
  char msg[36];
  snprintf(msg, sizeof(msg), "Writing %u/%u pages",
           (unsigned)done, (unsigned)kWritablePages);
  const int pct = total ? (int)((uint32_t)done * 100u / total) : 0;
  ProgressView::progress(msg, pct);
}
}

ChameleonMfuWriteScreen::ChameleonMfuWriteScreen(
    const uint8_t* dump, uint16_t dumpLen,
    const ChameleonClient::MfuTagInfo& info)
    : _source(SOURCE_MEMORY), _sourceInfo(info) {
  if (dump && dumpLen) {
    _dump = (uint8_t*)malloc(dumpLen);
    if (_dump) {
      memcpy(_dump, dump, dumpLen);
      _dumpLen = dumpLen;
    }
  }
}

String ChameleonMfuWriteScreen::_uidString(const uint8_t* uid, uint8_t len) {
  String s;
  for (uint8_t i = 0; i < len; ++i) {
    char b[4];
    snprintf(b, sizeof(b), "%02X%s", uid[i], (i + 1 < len) ? ":" : "");
    s += b;
  }
  return s;
}

void ChameleonMfuWriteScreen::_addRow(const char* label, const String& value) {
  if (_rowCount >= kMaxRows) return;
  _labels[_rowCount] = label;
  _values[_rowCount] = value;
  _rows[_rowCount] = {_labels[_rowCount].c_str(), _values[_rowCount].c_str()};
  ++_rowCount;
}

void ChameleonMfuWriteScreen::_freeDump() {
  if (_dump) free(_dump);
  _dump = nullptr;
  _dumpLen = 0;
}

void ChameleonMfuWriteScreen::_restoreContext() {
  auto& c = ChameleonClient::get();
  if (_restoreSlot) {
    c.setActiveSlot(_previousSlot);
    _restoreSlot = false;
  }
  if (_restoreMode) {
    c.setMode(_previousMode);
    _restoreMode = false;
  }
}

bool ChameleonMfuWriteScreen::_loadFile() {
  if (!Uni.Storage) return false;
  fs::File f = Uni.Storage->open(_path.c_str(), "r");
  if (!f) return false;
  if (f.size() != kNtag215Bytes) { f.close(); return false; }

  _dump = (uint8_t*)malloc(kNtag215Bytes);
  if (!_dump) { f.close(); return false; }
  const int n = f.read(_dump, kNtag215Bytes);
  f.close();
  if (n != kNtag215Bytes) { _freeDump(); return false; }
  _dumpLen = kNtag215Bytes;

  _sourceInfo.type = ChameleonClient::MFU_NTAG215;
  _sourceInfo.pages = 135;
  _sourceInfo.uidLen = 7;
  _sourceInfo.uid[0] = _dump[0];
  _sourceInfo.uid[1] = _dump[1];
  _sourceInfo.uid[2] = _dump[2];
  _sourceInfo.uid[3] = _dump[4];
  _sourceInfo.uid[4] = _dump[5];
  _sourceInfo.uid[5] = _dump[6];
  _sourceInfo.uid[6] = _dump[7];
  return true;
}

bool ChameleonMfuWriteScreen::_loadSlot() {
  auto& c = ChameleonClient::get();
  ChameleonClient::SlotTypes types[8] = {};
  if (!c.getSlotTypes(types) || types[_slot].hfType != ChameleonClient::MFU_NTAG215)
    return false;

  if (c.getActiveSlot(&_previousSlot)) _restoreSlot = true;
  if (!c.setActiveSlot(_slot)) return false;

  uint8_t pages = 0;
  if (!c.mfuGetPageCount(&pages) || pages != 135) {
    _restoreContext();
    return false;
  }

  _dump = (uint8_t*)malloc(kNtag215Bytes);
  if (!_dump) { _restoreContext(); return false; }

  uint16_t done = 0;
  while (done < pages) {
    const uint8_t count = (uint8_t)min((uint16_t)32, (uint16_t)(pages - done));
    uint16_t got = 0;
    if (!c.mfuGetPageData((uint8_t)done, count, _dump + done * 4u, nullptr, &got) ||
        got < (uint16_t)count * 4u) {
      _freeDump();
      _restoreContext();
      return false;
    }
    done += count;
  }
  _dumpLen = kNtag215Bytes;
  _restoreContext();

  _sourceInfo.type = ChameleonClient::MFU_NTAG215;
  _sourceInfo.pages = 135;
  _sourceInfo.uidLen = 7;
  _sourceInfo.uid[0] = _dump[0];
  _sourceInfo.uid[1] = _dump[1];
  _sourceInfo.uid[2] = _dump[2];
  _sourceInfo.uid[3] = _dump[4];
  _sourceInfo.uid[4] = _dump[5];
  _sourceInfo.uid[5] = _dump[6];
  _sourceInfo.uid[6] = _dump[7];
  return true;
}

bool ChameleonMfuWriteScreen::_loadMemory() {
  return _dump && _dumpLen == kNtag215Bytes &&
         _sourceInfo.type == ChameleonClient::MFU_NTAG215 &&
         _sourceInfo.pages == 135;
}

bool ChameleonMfuWriteScreen::_loadSource() {
  if (_source == SOURCE_FILE) return _loadFile();
  if (_source == SOURCE_SLOT) return _loadSlot();
  return _loadMemory();
}

void ChameleonMfuWriteScreen::_buildSourcePreview() {
  _rowCount = 0;
  String sourceLabel = "Read Tag";
  if (_source == SOURCE_FILE) sourceLabel = "File";
  else if (_source == SOURCE_SLOT) sourceLabel = String("Slot ") + (_slot + 1);
  _addRow("Source", sourceLabel);
  _addRow("Type", "NTAG215");
  _addRow("UID", _uidString(_sourceInfo.uid, _sourceInfo.uidLen));
  _addRow("Pages", "135");
  _addRow("Dump", String(_dumpLen) + " bytes");

  const uint8_t* ndef = nullptr;
  size_t ndefLen = 0;
  NdefParser::Result parsed;
  if (NdefParser::extractType2Ndef(_dump, _dumpLen, &ndef, &ndefLen) &&
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
        if (parsed.text.length()) addWrappedRow("Text", parsed.text);
        break;
      case NdefParser::RECORD_URL:
        _addRow("NDEF", "URL"); _addRow("URL", parsed.uri); break;
      case NdefParser::RECORD_PHONE:
        _addRow("NDEF", "Phone"); _addRow("Phone", parsed.phone); break;
      case NdefParser::RECORD_EMAIL:
        _addRow("NDEF", "Email"); _addRow("Email", parsed.email); break;
      case NdefParser::RECORD_VCARD:
        _addRow("NDEF", "vCard");
        if (parsed.contact.length()) _addRow("Contact", parsed.contact);
        if (parsed.phone.length()) _addRow("Phone", parsed.phone);
        if (parsed.email.length()) _addRow("Email", parsed.email);
        break;
      default: _addRow("NDEF", "Unsupported"); break;
    }
  } else {
    _addRow("NDEF", "Not found");
  }
  _addRow("[Press]", "Write to Tag");
  _scrollView.setRows(_rows, _rowCount);
}

void ChameleonMfuWriteScreen::_detectTarget() {
  _busy = true;
  auto& c = ChameleonClient::get();
  if (!_restoreMode && c.getMode(&_previousMode)) _restoreMode = true;
  c.setMode(1);

  ShowStatusAction::show("Place target tag...", 0);
  bool ok = c.mfuDetect(&_targetInfo) &&
            _targetInfo.type == ChameleonClient::MFU_NTAG215 &&
            _targetInfo.pages == 135;
  _busy = false;

  if (!ok) {
    _restoreContext();
    render();
    ShowStatusAction::show("Target must be NTAG215", 1500);
    render();
    return;
  }

  _write();
}

void ChameleonMfuWriteScreen::_write() {
  _busy = true;
  ProgressView::init();
  ProgressView::progress("Writing 0/126 pages", 0);
  bool ok = ChameleonClient::get().mfuWriteNtag215User(
      _dump, _dumpLen, _mfuWriteProgress, &_targetInfo);
  ProgressView::finish();
  _busy = false;
  _restoreContext();

  if (ok) {
    ShowStatusAction::show("Tag written", 1600);
    _freeDump();
    Screen.goBack();
    return;
  }

  _buildSourcePreview();
  render();
  ShowStatusAction::show("Tag write failed", 1600);
  render();
}

void ChameleonMfuWriteScreen::onInit() {
  _busy = true;
  ShowStatusAction::show("Loading...", 0);
  bool ok = _loadSource();
  _busy = false;
  if (!ok) {
    render();
    const char* msg = _source == SOURCE_FILE ? "Invalid NTAG215 dump" :
                      (_source == SOURCE_SLOT ? "Slot is not NTAG215" :
                                                "Read tag is not NTAG215");
    ShowStatusAction::show(msg, 1600);
    Screen.goBack();
    return;
  }
  _buildSourcePreview();
}

void ChameleonMfuWriteScreen::onUpdate() {
  if (_busy || !Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();
  if (dir == INavigation::DIR_BACK) {
    _restoreContext();
    _freeDump();
    Screen.goBack();
    return;
  }
  if (dir == INavigation::DIR_PRESS) {
    _detectTarget();
    return;
  }
  _scrollView.onNav(dir);
}

void ChameleonMfuWriteScreen::onRender() {
  _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
}
