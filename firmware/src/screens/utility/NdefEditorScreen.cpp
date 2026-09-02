#include "NdefEditorScreen.h"

#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/nfc/NdefBuilder.h"

static String _editorUriPrefix(uint8_t code) {
  switch (code) {
    case 0x00: return "";
    case 0x01: return "http://www.";
    case 0x02: return "https://www.";
    case 0x03: return "http://";
    case 0x04: return "https://";
    case 0x05: return "tel:";
    case 0x06: return "mailto:";
    case 0x07: return "ftp://anonymous:anonymous@";
    case 0x08: return "ftp://ftp.";
    case 0x09: return "ftps://";
    case 0x0A: return "sftp://";
    case 0x0B: return "smb://";
    case 0x0C: return "nfs://";
    case 0x0D: return "ftp://";
    case 0x0E: return "dav://";
    case 0x0F: return "news:";
    case 0x10: return "telnet://";
    case 0x11: return "imap:";
    case 0x12: return "rtsp://";
    case 0x13: return "urn:";
    case 0x14: return "pop:";
    case 0x15: return "sip:";
    case 0x16: return "sips:";
    case 0x17: return "tftp:";
    case 0x18: return "btspp://";
    case 0x19: return "btl2cap://";
    case 0x1A: return "btgoep://";
    case 0x1B: return "tcpobex://";
    case 0x1C: return "irdaobex://";
    case 0x1D: return "file://";
    case 0x1E: return "urn:epc:id:";
    case 0x1F: return "urn:epc:tag:";
    case 0x20: return "urn:epc:pat:";
    case 0x21: return "urn:epc:raw:";
    case 0x22: return "urn:epc:";
    case 0x23: return "urn:nfc:";
    default:   return "";
  }
}

static String _sanitizeNdefName(String name) {
  name.trim();
  if (name.endsWith(".ndef")) name.remove(name.length() - 5);
  if (name.length() == 0) name = "record";

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
  if (name.length() == 0) name = "record";
  return name;
}

static String _vcardValue(const String& card, const char* key) {
  int pos = 0;
  const String prefix = String(key) + ":";
  while (pos < (int)card.length()) {
    int end = card.indexOf('\n', pos);
    if (end < 0) end = card.length();
    String line = card.substring(pos, end);
    if (line.endsWith("\r")) line.remove(line.length() - 1);

    // Accept both plain properties ("TEL:") and parameterized ones
    // ("TEL;TYPE=CELL:").
    if (line.startsWith(prefix)) return line.substring(prefix.length());
    String semiPrefix = String(key) + ";";
    if (line.startsWith(semiPrefix)) {
      int colon = line.indexOf(':');
      if (colon >= 0) return line.substring(colon + 1);
    }
    pos = end + 1;
  }
  return "";
}

const char* NdefEditorScreen::title() {
  if (_state == STATE_PREVIEW_INITIAL || _state == STATE_PREVIEW_FINAL)
    return "NDEF Preview";
  if (_state == STATE_VCARD_FORM)
    return "Edit vCard";
  return "Edit NDEF Record";
}

void NdefEditorScreen::onInit() {
  _pickDir = _ndefPath;
  _openFiles();
}

void NdefEditorScreen::onUpdate() {
  if (_state == STATE_PREVIEW_INITIAL || _state == STATE_PREVIEW_FINAL) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();

      if (dir == INavigation::DIR_BACK) {
        _openFiles();
        return;
      }

      if (dir == INavigation::DIR_PRESS) {
        if (_recordType == REC_UNSUPPORTED) {
          ShowStatusAction::show("Record type not editable", 1500);
          render();
          return;
        }

        if (_state == STATE_PREVIEW_INITIAL) {
          // Keep the initial preview so the user can verify the selected file.
          // vCard then enters its field form and skips the redundant final preview.
          if (_recordType == REC_VCARD) {
            _openVcardForm();
            return;
          }

          if (!_editRecord()) {
            _openFiles();
            return;
          }

          uint8_t edited[MAX_NDEF_BYTES] = {};
          size_t editedLen = 0;
          if (_rebuildRecord(edited, editedLen)) {
            memcpy(_ndef, edited, editedLen);
            _ndefLen = editedLen;
            _parseNdef(_ndef, _ndefLen);
            _state = STATE_PREVIEW_FINAL;
          }
          _showPreview();
          return;
        }

        // Final preview: PRESS saves; successful save returns to file list.
        if (_saveEdited(_ndef, _ndefLen)) {
          _openFiles();
          return;
        }

        _showPreview();
        return;
      }

      _scrollView.onNav(dir);
    }
    return;
  }

  ListScreen::onUpdate();
}

void NdefEditorScreen::onRender() {
  if (_state == STATE_PREVIEW_INITIAL || _state == STATE_PREVIEW_FINAL) {
    _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  ListScreen::onRender();
}

void NdefEditorScreen::onBack() {
  if (_state == STATE_VCARD_FORM) {
    _state = STATE_PREVIEW_INITIAL;
    _showPreview();
    return;
  }

  if (_state == STATE_PREVIEW_INITIAL || _state == STATE_PREVIEW_FINAL) {
    _openFiles();
    return;
  }

  if (_pickDir == _ndefPath || _pickDir.length() == 0) {
    Screen.goBack();
    return;
  }

  int slash = _pickDir.lastIndexOf('/');
  String parent = (slash > 0) ? _pickDir.substring(0, slash) : String(_ndefPath);
  if (!parent.startsWith(_ndefPath)) parent = _ndefPath;
  _pickDir = parent;
  _openFiles();
}

void NdefEditorScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_VCARD_FORM) {
    if (index < 6) _editVcardField(index);
    else if (index == 6) _finishVcardEdit();
    return;
  }

  if (_state == STATE_FILE_SELECT) _selectFile(index);
}

void NdefEditorScreen::_openFiles() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable", 1500);
    Screen.goBack();
    return;
  }

  Uni.Storage->makeDir(_nfcPath);
  Uni.Storage->makeDir(_ndefPath);

  if (_pickDir.length() == 0) _pickDir = _ndefPath;
  _browser.root = _ndefPath;
  _state = STATE_FILE_SELECT;

  uint8_t n = _browser.load(this, _pickDir, ".ndef");
  if (n == 0) {
    ShowStatusAction::show("No .ndef files", 1500);
    Screen.goBack();
    return;
  }
  setItems(_browser.items(), n);
}

void NdefEditorScreen::_selectFile(uint8_t index) {
  if (index >= _browser.count()) return;

  const auto& e = _browser.entry(index);
  if (e.isDir) {
    _pickDir = e.path;
    _openFiles();
    return;
  }

  fs::File f = Uni.Storage->open(e.path.c_str(), "r");
  if (!f) {
    ShowStatusAction::show("Read failed", 1500);
    _openFiles();
    return;
  }

  const size_t len = f.size();
  if (len == 0 || len > MAX_NDEF_BYTES) {
    f.close();
    ShowStatusAction::show(len == 0 ? "Empty .ndef file" : "NDEF file too large", 1500);
    _openFiles();
    return;
  }

  const size_t got = f.read(_ndef, len);
  f.close();
  if (got != len) {
    ShowStatusAction::show("Read failed", 1500);
    _openFiles();
    return;
  }

  _ndefLen = len;
  _filePath = e.path;
  _baseName = e.name;
  if (_baseName.endsWith(".ndef")) _baseName.remove(_baseName.length() - 5);

  _parseNdef(_ndef, _ndefLen);
  _state = STATE_PREVIEW_INITIAL;
  _showPreview();
}

bool NdefEditorScreen::_parseNdef(const uint8_t* ndef, size_t len) {
  _recordType = REC_UNSUPPORTED;
  _text = _url = _phone = _email = "";
  _contact = _company = _address = _vcardPhone = _vcardEmail = _website = "";

  if (!ndef || len < 3) return false;

  size_t p = 0;
  uint8_t hdr = ndef[p++];
  bool sr = (hdr & 0x10) != 0;
  bool il = (hdr & 0x08) != 0;
  uint8_t tnf = hdr & 0x07;
  uint8_t typeLen = ndef[p++];

  uint32_t payloadLen = 0;
  if (sr) {
    if (p >= len) return false;
    payloadLen = ndef[p++];
  } else {
    if (p + 4 > len) return false;
    payloadLen = ((uint32_t)ndef[p] << 24) |
                 ((uint32_t)ndef[p + 1] << 16) |
                 ((uint32_t)ndef[p + 2] << 8) |
                 (uint32_t)ndef[p + 3];
    p += 4;
  }

  uint8_t idLen = 0;
  if (il) {
    if (p >= len) return false;
    idLen = ndef[p++];
  }

  if (p + typeLen + idLen + payloadLen > len) return false;

  const uint8_t* type = &ndef[p];
  p += typeLen;
  p += idLen;
  const uint8_t* payload = &ndef[p];

  if (tnf == 0x01 && typeLen == 1 && type[0] == 'T' && payloadLen >= 1) {
    uint8_t status = payload[0];
    bool utf16 = (status & 0x80) != 0;
    uint8_t langLen = status & 0x3F;
    if (utf16 || (size_t)1 + langLen > payloadLen) return false;
    for (size_t i = 1 + langLen; i < payloadLen; i++) _text += (char)payload[i];
    _recordType = REC_TEXT;
    return true;
  }

  if (tnf == 0x01 && typeLen == 1 && type[0] == 'U' && payloadLen >= 1) {
    String uri = _editorUriPrefix(payload[0]);
    for (size_t i = 1; i < payloadLen; i++) uri += (char)payload[i];

    if (uri.startsWith("tel:")) {
      _phone = uri.substring(4);
      _recordType = REC_PHONE;
    } else if (uri.startsWith("mailto:")) {
      _email = uri.substring(7);
      _recordType = REC_EMAIL;
    } else {
      _url = uri;
      _recordType = REC_URL;
    }
    return true;
  }

  String typeStr;
  for (uint8_t i = 0; i < typeLen; i++) typeStr += (char)type[i];
  typeStr.toLowerCase();

  if (tnf == 0x02 && (typeStr == "text/vcard" || typeStr == "text/x-vcard")) {
    String card;
    for (size_t i = 0; i < payloadLen; i++) card += (char)payload[i];

    _contact    = _vcardValue(card, "FN");
    _company    = _vcardValue(card, "ORG");
    _address    = _vcardValue(card, "ADR");
    _vcardPhone = _vcardValue(card, "TEL");
    _vcardEmail = _vcardValue(card, "EMAIL");
    _website    = _vcardValue(card, "URL");
    _recordType = REC_VCARD;
    return true;
  }

  return false;
}

void NdefEditorScreen::_resetRows() {
  _rowCount = 0;
}

void NdefEditorScreen::_pushRow(const String& label, const String& value) {
  if (_rowCount >= MAX_ROWS) return;
  _rowLabels[_rowCount] = label;
  _rowValues[_rowCount] = value;
  _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
  _rowCount++;
}

void NdefEditorScreen::_pushWrappedRow(const String& label, const String& value) {
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
           (value[pos] == ' ' || value[pos] == '\n' ||
            value[pos] == '\r' || value[pos] == '\t')) pos++;
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
  }
}

void NdefEditorScreen::_showPreview() {
  _resetRows();

  _pushRow("File", _baseName + ".ndef");

  switch (_recordType) {
    case REC_TEXT:
      _pushRow("Record", "Text");
      _pushWrappedRow("Text", _text);
      break;
    case REC_URL:
      _pushRow("Record", "URI");
      _pushWrappedRow("URI", _url);
      break;
    case REC_PHONE:
      _pushRow("Record", "Phone");
      _pushWrappedRow("Phone", _phone);
      break;
    case REC_EMAIL:
      _pushRow("Record", "Email");
      _pushWrappedRow("Mail", _email);
      break;
    case REC_VCARD:
      _pushRow("Record", "vCard");
      if (_contact.length())    _pushWrappedRow("Name", _contact);
      if (_company.length())    _pushWrappedRow("Company", _company);
      if (_address.length())    _pushWrappedRow("Address", _address);
      if (_vcardPhone.length()) _pushWrappedRow("Phone", _vcardPhone);
      if (_vcardEmail.length()) _pushWrappedRow("Mail", _vcardEmail);
      if (_website.length())    _pushWrappedRow("Website", _website);
      break;
    default:
      _pushRow("Record", "Unsupported");
      break;
  }

  if (_recordType == REC_UNSUPPORTED) {
    _pushRow("[Press]", "Not editable");
  } else {
    _pushRow("[Press]",
             _state == STATE_PREVIEW_FINAL ? "Save" : "Edit");
  }
  _scrollView.setRows(_rows, _rowCount);
  render();
}


void NdefEditorScreen::_openVcardForm() {
  _state = STATE_VCARD_FORM;
  _refreshVcardForm(0);
}

void NdefEditorScreen::_refreshVcardForm(uint8_t selected) {
  String* values[] = {
    &_contact, &_company, &_address, &_vcardPhone, &_vcardEmail, &_website
  };
  const char* labels[] = {"Name", "Company", "Address", "Phone", "Email", "Website"};

  for (uint8_t i = 0; i < 6; ++i) {
    _vcardDisplay[i] = values[i]->length() ? *values[i] : "-";
    _vcardItems[i] = {labels[i], _vcardDisplay[i].c_str()};
  }
  _vcardItems[6] = {"Save", nullptr};
  setItems(_vcardItems, 7, selected);
  render();
}

void NdefEditorScreen::_editVcardField(uint8_t index) {
  String* values[] = {
    &_contact, &_company, &_address, &_vcardPhone, &_vcardEmail, &_website
  };
  const char* titles[] = {"Name", "Company", "Address", "Phone", "Email", "Website"};

  String value;
  if (index == 3)
    value = InputTextAction::popup(titles[index], values[index]->c_str(),
                                   InputTextAction::INPUT_PHONE);
  else
    value = InputTextAction::popup(titles[index], values[index]->c_str());

  if (!InputTextAction::wasCancelled()) *values[index] = value;
  _refreshVcardForm(index);
}

bool NdefEditorScreen::_confirmVcardSave() {
  static constexpr InputSelectAction::Option opts[] = {
    {"Yes", "yes"},
    {"No", "no"},
  };
  const char* choice = InputSelectAction::popup("Save vCard?", opts, 2, "yes");
  return choice && !strcmp(choice, "yes");
}

void NdefEditorScreen::_finishVcardEdit() {
  if (!_confirmVcardSave()) {
    _refreshVcardForm(6);
    return;
  }

  uint8_t edited[MAX_NDEF_BYTES] = {};
  size_t editedLen = 0;
  if (!_rebuildRecord(edited, editedLen)) {
    _refreshVcardForm(6);
    return;
  }

  // The form is already the final vCard content view, so save directly.
  if (_saveEdited(edited, editedLen)) {
    _openFiles();
    return;
  }

  _refreshVcardForm(6);
}

bool NdefEditorScreen::_editRecord() {
  switch (_recordType) {
    case REC_TEXT: {
      String v = InputTextAction::popup("Text", _text.c_str());
      if (InputTextAction::wasCancelled() || v.length() == 0) return false;
      _text = v;
      return true;
    }
    case REC_URL: {
      String v = InputTextAction::popup("URL", _url.c_str());
      if (InputTextAction::wasCancelled() || v.length() == 0) return false;
      _url = v;
      return true;
    }
    case REC_PHONE: {
      String v = InputTextAction::popup("Phone", _phone.c_str(), InputTextAction::INPUT_PHONE);
      if (InputTextAction::wasCancelled() || v.length() == 0) return false;
      _phone = v;
      return true;
    }
    case REC_EMAIL: {
      String v = InputTextAction::popup("Email", _email.c_str());
      if (InputTextAction::wasCancelled() || v.length() == 0) return false;
      _email = v;
      return true;
    }
    case REC_VCARD: {
      String v = InputTextAction::popup("Contact name", _contact.c_str());
      if (InputTextAction::wasCancelled() || v.length() == 0) return false;
      _contact = v;

      v = InputTextAction::popup("Company", _company.c_str());
      if (InputTextAction::wasCancelled()) return false;
      _company = v;

      v = InputTextAction::popup("Address", _address.c_str());
      if (InputTextAction::wasCancelled()) return false;
      _address = v;

      v = InputTextAction::popup("Phone", _vcardPhone.c_str(), InputTextAction::INPUT_PHONE);
      if (InputTextAction::wasCancelled()) return false;
      _vcardPhone = v;

      v = InputTextAction::popup("Mail", _vcardEmail.c_str());
      if (InputTextAction::wasCancelled()) return false;
      _vcardEmail = v;

      v = InputTextAction::popup("Website", _website.c_str());
      if (InputTextAction::wasCancelled()) return false;
      _website = v;
      return true;
    }
    default:
      return false;
  }
}

bool NdefEditorScreen::_rebuildRecord(uint8_t* out, size_t& outLen) {
  outLen = 0;
  bool ok = false;

  switch (_recordType) {
    case REC_TEXT:
      ok = NdefBuilder::buildText(_text, out, outLen, MAX_NDEF_BYTES);
      break;
    case REC_URL:
      ok = NdefBuilder::buildUrl(_url, out, outLen, MAX_NDEF_BYTES);
      break;
    case REC_PHONE:
      ok = NdefBuilder::buildPhone(_phone, out, outLen, MAX_NDEF_BYTES);
      break;
    case REC_EMAIL:
      ok = NdefBuilder::buildEmail(_email, out, outLen, MAX_NDEF_BYTES);
      break;
    case REC_VCARD:
      ok = NdefBuilder::buildVcard(_contact, _company, _address, _vcardPhone,
                                   _vcardEmail, _website,
                                   out, outLen, MAX_NDEF_BYTES);
      break;
    default:
      return false;
  }

  if (!ok) ShowStatusAction::show("NDEF record too large", 1500);
  return ok;
}

bool NdefEditorScreen::_saveEdited(const uint8_t* ndef, size_t len) {
  if (!ndef || len == 0) return false;

  static constexpr InputSelectAction::Option opts[] = {
    {"Overwrite", "overwrite"},
    {"Save As",   "saveas"},
  };

  const char* choice = InputSelectAction::popup("Save", opts, 2);
  if (!choice) return false;

  String path = _filePath;

  if (!strcmp(choice, "saveas")) {
    String name = InputTextAction::popup("File name", _baseName.c_str());
    if (InputTextAction::wasCancelled()) return false;

    const String base = _sanitizeNdefName(name);
    path = String(_ndefPath) + "/" + base + ".ndef";

    if (Uni.Storage->exists(path.c_str())) {
      for (int n = 2; n < 1000; n++) {
        String candidate = String(_ndefPath) + "/" + base + "_(" + n + ").ndef";
        if (!Uni.Storage->exists(candidate.c_str())) {
          path = candidate;
          break;
        }
      }
    }
  }

  fs::File f = Uni.Storage->open(path.c_str(), "w");
  if (!f) {
    ShowStatusAction::show("Save failed", 1500);
    return false;
  }

  const size_t written = f.write(ndef, len);
  f.close();
  if (written != len) {
    ShowStatusAction::show("Save failed", 1500);
    return false;
  }

  _filePath = path;
  int slash = path.lastIndexOf('/');
  String file = (slash >= 0) ? path.substring(slash + 1) : path;
  _baseName = file;
  if (_baseName.endsWith(".ndef")) _baseName.remove(_baseName.length() - 5);

  ShowStatusAction::show(("Saved: " + file).c_str(), 1500);
  return true;
}
