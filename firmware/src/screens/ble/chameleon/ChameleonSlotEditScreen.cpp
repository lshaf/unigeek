#include "ChameleonSlotEditScreen.h"
#include "utils/chameleon/ChameleonClient.h"
#include "ChameleonSlotsScreen.h"
#include "ChameleonSlotViewScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "ui/actions/InputSelectOption.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"

void ChameleonSlotEditScreen::_load() {
  auto& c = ChameleonClient::get();
  snprintf(_title, sizeof(_title), "Slot %d", _slot + 1);

  uint8_t act = 0;
  if (c.getActiveSlot(&act)) _isActive = (act == _slot);

  ChameleonClient::SlotTypes types[8] = {};
  if (c.getSlotTypes(types)) {
    _hfType = types[_slot].hfType;
    _lfType = types[_slot].lfType;
  }

  bool hfEn[8] = {}, lfEn[8] = {};
  if (c.getEnabledSlots(hfEn, lfEn)) {
    _hfEnabled = hfEn[_slot];
    _lfEnabled = lfEn[_slot];
  }

  c.getSlotNick(_slot, 2, _hfNick, sizeof(_hfNick));
  c.getSlotNick(_slot, 1, _lfNick, sizeof(_lfNick));
  _rebuildLabels();
}

void ChameleonSlotEditScreen::_rebuildLabels() {
  snprintf(_labels[0], sizeof(_labels[0]), "Set active");
  snprintf(_subs[0],   sizeof(_subs[0]),   "%s", _isActive ? "[*]" : "-");

  snprintf(_labels[1], sizeof(_labels[1]), "HF type");
  snprintf(_subs[1],   sizeof(_subs[1]),   "%s", ChameleonClient::tagTypeName(_hfType));

  snprintf(_labels[2], sizeof(_labels[2]), "LF type");
  snprintf(_subs[2],   sizeof(_subs[2]),   "%s", ChameleonClient::tagTypeName(_lfType));

  snprintf(_labels[3], sizeof(_labels[3]), "HF enable");
  snprintf(_subs[3],   sizeof(_subs[3]),   "%s", _hfEnabled ? "On" : "Off");

  snprintf(_labels[4], sizeof(_labels[4]), "LF enable");
  snprintf(_subs[4],   sizeof(_subs[4]),   "%s", _lfEnabled ? "On" : "Off");

  snprintf(_labels[5], sizeof(_labels[5]), "HF nickname");
  snprintf(_subs[5],   sizeof(_subs[5]),   "%s", _hfNick[0] ? _hfNick : "-");

  snprintf(_labels[6], sizeof(_labels[6]), "LF nickname");
  snprintf(_subs[6],   sizeof(_subs[6]),   "%s", _lfNick[0] ? _lfNick : "-");

  snprintf(_labels[7], sizeof(_labels[7]), "Load default data");
  _subs[7][0] = 0;

  snprintf(_labels[8], sizeof(_labels[8]), "Write content");
  _subs[8][0] = 0;

  snprintf(_labels[9], sizeof(_labels[9]), "View content");
  _subs[9][0] = 0;

  snprintf(_labels[10], sizeof(_labels[10]), "Delete HF / LF");
  _subs[10][0] = 0;

  snprintf(_labels[11], sizeof(_labels[11]), "Save nicks");
  _subs[11][0] = 0;

  for (int i = 0; i < kCount; i++) {
    _items[i].label    = _labels[i];
    _items[i].sublabel = _subs[i][0] ? _subs[i] : nullptr;
  }
}

void ChameleonSlotEditScreen::onInit() {
  _load();
  setItems(_items);

  int n = Achievement.inc("chameleon_slot_edit");
  if (n == 1) Achievement.unlock("chameleon_slot_edit");
}

void ChameleonSlotEditScreen::onBack() {
  Screen.goBack();
}

void ChameleonSlotEditScreen::_setActive() {
  if (ChameleonClient::get().setActiveSlot(_slot)) {
    _isActive = true;

    int n = Achievement.inc("chameleon_slot_changed");
    if (n == 1) Achievement.unlock("chameleon_slot_changed");
    if (n == 5) Achievement.unlock("chameleon_slot_changed_5");
  }
  _rebuildLabels();
  render();
}

void ChameleonSlotEditScreen::_editType(bool lf) {
  static const InputSelectAction::Option hfOpts[] = {
    {"MF Classic Mini", "1000"},
    {"MF Classic 1K",   "1001"},
    {"MF Classic 2K",   "1002"},
    {"MF Classic 4K",   "1003"},
    {"NTAG210",         "1107"},
    {"NTAG212",         "1108"},
    {"NTAG213",         "1100"},
    {"NTAG215",         "1101"},
    {"NTAG216",         "1102"},
    {"UltraLight",      "1103"},
    {"Empty",           "0"},
  };
  static const InputSelectAction::Option lfOpts[] = {
    {"EM4100",   "100"},
    {"HID Prox", "200"},
    {"Empty",    "0"},
  };
  uint16_t cur = lf ? _lfType : _hfType;
  char def[8];
  snprintf(def, sizeof(def), "%u", cur);

  const char* r = lf
    ? InputSelectAction::popup("LF type", lfOpts, 3, def)
    : InputSelectAction::popup("HF type", hfOpts, 11, def);

  if (!r) { render(); return; }
  uint16_t v = (uint16_t)strtoul(r, nullptr, 10);

  if (ChameleonClient::get().setSlotTagType(_slot, v)) {
    if (lf) _lfType = v; else _hfType = v;
  } else {
    ShowStatusAction::show("Set type failed", 1200);
  }
  _rebuildLabels();
  render();
}

void ChameleonSlotEditScreen::_toggleEnable(bool lf) {
  bool next = !(lf ? _lfEnabled : _hfEnabled);
  uint8_t freq = lf ? 1 : 2;
  if (ChameleonClient::get().setSlotEnable(_slot, freq, next)) {
    if (lf) _lfEnabled = next; else _hfEnabled = next;
  }
  _rebuildLabels();
  render();
}

void ChameleonSlotEditScreen::_editNick(bool lf) {
  String cur = lf ? _lfNick : _hfNick;
  String r = InputTextAction::popup(lf ? "LF nick" : "HF nick", cur);
  if (r.length() == 0) { render(); return; }
  uint8_t freq = lf ? 1 : 2;
  if (ChameleonClient::get().setSlotNick(_slot, freq, r.c_str())) {
    strncpy(lf ? _lfNick : _hfNick, r.c_str(),
            (lf ? sizeof(_lfNick) : sizeof(_hfNick)) - 1);
    int n = Achievement.inc("chameleon_nick_set");
    if (n == 1) Achievement.unlock("chameleon_nick_set");
  } else {
    ShowStatusAction::show("Set nick failed", 1200);
  }
  _rebuildLabels();
  render();
}

void ChameleonSlotEditScreen::_loadDefault() {
  static const InputSelectAction::Option opts[] = {
    {"HF default", "hf"},
    {"LF default", "lf"},
  };
  const char* r = InputSelectAction::popup("Load default", opts, 2, nullptr);
  if (!r) { render(); return; }
  bool lf = (strcmp(r, "lf") == 0);
  uint16_t t = lf ? _lfType : _hfType;
  if (t == 0) { ShowStatusAction::show("Set type first", 1200); render(); return; }
  bool ok = ChameleonClient::get().setSlotDataDefault(_slot, t);
  ShowStatusAction::show(ok ? "Default loaded" : "Load failed", 1200);
  render();
}

void ChameleonSlotEditScreen::_deleteSlot(bool) {
  static const InputSelectAction::Option opts[] = {
    {"Delete HF", "hf"},
    {"Delete LF", "lf"},
  };
  const char* r = InputSelectAction::popup("Delete", opts, 2, nullptr);
  if (!r) { render(); return; }
  bool lf = (strcmp(r, "lf") == 0);
  uint8_t freq = lf ? 1 : 2;
  bool ok = ChameleonClient::get().deleteSlot(_slot, freq);
  ShowStatusAction::show(ok ? "Deleted" : "Delete failed", 1200);
  if (ok) {
    if (lf) { _lfType = 0; _lfEnabled = false; _lfNick[0] = 0; }
    else    { _hfType = 0; _hfEnabled = false; _hfNick[0] = 0; }
  }
  _rebuildLabels();
  render();
}

void ChameleonSlotEditScreen::_saveNicks() {
  bool ok = ChameleonClient::get().saveSlotNicks();
  ShowStatusAction::show(ok ? "Nicks saved" : "Save failed", 1200);
  render();
}

// ── Load content from SD / manual input ─────────────────────────────────────

static uint16_t _hfTypeForSize(uint32_t size) {
  if (size >= 4096) return 1003; // MF Classic 4K
  if (size >= 2048) return 1002; // MF Classic 2K
  if (size >= 1024) return 1001; // MF Classic 1K
  if (size >= 320)  return 1000; // MF Classic Mini
  return 0;
}

bool ChameleonSlotEditScreen::_writeHfFromBin(const char* path) {
  if (!Uni.Storage) return false;
  fs::File f = Uni.Storage->open(path, "r");
  if (!f) return false;

  uint32_t size = f.size();
  uint16_t tagType = _hfTypeForSize(size);
  if (tagType == 0) { f.close(); return false; }

  auto& c = ChameleonClient::get();
  if (!c.setSlotTagType(_slot, tagType)) { f.close(); return false; }

  // Block 0 holds UID / BCC / SAK / ATQA for anti-collision.
  uint8_t block0[16] = {};
  if (f.read(block0, 16) != 16) { f.close(); return false; }
  uint8_t uidLen = 4;
  uint8_t acoPayload[11] = {};
  acoPayload[0] = uidLen;
  memcpy(acoPayload + 1, block0, uidLen);
  acoPayload[1 + uidLen] = block0[7];  // ATQA reversed for anti-coll payload
  acoPayload[2 + uidLen] = block0[6];
  acoPayload[3 + uidLen] = block0[5];  // SAK
  uint16_t st = 0;
  c.sendCommand(ChameleonClient::CMD_MF1_SET_ANTI_COLL,
                acoPayload, 4 + uidLen, nullptr, nullptr, &st);

  // Stream blocks to the slot 8 blocks (128 B) at a time.
  f.seek(0);
  uint8_t buf[128];
  uint8_t startBlock = 0;
  while (f.available()) {
    int n = f.read(buf, sizeof(buf));
    if (n <= 0) break;
    if (!c.mf1LoadBlockData(_slot, startBlock, buf, (uint16_t)n)) { f.close(); return false; }
    startBlock += n / 16;
  }
  f.close();

  c.setSlotEnable(_slot, 2, true);    // HF = freq 2
  _hfType = tagType;
  _hfEnabled = true;
  return true;
}

static bool _parseHex(const String& in, uint8_t* out, uint8_t expectedLen) {
  String s = in;
  s.replace(":", ""); s.replace(" ", ""); s.trim();
  if (s.length() != (uint32_t)expectedLen * 2) return false;
  for (uint8_t i = 0; i < expectedLen; i++) {
    char hex[3] = { s[i * 2], s[i * 2 + 1], 0 };
    char* end = nullptr;
    unsigned long v = strtoul(hex, &end, 16);
    if (*end != 0) return false;
    out[i] = (uint8_t)v;
  }
  return true;
}

bool ChameleonSlotEditScreen::_writeLfFromHex(const char* hex) {
  uint8_t uid[5];
  if (!_parseHex(hex, uid, 5)) return false;

  auto& c = ChameleonClient::get();
  if (!c.setSlotTagType(_slot, 100)) return false;   // EM4100
  if (!c.setEM410XSlot(uid))         return false;
  c.setSlotEnable(_slot, 1, true);                   // LF = freq 1
  _lfType    = 100;
  _lfEnabled = true;
  return true;
}

void ChameleonSlotEditScreen::_viewContent() {
  static const InputSelectAction::Option opts[] = {
    {"HF content", "hf"},
    {"LF content", "lf"},
  };
  const char* r = InputSelectAction::popup("View which?", opts, 2, nullptr);
  if (!r) { render(); return; }
  bool lf = (strcmp(r, "lf") == 0);
  Screen.push(new ChameleonSlotViewScreen(_slot, lf));
}

void ChameleonSlotEditScreen::_writeContent() {
  static const InputSelectAction::Option freqOpts[] = {
    {"HF from .bin",  "hf"},
    {"LF EM410X UID", "lf"},
  };
  const char* f = InputSelectAction::popup("Load source", freqOpts, 2, nullptr);
  if (!f) { render(); return; }

  if (strcmp(f, "hf") == 0) {
    // Pick a .bin file from the dumps dir.
    static constexpr uint8_t kMax = 10;
    IStorage::DirEntry entries[kMax];
    String names[kMax];
    uint8_t count = 0;
    if (Uni.Storage && Uni.Storage->isAvailable()) {
      uint8_t total = Uni.Storage->listDir("/unigeek/nfc/dumps", entries, kMax);
      for (uint8_t i = 0; i < total && count < kMax; i++) {
        if (!entries[i].isDir && entries[i].name.endsWith(".bin")) {
          names[count++] = entries[i].name;
        }
      }
    }
    if (count == 0) {
      ShowStatusAction::show("No .bin in nfc/dumps", 1500);
      render();
      return;
    }
    InputSelectAction::Option opts[kMax];
    String vals[kMax];
    for (uint8_t i = 0; i < count; i++) {
      vals[i] = String(i);
      opts[i] = { names[i].c_str(), vals[i].c_str() };
    }
    const char* r = InputSelectAction::popup("HF dump", opts, count, nullptr);
    if (!r) { render(); return; }
    uint8_t idx = (uint8_t)atoi(r);
    String path = String("/unigeek/nfc/dumps/") + names[idx];
    bool ok = _writeHfFromBin(path.c_str());
    ShowStatusAction::show(ok ? "HF loaded to slot" : "HF load failed", 1500);
    if (ok) {
      int n = Achievement.inc("chameleon_slot_loaded");
      if (n == 1) Achievement.unlock("chameleon_slot_loaded");
    }
  } else {
    String hex = InputTextAction::popup("EM410X UID (10 hex)");
    if (hex.length() == 0) { render(); return; }
    bool ok = _writeLfFromHex(hex.c_str());
    ShowStatusAction::show(ok ? "LF loaded to slot" : "LF load failed", 1500);
    if (ok) {
      int n = Achievement.inc("chameleon_slot_loaded");
      if (n == 1) Achievement.unlock("chameleon_slot_loaded");
    }
  }
  _rebuildLabels();
  render();
}

void ChameleonSlotEditScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0:  _setActive();          break;
    case 1:  _editType(false);      break;
    case 2:  _editType(true);       break;
    case 3:  _toggleEnable(false);  break;
    case 4:  _toggleEnable(true);   break;
    case 5:  _editNick(false);      break;
    case 6:  _editNick(true);       break;
    case 7:  _loadDefault();        break;
    case 8:  _writeContent();       break;
    case 9:  _viewContent();        break;
    case 10: _deleteSlot(false);    break;
    case 11: _saveNicks();          break;
  }
}
