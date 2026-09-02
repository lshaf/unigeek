#include "NdefGeneratorScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/nfc/NdefBuilder.h"
#include "utils/nfc/NdefParser.h"

static String _sanitizeNdefName(String name) {
  name.trim();
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



const char* NdefGeneratorScreen::title() {
  if (_state == STATE_PREVIEW) return "NDEF Preview";
  if (_state == STATE_VCARD_FORM) return "vCard";
  return "New NDEF Record";
}

void NdefGeneratorScreen::onInit() {
  _state = STATE_TYPE_SELECT;
  setItems(_items);
}

void NdefGeneratorScreen::onUpdate() {
  if (_state == STATE_PREVIEW) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) {
        _previewNdefLen = 0;
        _previewSuggestedName = "";
        Screen.goBack();
      } else if (dir == INavigation::DIR_PRESS) {
        String name = InputTextAction::popup("File name", _previewSuggestedName.c_str());
        if (InputTextAction::wasCancelled()) {
          _previewNdefLen = 0;
          _previewSuggestedName = "";
          _state = STATE_TYPE_SELECT;
          setItems(_items);
          return;
        }
        if (_saveNdef(_previewNdef, _previewNdefLen, name)) {
          _previewNdefLen = 0;
          _previewSuggestedName = "";
          _state = STATE_TYPE_SELECT;
          setItems(_items);
        }
        render();
      } else {
        _scrollView.onNav(dir);
      }
    }
    return;
  }
  ListScreen::onUpdate();
}

void NdefGeneratorScreen::onRender() {
  if (_state == STATE_PREVIEW) {
    _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  ListScreen::onRender();
}

void NdefGeneratorScreen::onBack() {
  if (_state == STATE_VCARD_FORM) {
    _state = STATE_TYPE_SELECT;
    setItems(_items);
    render();
    return;
  }
  if (_state == STATE_PREVIEW) {
    _previewNdefLen = 0;
    _previewSuggestedName = "";
  }
  Screen.goBack();
}

void NdefGeneratorScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_VCARD_FORM) {
    if (index < 6) _editVcardField(index);
    else if (index == 6) _saveVcardFromForm();
    return;
  }

  if (index == 4) {
    _openVcardForm();
    return;
  }

  uint8_t ndef[MAX_NDEF_BYTES] = {};
  size_t ndefLen = 0;
  String suggestedName;

  if (!buildRecordInteractive(index, ndef, ndefLen, sizeof(ndef), suggestedName)) {
    render();
    return;
  }

  memcpy(_previewNdef, ndef, ndefLen);
  _previewNdefLen = ndefLen;
  _previewSuggestedName = suggestedName;
  _showPreview(_previewNdef, _previewNdefLen);
}


void NdefGeneratorScreen::_openVcardForm() {
  _vcardContact = "";
  _vcardCompany = "";
  _vcardAddress = "";
  _vcardPhone = "";
  _vcardEmail = "";
  _vcardWebsite = "";
  _state = STATE_VCARD_FORM;
  _refreshVcardForm(0);
}

void NdefGeneratorScreen::_refreshVcardForm(uint8_t selected) {
  String* values[] = {
    &_vcardContact, &_vcardCompany, &_vcardAddress,
    &_vcardPhone, &_vcardEmail, &_vcardWebsite
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

void NdefGeneratorScreen::_editVcardField(uint8_t index) {
  String* values[] = {
    &_vcardContact, &_vcardCompany, &_vcardAddress,
    &_vcardPhone, &_vcardEmail, &_vcardWebsite
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

bool NdefGeneratorScreen::_confirmVcardSave() {
  static constexpr InputSelectAction::Option opts[] = {
    {"Yes", "yes"},
    {"No", "no"},
  };
  const char* choice = InputSelectAction::popup("Save vCard?", opts, 2, "yes");
  return choice && !strcmp(choice, "yes");
}

void NdefGeneratorScreen::_saveVcardFromForm() {
  if (!_confirmVcardSave()) {
    _refreshVcardForm(6);
    return;
  }

  uint8_t ndef[MAX_NDEF_BYTES] = {};
  size_t ndefLen = 0;
  if (!NdefBuilder::buildVcard(_vcardContact, _vcardCompany, _vcardAddress,
                               _vcardPhone, _vcardEmail, _vcardWebsite,
                               ndef, ndefLen, sizeof(ndef))) {
    ShowStatusAction::show("vCard too large");
    _refreshVcardForm(6);
    return;
  }

  String name = InputTextAction::popup("File name", "vcard");
  if (InputTextAction::wasCancelled()) {
    _refreshVcardForm(6);
    return;
  }

  if (_saveNdef(ndef, ndefLen, name)) {
    _state = STATE_TYPE_SELECT;
    setItems(_items);
  } else {
    _refreshVcardForm(6);
  }
  render();
}

void NdefGeneratorScreen::_resetRows() { _rowCount = 0; }

void NdefGeneratorScreen::_pushRow(const String& label, const String& value) {
  if (_rowCount >= MAX_ROWS) return;
  _rowLabels[_rowCount] = label;
  _rowValues[_rowCount] = value;
  _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
  _rowCount++;
}

void NdefGeneratorScreen::_pushWrappedRow(const String& label, const String& value) {
  if (value.length() == 0) { _pushRow(label, ""); return; }
  int totalChars = (bodyW() - 10) / 6;
  if (totalChars < 12) totalChars = 12;
  int firstChars = totalChars - (int)label.length() - 2;
  if (firstChars < 8) firstChars = 8;

  int pos = 0;
  bool first = true;
  while (pos < (int)value.length() && _rowCount < MAX_ROWS) {
    const int maxChars = first ? firstChars : totalChars;
    int end = min(pos + maxChars, (int)value.length());
    String part = value.substring(pos, end);
    part.trim();
    _pushRow(first ? label : "", part);
    pos = end;
    first = false;
  }
}

void NdefGeneratorScreen::_showPreview(const uint8_t* ndef, size_t ndefLen) {
  _state = STATE_PREVIEW;
  _resetRows();

  char lenBuf[24];
  snprintf(lenBuf, sizeof(lenBuf), "%u bytes", (unsigned)ndefLen);
  _pushRow("NDEF Size", lenBuf);

  NdefParser::Result parsed;
  if (!NdefParser::parse(ndef, ndefLen, parsed)) {
    _pushRow("NDEF", "Invalid record");
  } else {
    switch (parsed.kind) {
      case NdefParser::RECORD_TEXT:
        _pushRow("Record", "Text");
        _pushWrappedRow("Text", parsed.text);
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
        if (parsed.contact.length()) _pushWrappedRow("Contact", parsed.contact);
        if (parsed.company.length()) _pushWrappedRow("Company", parsed.company);
        if (parsed.address.length()) _pushWrappedRow("Address", parsed.address);
        if (parsed.phone.length()) _pushWrappedRow("Phone", parsed.phone);
        if (parsed.email.length()) _pushWrappedRow("Mail", parsed.email);
        if (parsed.website.length()) _pushWrappedRow("Website", parsed.website);
        break;

      default: {
        String typeStr = parsed.type;
        typeStr.toLowerCase();
        _pushRow("Record", typeStr.length() ? typeStr : "Unknown");
        break;
      }
    }
  }

  _pushRow("[Press]", "Save");
  _scrollView.setRows(_rows, _rowCount);
  render();
}

bool NdefGeneratorScreen::buildRecordInteractive(uint8_t index,
                                                 uint8_t* out, size_t& outLen,
                                                 size_t maxLen,
                                                 String& suggestedName) {
  if (!out || maxLen == 0) return false;
  outLen = 0;

  switch (index) {
    case 0: {
      String value = InputTextAction::popup("Text", "");
      if (InputTextAction::wasCancelled() || value.length() == 0) return false;
      if (!NdefBuilder::buildText(value, out, outLen, maxLen)) {
        ShowStatusAction::show("Text too long");
        return false;
      }
      suggestedName = "text";
      return true;
    }

    case 1: {
      String url = InputTextAction::popup("URL", "https://");
      if (InputTextAction::wasCancelled() || url.length() == 0) return false;
      if (!NdefBuilder::buildUrl(url, out, outLen, maxLen)) {
        ShowStatusAction::show("URL too long");
        return false;
      }
      suggestedName = "url";
      return true;
    }

    case 2: {
      String phone = InputTextAction::popup("Phone", "", InputTextAction::INPUT_PHONE);
      if (InputTextAction::wasCancelled() || phone.length() == 0) return false;
      if (!NdefBuilder::buildPhone(phone, out, outLen, maxLen)) {
        ShowStatusAction::show("Phone too long");
        return false;
      }
      suggestedName = "phone";
      return true;
    }

    case 3: {
      String email = InputTextAction::popup("Email", "");
      if (InputTextAction::wasCancelled() || email.length() == 0) return false;
      if (!NdefBuilder::buildEmail(email, out, outLen, maxLen)) {
        ShowStatusAction::show("Email too long");
        return false;
      }
      suggestedName = "email";
      return true;
    }

    case 4: {
      String contact = InputTextAction::popup("Contact name", "");
      if (InputTextAction::wasCancelled() || contact.length() == 0) return false;

      String company = InputTextAction::popup("Company", "");
      if (InputTextAction::wasCancelled()) return false;

      String address = InputTextAction::popup("Address", "");
      if (InputTextAction::wasCancelled()) return false;

      String phone = InputTextAction::popup("Phone", "", InputTextAction::INPUT_PHONE);
      if (InputTextAction::wasCancelled()) return false;

      String mail = InputTextAction::popup("Mail", "");
      if (InputTextAction::wasCancelled()) return false;

      String website = InputTextAction::popup("Website", "https://");
      if (InputTextAction::wasCancelled()) return false;

      if (!NdefBuilder::buildVcard(contact, company, address, phone, mail, website,
                                   out, outLen, maxLen)) {
        ShowStatusAction::show("vCard too large");
        return false;
      }
      suggestedName = "vcard";
      return true;
    }
  }

  return false;
}

bool NdefGeneratorScreen::_saveNdef(const uint8_t* ndef, size_t ndefLen,
                                    const String& suggestedName) {
  if (!ndef || ndefLen == 0 || ndefLen > MAX_NDEF_BYTES) {
    ShowStatusAction::show("Invalid NDEF record");
    return false;
  }

  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable");
    return false;
  }

  Uni.Storage->makeDir(_nfcPath);
  Uni.Storage->makeDir(_ndefPath);

  const String base = _sanitizeNdefName(suggestedName);
  String path = String(_ndefPath) + "/" + base + ".ndef";

  if (Uni.Storage->exists(path.c_str())) {
    for (int n = 2; n < 1000; n++) {
      String candidate = String(_ndefPath) + "/" + base + "_(" + n + ").ndef";
      if (!Uni.Storage->exists(candidate.c_str())) {
        path = candidate;
        break;
      }
    }
  }

  fs::File f = Uni.Storage->open(path.c_str(), "w");
  if (!f) {
    ShowStatusAction::show("Save failed");
    return false;
  }

  const size_t written = f.write(ndef, ndefLen);
  f.close();

  if (written != ndefLen) {
    ShowStatusAction::show("Save failed");
    return false;
  }

  const int slash = path.lastIndexOf('/');
  const String name = (slash >= 0) ? path.substring(slash + 1) : path;
  ShowStatusAction::show(("Saved: " + name).c_str(), 1500);
  return true;
}
