#include "NfcDumpGeneratorScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "screens/utility/NdefGeneratorScreen.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/nfc/NfcDumpBuilder.h"
#include "utils/nfc/NdefParser.h"
#include "utils/ble/ChameleonClient.h"
#include "screens/utility/NfcScreen.h"

#if defined(ESP32)
#include <esp_system.h>
#endif

static String _sanitizeDumpName(String name) {
  name.trim();
  if (name.length() == 0) name = "ntag215";

  if (name.endsWith(".bin")) name.remove(name.length() - 4);
  if (name.endsWith(".ndef")) name.remove(name.length() - 5);

  for (int i = 0; i < (int)name.length(); i++) {
    const char c = name[i];
    const bool ok =
        (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '-' || c == '_';
    if (!ok) name.setCharAt(i, '_');
  }

  while (name.indexOf("__") >= 0) name.replace("__", "_");
  while (name.startsWith("_")) name.remove(0, 1);
  while (name.endsWith("_")) name.remove(name.length() - 1);
  if (name.length() == 0) name = "ntag215";
  return name;
}

const char* NfcDumpGeneratorScreen::title() {
  switch (_state) {
    case STATE_TAG_TYPE:         return "Generate Dump";
    case STATE_NDEF_CONTENT:     return "NDEF Content";
    case STATE_NDEF_TYPE:        return "New NDEF Record";
    case STATE_NDEF_FILE_SELECT: return "NDEF Files";
    case STATE_NDEF_PREVIEW:     return "NDEF Preview";
  }
  return "Generate Dump";
}

void NfcDumpGeneratorScreen::onInit() {
  _goTagType();
}

void NfcDumpGeneratorScreen::onUpdate() {
  if (_state == STATE_NDEF_PREVIEW) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        if (_previewFromFile) {
          _openNdefFiles();
        } else {
          _previewNdefLen = 0;
          _previewSuggestedName = "";
          _previewFromFile = false;
          _ndefPickDir = "";
          _goTagType();
        }
      } else if (dir == INavigation::DIR_PRESS && _previewNdefLen > 0) {
        if (_saveDump(_previewNdef, _previewNdefLen, _previewSuggestedName)) {
          _previewNdefLen = 0;
          _previewSuggestedName = "";
          _previewFromFile = false;
          _ndefPickDir = "";
          Screen.goBack();
          return;
        } else {
          render();
        }
      } else {
        _scrollView.onNav(dir);
      }
    }
    return;
  }

  ListScreen::onUpdate();
}

void NfcDumpGeneratorScreen::onRender() {
  if (_state == STATE_NDEF_PREVIEW) {
    _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  ListScreen::onRender();
}

void NfcDumpGeneratorScreen::onBack() {
  switch (_state) {
    case STATE_TAG_TYPE:
      Screen.goBack();
      break;

    case STATE_NDEF_CONTENT:
      _goTagType();
      break;

    case STATE_NDEF_TYPE:
      _goNdefContent();
      break;

    case STATE_NDEF_PREVIEW:
      if (_previewFromFile) {
        _openNdefFiles();
      } else {
        _previewNdefLen = 0;
        _previewSuggestedName = "";
        _previewFromFile = false;
        _ndefPickDir = "";
        _goTagType();
      }
      break;

    case STATE_NDEF_FILE_SELECT:
      if (_ndefPickDir == _ndefPath || _ndefPickDir.length() == 0) {
        _ndefPickDir = "";
        _goNdefContent();
      } else {
        int slash = _ndefPickDir.lastIndexOf('/');
        String parent = (slash > 0) ? _ndefPickDir.substring(0, slash) : String(_ndefPath);
        if (!parent.startsWith(_ndefPath)) parent = _ndefPath;
        _ndefPickDir = parent;
        _openNdefFiles();
      }
      break;
  }
}

void NfcDumpGeneratorScreen::onItemSelected(uint8_t index) {
  switch (_state) {
    case STATE_TAG_TYPE:
      if (index == 0) {
        _tagType = TAG_MIFARE_CLASSIC_1K;
        _goNdefContent();
      } else if (index == 1) {
        _tagType = TAG_MIFARE_CLASSIC_4K;
        _goNdefContent();
      } else if (index == 2) {
        _tagType = TAG_NTAG210; _goNdefContent();
      } else if (index == 3) {
        _tagType = TAG_NTAG212; _goNdefContent();
      } else if (index == 4) {
        _tagType = TAG_NTAG213; _goNdefContent();
      } else if (index == 5) {
        _tagType = TAG_NTAG215; _goNdefContent();
      } else if (index == 6) {
        _tagType = TAG_NTAG216; _goNdefContent();
      }
      break;

    case STATE_NDEF_CONTENT:
      if (index == 0) {
        _goNdefType();
      } else if (index == 1) {
        _ndefPickDir = _ndefPath;
        _openNdefFiles();
      } else if (index == 2) {
        String emptyName;
        switch (_tagType) {
          case TAG_MIFARE_CLASSIC_1K: emptyName = "mfc1k_empty"; break;
          case TAG_MIFARE_CLASSIC_4K: emptyName = "mfc4k_empty"; break;
          case TAG_NTAG210: emptyName = "ntag210_empty"; break;
          case TAG_NTAG212: emptyName = "ntag212_empty"; break;
          case TAG_NTAG213: emptyName = "ntag213_empty"; break;
          case TAG_NTAG215: emptyName = "ntag215_empty"; break;
          case TAG_NTAG216: emptyName = "ntag216_empty"; break;
        }
        if (_saveDump(nullptr, 0, emptyName)) {
          Screen.goBack();
          return;
        }
        render();
      }
      break;

    case STATE_NDEF_TYPE: {
      uint8_t ndef[NdefGeneratorScreen::MAX_NDEF_BYTES] = {};
      size_t ndefLen = 0;
      String suggestedName;
      if (NdefGeneratorScreen::buildRecordInteractive(
              index, ndef, ndefLen, sizeof(ndef), suggestedName)) {
        _previewFromFile = false;
        _showNdefPreview(ndef, ndefLen, suggestedName);
      } else {
        render();
      }
      break;
    }

    case STATE_NDEF_FILE_SELECT:
      _selectNdefFile(index);
      break;
  }
}

void NfcDumpGeneratorScreen::_goTagType() {
  _state = STATE_TAG_TYPE;
  setItems(_tagItems);
}

void NfcDumpGeneratorScreen::_goNdefContent() {
  _state = STATE_NDEF_CONTENT;
  setItems(_contentItems);
}

void NfcDumpGeneratorScreen::_goNdefType() {
  _state = STATE_NDEF_TYPE;
  setItems(_ndefTypeItems);
}

void NfcDumpGeneratorScreen::_openNdefFiles() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable", 1500);
    _goNdefContent();
    return;
  }

  Uni.Storage->makeDir(_nfcPath);
  Uni.Storage->makeDir(_ndefPath);

  if (_ndefPickDir.length() == 0) _ndefPickDir = _ndefPath;
  _browser.root = _ndefPath;
  _state = STATE_NDEF_FILE_SELECT;

  uint8_t n = _browser.load(this, _ndefPickDir, ".ndef");
  if (n == 0) {
    ShowStatusAction::show("No .ndef files", 1500);
    _ndefPickDir = "";
    _goNdefContent();
    return;
  }
  setItems(_browser.items(), n);
}

void NfcDumpGeneratorScreen::_selectNdefFile(uint8_t index) {
  if (index >= _browser.count()) return;

  const auto& e = _browser.entry(index);
  if (e.isDir) {
    _ndefPickDir = e.path;
    _openNdefFiles();
    return;
  }

  fs::File f = Uni.Storage->open(e.path.c_str(), "r");
  if (!f) {
    ShowStatusAction::show("Read failed", 1500);
    _openNdefFiles();
    return;
  }

  const size_t len = f.size();
  if (len == 0 || len > MAX_INPUT_NDEF) {
    f.close();
    ShowStatusAction::show(len == 0 ? "Empty .ndef file" : "NDEF file too large", 1500);
    _openNdefFiles();
    return;
  }

  uint8_t ndef[MAX_INPUT_NDEF];
  const size_t got = f.read(ndef, len);
  f.close();
  if (got != len) {
    ShowStatusAction::show("Read failed", 1500);
    _openNdefFiles();
    return;
  }

  String base = e.name;
  if (base.endsWith(".ndef")) base.remove(base.length() - 5);
  _previewFromFile = true;
  _showNdefPreview(ndef, len, base);
}






void NfcDumpGeneratorScreen::_resetRows() { _rowCount = 0; }

void NfcDumpGeneratorScreen::_pushRow(const String& label, const String& value) {
  if (_rowCount >= MAX_ROWS) return;
  _rowLabels[_rowCount] = label;
  _rowValues[_rowCount] = value;
  _rows[_rowCount] = { _rowLabels[_rowCount].c_str(), _rowValues[_rowCount] };
  _rowCount++;
}

String NfcDumpGeneratorScreen::_hexBlock(const uint8_t* data, uint8_t len) const {
  String s;
  for (uint8_t i = 0; i < len; i++) {
    char buf[4];
    sprintf(buf, "%s%02X", i == 0 ? "" : " ", data[i]);
    s += buf;
  }
  return s;
}

void NfcDumpGeneratorScreen::_pushWrappedRow(const String& label, const String& value) {
  if (value.length() == 0) {
    _pushRow(label, "");
    return;
  }

  int totalChars = (bodyW() - 10) / 6;
  if (totalChars < 12) totalChars = 12;
  int firstChars = totalChars - (int)label.length() - 2;
  if (firstChars < 8) firstChars = 8;

  int pos = 0;
  bool first = true;
  while (pos < (int)value.length() && _rowCount < MAX_ROWS) {
    while (pos < (int)value.length() &&
           (value[pos] == ' ' || value[pos] == '\n' || value[pos] == '\r' || value[pos] == '\t')) pos++;
    if (pos >= (int)value.length()) break;

    const int maxChars = first ? firstChars : totalChars;
    int end = pos + maxChars;
    if (end > (int)value.length()) end = value.length();
    int newline = value.indexOf('\n', pos);
    if (newline >= pos && newline < end) {
      end = newline;
    } else if (end < (int)value.length()) {
      int split = -1;
      for (int i = end; i > pos; --i) {
        if (value[i - 1] == ' ' || value[i - 1] == '\t') { split = i - 1; break; }
      }
      if (split > pos) end = split;
    }
    if (end <= pos) end = min(pos + maxChars, (int)value.length());

    String part = value.substring(pos, end);
    part.trim();
    _pushRow(first ? label : "", part);
    pos = end;
    first = false;
    while (pos < (int)value.length() &&
           (value[pos] == ' ' || value[pos] == '\n' || value[pos] == '\r' || value[pos] == '\t')) pos++;
  }
}

void NfcDumpGeneratorScreen::_showNdefPreview(const uint8_t* ndef, size_t ndefLen,
                                             const String& suggestedName) {
  _state = STATE_NDEF_PREVIEW;
  _resetRows();
  _previewNdefLen = 0;
  _previewSuggestedName = suggestedName;

  if (!ndef || ndefLen == 0 || ndefLen > MAX_INPUT_NDEF) {
    _pushRow("NDEF", "Invalid record");
    _scrollView.setRows(_rows, _rowCount);
    render();
    return;
  }

  memcpy(_previewNdef, ndef, ndefLen);
  _previewNdefLen = ndefLen;

  char lenBuf[24];
  snprintf(lenBuf, sizeof(lenBuf), "%u bytes", (unsigned)ndefLen);
  _pushRow("NDEF Size", lenBuf);

  auto finishPreview = [&]() {
    if (_previewNdefLen > 0) _pushRow("[Press]", "Generate Dump");
    _scrollView.setRows(_rows, _rowCount);
    render();
  };

  NdefParser::Result parsed;
  if (!NdefParser::parse(ndef, ndefLen, parsed)) {
    _pushRow("NDEF", "Invalid record");
    finishPreview();
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

  finishPreview();
}


bool NfcDumpGeneratorScreen::_saveDump(const uint8_t* ndef, size_t ndefLen,
                                     const String& suggestedName) {
  switch (_tagType) {
    case TAG_MIFARE_CLASSIC_1K:
      return _saveMifareClassic1K(ndef, ndefLen, suggestedName);
    case TAG_MIFARE_CLASSIC_4K:
      return _saveMifareClassic4K(ndef, ndefLen, suggestedName);
    case TAG_NTAG210:
    case TAG_NTAG212:
    case TAG_NTAG213:
    case TAG_NTAG215:
    case TAG_NTAG216:
      return _saveNtag21x(ndef, ndefLen, suggestedName);
  }
  return false;
}

void NfcDumpGeneratorScreen::_generateMifareUid(uint8_t uid[4]) {
#if defined(ESP32)
  const uint32_t r = esp_random();
  uid[0] = (uint8_t)(r >> 0);
  uid[1] = (uint8_t)(r >> 8);
  uid[2] = (uint8_t)(r >> 16);
  uid[3] = (uint8_t)(r >> 24);
#else
  for (uint8_t i = 0; i < 4; i++) uid[i] = (uint8_t)random(0, 256);
#endif
}

bool NfcDumpGeneratorScreen::_saveMifareClassic1K(
    const uint8_t* ndef, size_t ndefLen, const String&) {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable", 1500);
    return false;
  }

  uint8_t uid[4] = {};
  _generateMifareUid(uid);

  uint8_t image[NfcDumpBuilder::MIFARE_CLASSIC_1K_SIZE] = {};
  size_t imageLen = 0;
  if (!NfcDumpBuilder::buildMifareClassic1K(
          uid, ndef, ndefLen, image, imageLen, sizeof(image))) {
    ShowStatusAction::show("Cannot build MFC1K", 1500);
    return false;
  }

  char suggested[32] = {};
  size_t suggestedPos = snprintf(suggested, sizeof(suggested), "%s_",
                                 ChameleonClient::tagTypeName(1001));
  for (uint8_t i = 0; i < 4 && suggestedPos + 2 < sizeof(suggested); ++i) {
    suggestedPos += snprintf(suggested + suggestedPos,
                             sizeof(suggested) - suggestedPos, "%02X", uid[i]);
  }
  String name = InputTextAction::popup("File name", suggested);
  if (InputTextAction::wasCancelled()) return false;

  Uni.Storage->makeDir(_nfcPath);
  Uni.Storage->makeDir(_dumpPath);

  const String base = _sanitizeDumpName(name);
  String path = String(_dumpPath) + "/" + base + ".bin";

  if (Uni.Storage->exists(path.c_str())) {
    for (int n = 2; n < 1000; n++) {
      String candidate = String(_dumpPath) + "/" + base + "_(" + n + ").bin";
      if (!Uni.Storage->exists(candidate.c_str())) {
        path = candidate;
        break;
      }
    }
  }

  fs::File f = Uni.Storage->open(path.c_str(), "w");
  if (!f) {
    ShowStatusAction::show("Save failed", 1500);
    return false;
  }

  const size_t written = f.write(image, imageLen);
  f.close();
  if (written != imageLen) {
    ShowStatusAction::show("Save failed", 1500);
    return false;
  }

  const int slash = path.lastIndexOf('/');
  const String saved = (slash >= 0) ? path.substring(slash + 1) : path;
  ShowStatusAction::show(("Saved: " + saved).c_str(), 1500);
  return true;
}


bool NfcDumpGeneratorScreen::_saveMifareClassic4K(
    const uint8_t* ndef, size_t ndefLen, const String&) {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable", 1500);
    return false;
  }

  uint8_t uid[4] = {};
  _generateMifareUid(uid);

  // Keep the 4 KiB image off the task stack.
  uint8_t* image = (uint8_t*)malloc(NfcDumpBuilder::MIFARE_CLASSIC_4K_SIZE);
  if (!image) {
    ShowStatusAction::show("Out of memory", 1500);
    return false;
  }

  size_t imageLen = 0;
  const bool built = NfcDumpBuilder::buildMifareClassic4K(
      uid, ndef, ndefLen, image, imageLen,
      NfcDumpBuilder::MIFARE_CLASSIC_4K_SIZE);

  if (!built) {
    free(image);
    ShowStatusAction::show("Cannot build MFC4K", 1500);
    return false;
  }

  char suggested[32] = {};
  size_t suggestedPos = snprintf(suggested, sizeof(suggested), "%s_",
                                 ChameleonClient::tagTypeName(1003));
  for (uint8_t i = 0; i < 4 && suggestedPos + 2 < sizeof(suggested); ++i) {
    suggestedPos += snprintf(suggested + suggestedPos,
                             sizeof(suggested) - suggestedPos, "%02X", uid[i]);
  }

  String name = InputTextAction::popup("File name", suggested);
  if (InputTextAction::wasCancelled()) {
    free(image);
    return false;
  }

  Uni.Storage->makeDir(_nfcPath);
  Uni.Storage->makeDir(_dumpPath);

  const String base = _sanitizeDumpName(name);
  String path = String(_dumpPath) + "/" + base + ".bin";

  if (Uni.Storage->exists(path.c_str())) {
    for (int n = 2; n < 1000; ++n) {
      String candidate = String(_dumpPath) + "/" + base + "_(" + n + ").bin";
      if (!Uni.Storage->exists(candidate.c_str())) {
        path = candidate;
        break;
      }
    }
  }

  fs::File f = Uni.Storage->open(path.c_str(), "w");
  if (!f) {
    free(image);
    ShowStatusAction::show("Save failed", 1500);
    return false;
  }

  const size_t written = f.write(image, imageLen);
  f.close();
  free(image);

  if (written != imageLen) {
    ShowStatusAction::show("Save failed", 1500);
    return false;
  }

  const int slash = path.lastIndexOf('/');
  const String saved = (slash >= 0) ? path.substring(slash + 1) : path;
  ShowStatusAction::show(("Saved: " + saved).c_str(), 1500);
  return true;
}

void NfcDumpGeneratorScreen::_generateUid(uint8_t uid[7]) {
  uid[0] = 0x04; // NXP manufacturer ID

#if defined(ESP32)
  uint32_t r1 = esp_random();
  uint32_t r2 = esp_random();
  uid[1] = (uint8_t)(r1 >> 0);
  uid[2] = (uint8_t)(r1 >> 8);
  uid[3] = (uint8_t)(r1 >> 16);
  uid[4] = (uint8_t)(r1 >> 24);
  uid[5] = (uint8_t)(r2 >> 0);
  uid[6] = (uint8_t)(r2 >> 8);
#else
  for (uint8_t i = 1; i < 7; i++) uid[i] = (uint8_t)random(0, 256);
#endif
}

bool NfcDumpGeneratorScreen::_saveNtag21x(
    const uint8_t* ndef, size_t ndefLen, const String&) {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable", 1500); return false;
  }

  NfcDumpBuilder::Ntag21xType bt;
  uint16_t cuType=0;
  size_t imageSize=0;
  switch (_tagType) {
    case TAG_NTAG210: bt=NfcDumpBuilder::Ntag21xType::NTAG210; cuType=1107; imageSize=NfcDumpBuilder::NTAG210_SIZE; break;
    case TAG_NTAG212: bt=NfcDumpBuilder::Ntag21xType::NTAG212; cuType=1108; imageSize=NfcDumpBuilder::NTAG212_SIZE; break;
    case TAG_NTAG213: bt=NfcDumpBuilder::Ntag21xType::NTAG213; cuType=1100; imageSize=NfcDumpBuilder::NTAG213_SIZE; break;
    case TAG_NTAG215: bt=NfcDumpBuilder::Ntag21xType::NTAG215; cuType=1101; imageSize=NfcDumpBuilder::NTAG215_SIZE; break;
    case TAG_NTAG216: bt=NfcDumpBuilder::Ntag21xType::NTAG216; cuType=1102; imageSize=NfcDumpBuilder::NTAG216_SIZE; break;
    default: return false;
  }

  uint8_t uid[7]={}; _generateUid(uid);
  uint8_t* image=(uint8_t*)malloc(imageSize);
  if (!image) { ShowStatusAction::show("Out of memory",1500); return false; }

  size_t imageLen=0;
  if (!NfcDumpBuilder::buildNtag21x(bt,uid,ndef,ndefLen,image,imageLen,imageSize)) {
    free(image); ShowStatusAction::show("Cannot build NTAG",1500); return false;
  }

  char suggested[40]={};
  size_t n=snprintf(suggested,sizeof(suggested),"%s_",ChameleonClient::tagTypeName(cuType));
  for (uint8_t k=0;k<7 && n+2<sizeof(suggested);++k)
    n+=snprintf(suggested+n,sizeof(suggested)-n,"%02X",uid[k]);

  String name=InputTextAction::popup("File name",suggested);
  if (InputTextAction::wasCancelled()) { free(image); return false; }

  Uni.Storage->makeDir(_nfcPath); Uni.Storage->makeDir(_dumpPath);
  const String base=_sanitizeDumpName(name);
  String path=String(_dumpPath)+"/"+base+".bin";
  if (Uni.Storage->exists(path.c_str())) {
    for (int k=2;k<1000;++k) {
      String candidate=String(_dumpPath)+"/"+base+"_("+k+").bin";
      if (!Uni.Storage->exists(candidate.c_str())) { path=candidate; break; }
    }
  }

  fs::File f=Uni.Storage->open(path.c_str(),"w");
  if (!f) { free(image); ShowStatusAction::show("Save failed",1500); return false; }
  const size_t written=f.write(image,imageLen);
  f.close(); free(image);
  if (written!=imageLen) { ShowStatusAction::show("Save failed",1500); return false; }

  const int slash=path.lastIndexOf('/');
  const String saved=(slash>=0)?path.substring(slash+1):path;
  ShowStatusAction::show(("Saved: "+saved).c_str(),1500);
  return true;
}
