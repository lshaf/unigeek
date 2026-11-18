#include "ui/actions/ConfirmAction.h"
#include "ChameleonMfcWriteScreen.h"
#include "ChameleonMfuWriteScreen.h"
#include "ChameleonSlotContentScreen.h"
#include "ChameleonSlotEditScreen.h"
#include "ChameleonSlotViewScreen.h"
#include "ChameleonSlotsScreen.h"
#include "ChameleonT5577WriteScreen.h"
#include "core/AchievementManager.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"
#include "utils/ble/ChameleonClient.h"
#include "utils/rfid/LFCodec.h"

void ChameleonSlotEditScreen::_load() {
  auto& c = ChameleonClient::get();

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
  snprintf(_labels[0], sizeof(_labels[0]), "Set Active");
  snprintf(_subs[0], sizeof(_subs[0]), "%s", _isActive ? "[*]" : "-");

  snprintf(_labels[1], sizeof(_labels[1]), "HF Type");
  snprintf(_subs[1], sizeof(_subs[1]), "%s", ChameleonClient::tagTypeName(_hfType));
  snprintf(_labels[2], sizeof(_labels[2]), "LF Type");
  snprintf(_subs[2], sizeof(_subs[2]), "%s", ChameleonClient::tagTypeName(_lfType));
  snprintf(_labels[3], sizeof(_labels[3]), "HF Enable");
  snprintf(_subs[3], sizeof(_subs[3]), "%s", _hfEnabled ? "On" : "Off");
  snprintf(_labels[4], sizeof(_labels[4]), "LF Enable");
  snprintf(_subs[4], sizeof(_subs[4]), "%s", _lfEnabled ? "On" : "Off");

  snprintf(_labels[5], sizeof(_labels[5]), "HF Nickname");
  snprintf(_subs[5], sizeof(_subs[5]), "%s", _hfNick[0] ? _hfNick : "-");
  snprintf(_labels[6], sizeof(_labels[6]), "LF Nickname");
  snprintf(_subs[6], sizeof(_subs[6]), "%s", _lfNick[0] ? _lfNick : "-");
  snprintf(_labels[7], sizeof(_labels[7]), "Save Nicks");
  _subs[7][0] = 0;

  snprintf(_labels[8], sizeof(_labels[8]), "Tag Details");
  _subs[8][0] = 0;
  snprintf(_labels[9], sizeof(_labels[9]), "Raw Data");
  _subs[9][0] = 0;
  snprintf(_labels[10], sizeof(_labels[10]), "Load File to Slot");
  _subs[10][0] = 0;
  snprintf(_labels[11], sizeof(_labels[11]), "Save Slot to File");
  _subs[11][0] = 0;
  snprintf(_labels[12], sizeof(_labels[12]), "Write to Tag");
  _subs[12][0] = 0;
  snprintf(_labels[13], sizeof(_labels[13]), "Reset Slot");
  _subs[13][0] = 0;
  snprintf(_labels[14], sizeof(_labels[14]), "Delete Slot Data");
  _subs[14][0] = 0;

  for (int i = 0; i < kCount; i++) {
    _items[i].label = _labels[i];
    _items[i].sublabel = _subs[i][0] ? _subs[i] : nullptr;
  }
}

void ChameleonSlotEditScreen::onInit() {
  snprintf(_title, sizeof(_title), "Slot %d", _slot + 1);
  render();
  ShowStatusAction::show("Loading...", 0);
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
  InputSelectAction::Option lfOpts[8] = {};
  char lfValues[8][8] = {};
  uint8_t lfCount = 0;
  for (size_t i = 0; i < LFCodec::formatCount() && lfCount < 7; ++i) {
    const LFCodec::FormatInfo* info = LFCodec::formatAt(i);
    if (!info) continue;
    snprintf(lfValues[lfCount], sizeof(lfValues[lfCount]), "%u", info->chameleonType);
    lfOpts[lfCount] = {info->name, lfValues[lfCount]};
    ++lfCount;
  }
  snprintf(lfValues[lfCount], sizeof(lfValues[lfCount]), "0");
  lfOpts[lfCount] = {"Empty", lfValues[lfCount]};
  ++lfCount;
  uint16_t cur = lf ? _lfType : _hfType;
  char def[8];
  snprintf(def, sizeof(def), "%u", cur);

  const char* r = lf
    ? InputSelectAction::popup("LF type", lfOpts, lfCount, def)
    : InputSelectAction::popup("HF type", hfOpts, 11, def);

  if (!r) { render(); return; }
  uint16_t v = (uint16_t)strtoul(r, nullptr, 10);

  bool ok = ChameleonClient::get().setSlotTagType(_slot, v);
  if (ok) {
    if (lf) _lfType = v; else _hfType = v;
  }
  _rebuildLabels();
  render();
  if (!ok) {
    ShowStatusAction::show("Failed", 1200);
    render();
  }
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
  if (InputTextAction::wasCancelled() || r.length() == 0) { render(); return; }
  uint8_t freq = lf ? 1 : 2;
  bool ok = ChameleonClient::get().setSlotNick(_slot, freq, r.c_str());
  if (ok) {
    strncpy(lf ? _lfNick : _hfNick, r.c_str(),
            (lf ? sizeof(_lfNick) : sizeof(_hfNick)) - 1);
    int n = Achievement.inc("chameleon_nick_set");
    if (n == 1) Achievement.unlock("chameleon_nick_set");
  }
  _rebuildLabels();
  render();
  if (!ok) {
    ShowStatusAction::show("Failed", 1200);
    render();
  }
}

void ChameleonSlotEditScreen::_loadDefault() {
  static const InputSelectAction::Option opts[] = {
    {"HF", "hf"},
    {"LF", "lf"},
  };
  const char* r = InputSelectAction::popup("Reset Slot", opts, 2, nullptr);
  if (!r) { render(); return; }
  bool lf = (strcmp(r, "lf") == 0);
  uint16_t t = lf ? _lfType : _hfType;
  if (t == 0) {
    render();
    ShowStatusAction::show("Set type first", 1200);
    render();
    return;
  }
  render();
  if (!ConfirmAction::popup("Reset selected slot?", "This action cannot be undone")) { render(); return; }
  render();
  bool ok = ChameleonClient::get().setSlotDataDefault(_slot, t);
  render();
  ShowStatusAction::show(ok ? "Data reset" : "Failed", 1200);
  render();
}

void ChameleonSlotEditScreen::_deleteSlot(bool) {
  static const InputSelectAction::Option opts[] = {
    {"HF", "hf"},
    {"LF", "lf"},
  };
  const char* r = InputSelectAction::popup("Delete Slot Data", opts, 2, nullptr);
  if (!r) { render(); return; }
  bool lf = (strcmp(r, "lf") == 0);
  render();
  if (!ConfirmAction::popup("Delete slot data?", "This action cannot be undone")) { render(); return; }
  render();
  uint8_t freq = lf ? 1 : 2;
  bool ok = ChameleonClient::get().deleteSlot(_slot, freq);
  if (ok) {
    if (lf) { _lfType = 0; _lfEnabled = false; _lfNick[0] = 0; }
    else    { _hfType = 0; _hfEnabled = false; _hfNick[0] = 0; }
  }
  _rebuildLabels();
  render();
  ShowStatusAction::show(ok ? "Deleted" : "Failed", 1200);
  render();
}

void ChameleonSlotEditScreen::_saveNicks() {
  bool ok = ChameleonClient::get().saveSlotNicks();
  render();
  ShowStatusAction::show(ok ? "Saved" : "Failed", 1200);
  render();
}

// ── Load content from SD / manual input ─────────────────────────────────────

static uint16_t _hfTypeForSize(uint32_t size) {
  // Raw NTAG21x dumps: exact page-image sizes.
  if (size == 80)  return 1107; // NTAG210
  if (size == 164) return 1108; // NTAG212
  if (size == 180) return 1100; // NTAG213
  if (size == 540) return 1101; // NTAG215
  if (size == 924) return 1102; // NTAG216

  if (size == 4096) return 1003; // MF Classic 4K
  if (size == 2048) return 1002; // MF Classic 2K
  if (size == 1024) return 1001; // MF Classic 1K
  if (size == 320)  return 1000; // MF Classic Mini
  return 0;
}

static bool _isMfClassicType(uint16_t type) {
  return type >= 1000 && type <= 1003;
}

static bool _isNtag21xType(uint16_t type) {
  return type == 1107 || type == 1108 || type == 1100 ||
         type == 1101 || type == 1102;
}

static uint16_t _dumpSizeForType(uint16_t type) {
  switch (type) {
    case 1000: return 320;  // MF Classic Mini
    case 1001: return 1024; // MF Classic 1K
    case 1002: return 2048; // MF Classic 2K
    case 1003: return 4096; // MF Classic 4K
    case 1107: return 80;   // NTAG210
    case 1108: return 164;  // NTAG212
    case 1100: return 180;  // NTAG213
    case 1101: return 540;  // NTAG215
    case 1102: return 924;  // NTAG216
    default:   return 0;
  }
}

static String _sanitizeDownloadName(String name) {
  name.trim();
  if (name.endsWith(".bin")) name.remove(name.length() - 4);

  for (int i = 0; i < (int)name.length(); ++i) {
    const char ch = name[i];
    const bool ok =
        (ch >= 'a' && ch <= 'z') ||
        (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') ||
        ch == '-' || ch == '_';
    if (!ok) name.setCharAt(i, '_');
  }

  while (name.indexOf("__") >= 0) name.replace("__", "_");
  while (name.startsWith("_")) name.remove(0, 1);
  while (name.endsWith("_")) name.remove(name.length() - 1);
  if (name.length() == 0) name = "slot_dump";
  return name;
}

bool ChameleonSlotEditScreen::_writeHfFromBin(const char* path) {
  if (!Uni.Storage) return false;
  fs::File f = Uni.Storage->open(path, "r");
  if (!f) return false;

  const uint32_t size = f.size();
  const uint16_t tagType = _hfTypeForSize(size);
  if (tagType == 0) { f.close(); return false; }

  auto& c = ChameleonClient::get();

  uint8_t previousSlot = 0;
  uint8_t previousMode = 0;
  const bool restoreSlot =
      c.getActiveSlot(&previousSlot) && previousSlot != _slot;
  const bool restoreMode = c.getMode(&previousMode);

  auto restoreContext = [&]() {
    if (restoreSlot) c.setActiveSlot(previousSlot);
    if (restoreMode) c.setMode(previousMode);
  };

  if (!c.setSlotTagType(_slot, tagType) ||
      !c.setSlotDataDefault(_slot, tagType) ||
      !c.setActiveSlot(_slot)) {
    f.close();
    restoreContext();
    return false;
  }

  if (_isMfClassicType(tagType)) {
    // Raw Classic block 0 carries UID/SAK/ATQA using one of the standard
    // 4-byte or 7-byte UID layouts. Do not assume a 4-byte UID: .bin files
    // can come from the PN532, CU, or external tools.
    uint8_t block0[16] = {};
    if (f.read(block0, 16) != 16) { f.close(); restoreContext(); return false; }

    uint8_t uid[7] = {};
    uint8_t uidLen = 0;
    uint8_t sak = 0;
    uint8_t atqa0 = 0, atqa1 = 0;
    const uint8_t bcc4 = block0[0] ^ block0[1] ^ block0[2] ^ block0[3];
    if (block0[4] == bcc4 && (block0[6] & 0xC0) == 0x00) {
      uidLen = 4;
      memcpy(uid, block0, 4);
      sak = block0[5];
      atqa0 = block0[6];
      atqa1 = block0[7];
    } else if ((block0[8] & 0xC0) == 0x40) {
      uidLen = 7;
      memcpy(uid, block0, 7);
      sak = block0[7];
      atqa0 = block0[8];
      atqa1 = block0[9];
    } else {
      f.close();
      restoreContext();
      return false;
    }

    uint8_t acoPayload[12] = {};
    acoPayload[0] = uidLen;
    memcpy(acoPayload + 1, uid, uidLen);
    acoPayload[1 + uidLen] = atqa0;
    acoPayload[2 + uidLen] = atqa1;
    acoPayload[3 + uidLen] = sak;
    acoPayload[4 + uidLen] = 0;
    uint16_t st = 0;
    if (!c.sendCommand(ChameleonClient::CMD_MF1_SET_ANTI_COLL,
                       acoPayload, 5 + uidLen, nullptr, nullptr, &st) ||
        (st != 0 && st != 0x68)) {
      f.close();
      restoreContext();
      return false;
    }

    f.seek(0);
    uint8_t buf[128];
    uint8_t startBlock = 0;
    uint32_t loaded = 0;
    const uint16_t totalBlocks = (uint16_t)(size / 16);

    ProgressView::init();
    while (f.available()) {
      int n = f.read(buf, sizeof(buf));
      if (n <= 0) break;

      const uint16_t loadedBlocks = (uint16_t)(loaded / 16);
      char msg[48];
      snprintf(msg, sizeof(msg), "Loading dump into slot %u: block %u/%u",
               _slot + 1, (unsigned)loadedBlocks, (unsigned)totalBlocks);
      const int pct = (size > 0) ? (int)((loaded * 100UL) / size) : 0;
      ProgressView::progress(msg, pct);

      if (!c.mf1LoadBlockData(_slot, startBlock, buf, (uint16_t)n)) {
        f.close();
        ProgressView::finish();
        restoreContext();
        return false;
      }
      loaded += (uint32_t)n;
      startBlock += n / 16;
    }
    f.close();
    ProgressView::finish();
  } else if (_isNtag21xType(tagType)) {
    // Standard raw NTAG21x dump, 4 bytes per page.
    // UID is encoded across pages 0 and 1:
    //   page 0: UID0 UID1 UID2 BCC0
    //   page 1: UID3 UID4 UID5 UID6
    uint8_t firstPages[12] = {};
    if (f.read(firstPages, sizeof(firstPages)) != (int)sizeof(firstPages)) {
      f.close();
      restoreContext();
      return false;
    }

    const uint8_t uid[7] = {
      firstPages[0], firstPages[1], firstPages[2],
      firstPages[4], firstPages[5], firstPages[6], firstPages[7]
    };

    // Type-2 / NTAG21x anti-collision values: 7-byte UID, ATQA 0x0044,
    // SAK 0x00, no ATS. The protocol expects ATQA as the two wire bytes.
    uint8_t acoPayload[12] = {};
    acoPayload[0] = 7;
    memcpy(acoPayload + 1, uid, 7);
    acoPayload[8]  = 0x44;
    acoPayload[9]  = 0x00;
    acoPayload[10] = 0x00;
    acoPayload[11] = 0x00;
    uint16_t st = 0;
    if (!c.sendCommand(ChameleonClient::CMD_MF1_SET_ANTI_COLL,
                       acoPayload, sizeof(acoPayload), nullptr, nullptr, &st) ||
        (st != 0 && st != 0x68)) {
      f.close();
      restoreContext();
      return false;
    }

    f.seek(0);
    static constexpr uint8_t kPagesPerChunk = 32; // 128 data bytes / BLE command
    uint8_t buf[kPagesPerChunk * 4];
    uint8_t firstPage = 0;
    uint16_t loadedPages = 0;
    const uint16_t totalPages = (uint16_t)(size / 4);

    ProgressView::init();
    while (loadedPages < totalPages) {
      const uint8_t pageCount = (uint8_t)min((uint16_t)kPagesPerChunk,
                                             (uint16_t)(totalPages - loadedPages));
      const uint16_t want = (uint16_t)pageCount * 4;
      const int n = f.read(buf, want);
      if (n != want) {
        f.close();
        ProgressView::finish();
        restoreContext();
        return false;
      }

      char msg[48];
      snprintf(msg, sizeof(msg), "Loading dump into slot %u: page %u/%u",
               _slot + 1, (unsigned)loadedPages, (unsigned)totalPages);
      const int pct = (int)((loadedPages * 100UL) / totalPages);
      ProgressView::progress(msg, pct);

      if (!c.mfuLoadPageData(_slot, firstPage, buf, pageCount)) {
        f.close();
        ProgressView::finish();
        restoreContext();
        return false;
      }
      loadedPages += pageCount;
      firstPage += pageCount;
    }
    f.close();
    ProgressView::progress("Dump loaded", 100);
    ProgressView::finish();
  } else {
    f.close();
    restoreContext();
    return false;
  }

  if (!c.setSlotEnable(_slot, 2, true)) {
    restoreContext();
    return false;
  }
  if (!c.setMode(0)) {
    restoreContext();
    return false;
  }

  _hfType = tagType;
  _hfEnabled = true;
  restoreContext();
  return true;
}
bool ChameleonSlotEditScreen::_writeLfFromBin(const char* path) {
  if (!path || !Uni.Storage) return false;

  const LFCodec::FormatInfo* info = LFCodec::fromFilename(path);
  if (!info) return false;
  const uint16_t tagType = info->chameleonType;
  const uint8_t expected = info->dataSize;

  fs::File f = Uni.Storage->open(path, "r");
  if (!f || f.size() != expected) { if (f) f.close(); return false; }
  uint8_t data[16] = {};
  const int n = f.read(data, expected);
  f.close();
  if (n != expected) return false;

  auto& c = ChameleonClient::get();
  uint8_t previousSlot = 0, previousMode = 0;
  const bool restoreSlot = c.getActiveSlot(&previousSlot) && previousSlot != _slot;
  const bool restoreMode = c.getMode(&previousMode);
  auto restoreContext = [&]() {
    if (restoreSlot) c.setActiveSlot(previousSlot);
    if (restoreMode) c.setMode(previousMode);
  };

  bool ok = c.setSlotTagType(_slot, tagType) && c.setActiveSlot(_slot);
  if (ok) {
    if (tagType == 100) ok = c.setEM410XSlot(data);
    else if (tagType == 200) ok = c.setHIDProxSlot(data, expected);
    else if (tagType == 201) ok = c.setIoProxSlot(data);
    else if (tagType == 170) ok = c.setVikingSlot(data, expected);
    else if (tagType == 150) ok = c.setPACSlot(data);
    else if (tagType == 180) ok = c.setJablotronSlot(data);
    else ok = false;
  }
  if (ok) ok = c.setSlotEnable(_slot, 1, true) && c.setMode(0);

  if (ok) {
    _lfType = tagType;
    _lfEnabled = true;
  }
  restoreContext();
  return ok;
}

void ChameleonSlotEditScreen::_viewContent() {
  static const InputSelectAction::Option opts[] = {
    {"HF", "hf"},
    {"LF", "lf"},
  };
  const char* r = InputSelectAction::popup("Tag Details", opts, 2, nullptr);
  if (!r) { render(); return; }
  const bool lf = strcmp(r, "lf") == 0;
  Screen.push(new ChameleonSlotContentScreen(_slot, lf));
}

void ChameleonSlotEditScreen::_viewData() {
  static const InputSelectAction::Option opts[] = {
    {"HF", "hf"},
    {"LF", "lf"},
  };
  const char* r = InputSelectAction::popup("Raw Data", opts, 2, nullptr);
  if (!r) { render(); return; }
  bool lf = (strcmp(r, "lf") == 0);
  Screen.push(new ChameleonSlotViewScreen(_slot, lf));
}

static String _hexBytes(const uint8_t* data, uint8_t len) {
  String out;
  char h[3];
  for (uint8_t i = 0; i < len; ++i) {
    snprintf(h, sizeof(h), "%02X", data[i]);
    out += h;
  }
  return out;
}

bool ChameleonSlotEditScreen::_saveLfSlotToFile() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) return false;

  auto& c = ChameleonClient::get();
  uint8_t previousSlot = 0;
  const bool restoreSlot = c.getActiveSlot(&previousSlot) && previousSlot != _slot;
  if (!c.setActiveSlot(_slot)) return false;

  uint8_t data[16] = {};
  uint8_t len = 0;
  bool ok = false;
  const LFCodec::FormatInfo* info = LFCodec::fromChameleonType(_lfType);
  String typeName = info ? info->filePrefix : "LF";
  if (info) {
    switch (info->protocol) {
      case LFCodec::Protocol::EM410X:     len = 5; ok = c.getEM410XSlot(data); break;
      case LFCodec::Protocol::HIDProx:    ok = c.getHIDProxSlot(data, &len); break;
      case LFCodec::Protocol::IoProx:     ok = c.getIoProxSlot(data, &len); break;
      case LFCodec::Protocol::Viking:     ok = c.getVikingSlot(data, &len); break;
      case LFCodec::Protocol::PACStanley: ok = c.getPACSlot(data, &len); break;
      case LFCodec::Protocol::Jablotron:  ok = c.getJablotronSlot(data, &len); break;
      default: break;
    }
    ok = ok && LFCodec::validate(info->protocol, len);
  }

  if (restoreSlot) c.setActiveSlot(previousSlot);
  if (!ok || !len) return false;

  const String suggested = typeName + "_" + _hexBytes(data, len);
  String name = InputTextAction::popup("File name", suggested);
  if (InputTextAction::wasCancelled()) return true;

  Uni.Storage->makeDir("/unigeek/rfid");
  Uni.Storage->makeDir("/unigeek/rfid/data");
  const String base = _sanitizeDownloadName(name);
  String path = String("/unigeek/rfid/data/") + base + ".bin";
  if (Uni.Storage->exists(path.c_str())) {
    for (int n = 2; n < 1000; ++n) {
      String candidate = String("/unigeek/rfid/data/") + base + "_(" + n + ").bin";
      if (!Uni.Storage->exists(candidate.c_str())) { path = candidate; break; }
    }
  }

  fs::File f = Uni.Storage->open(path.c_str(), "w");
  if (!f) return false;
  const size_t written = f.write(data, len);
  f.close();
  if (written != len) return false;

  const int slash = path.lastIndexOf('/');
  const String saved = slash >= 0 ? path.substring(slash + 1) : path;
  render();
  ShowStatusAction::show(("Saved: " + saved).c_str(), 1600);
  render();
  return true;
}

void ChameleonSlotEditScreen::_downloadDump() {
  static const InputSelectAction::Option opts[] = {
    {"HF", "hf"},
    {"LF", "lf"},
  };
  const char* selected = InputSelectAction::popup("Save Slot to File", opts, 2, nullptr);
  if (!selected) { render(); return; }
  if (strcmp(selected, "lf") == 0) {
    render();
    if (!_saveLfSlotToFile()) {
      ShowStatusAction::show("Failed", 1600);
      render();
    }
    return;
  }
  const uint16_t dumpSize = _dumpSizeForType(_hfType);
  if (dumpSize == 0) {
    render();
    ShowStatusAction::show("Tag not supported", 1600);
    render();
    return;
  }

  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    render();
    ShowStatusAction::show("Storage unavailable", 1600);
    render();
    return;
  }

  auto& c = ChameleonClient::get();

  uint8_t previousSlot = 0;
  const bool restoreSlot =
      c.getActiveSlot(&previousSlot) && previousSlot != _slot;

  if (!c.setActiveSlot(_slot)) {
    render();
    ShowStatusAction::show("Failed", 1600);
    render();
    return;
  }

  uint8_t* dump = (uint8_t*)malloc(dumpSize);
  if (!dump) {
    if (restoreSlot) c.setActiveSlot(previousSlot);
    render();
    ShowStatusAction::show("Out of memory", 1600);
    render();
    return;
  }

  memset(dump, 0, dumpSize);
  bool ok = true;

  // Clear the body before ProgressView so no previous overlay is left behind.
  Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  ProgressView::init();

  if (_isMfClassicType(_hfType)) {
    const uint16_t totalBlocks = dumpSize / 16u;
    uint16_t doneBlocks = 0;

    while (doneBlocks < totalBlocks) {
      const uint8_t count =
          (uint8_t)min((uint16_t)8, (uint16_t)(totalBlocks - doneBlocks));
      const uint16_t want = (uint16_t)count * 16u;
      uint16_t st = 0, len = 0;

      char msg[40];
      snprintf(msg, sizeof(msg), "Downloading %u/%u blocks",
               (unsigned)doneBlocks, (unsigned)totalBlocks);
      ProgressView::progress(
          msg, (int)((uint32_t)doneBlocks * 100u / totalBlocks));

      if (!c.mf1GetBlockData((uint8_t)doneBlocks, count,
                             dump + (size_t)doneBlocks * 16u,
                             &st, &len) ||
          len < want) {
        ok = false;
        break;
      }

      doneBlocks += count;
    }

  } else {
    const uint16_t totalPages = dumpSize / 4u;
    uint16_t donePages = 0;

    while (donePages < totalPages) {
      const uint8_t count =
          (uint8_t)min((uint16_t)32, (uint16_t)(totalPages - donePages));
      const uint16_t want = (uint16_t)count * 4u;
      uint16_t st = 0, len = 0;

      char msg[40];
      snprintf(msg, sizeof(msg), "Downloading %u/%u pages",
               (unsigned)donePages, (unsigned)totalPages);
      ProgressView::progress(
          msg, (int)((uint32_t)donePages * 100u / totalPages));

      if (!c.mfuGetPageData((uint8_t)donePages, count,
                            dump + (size_t)donePages * 4u,
                            &st, &len) ||
          len < want) {
        ok = false;
        break;
      }

      donePages += count;
    }
  }

  ProgressView::finish();

  if (restoreSlot) c.setActiveSlot(previousSlot);

  if (!ok) {
    free(dump);
    render();
    ShowStatusAction::show("Failed", 1600);
    render();
    return;
  }

  // Use the same dump basename convention as Read Tag -> Save Dump:
  // <canonical tag type>_<UID>. Keep tagTypeName() spelling exactly
  // (MF-1K, MF-4K, NTAG215, ...).
  String typeName = ChameleonClient::tagTypeName(_hfType);
  String uid;

  if (_isMfClassicType(_hfType) && dumpSize >= 16) {
    const uint8_t bcc = dump[0] ^ dump[1] ^ dump[2] ^ dump[3];
    if (dump[4] == bcc) {
      char uidBuf[9];
      snprintf(uidBuf, sizeof(uidBuf), "%02X%02X%02X%02X",
               dump[0], dump[1], dump[2], dump[3]);
      uid = uidBuf;
    }
  } else if (dumpSize >= 12) {
    // Type-2 manufacturer pages: UID0..2/BCC0, UID3..6, BCC1.
    const uint8_t bcc0 = 0x88 ^ dump[0] ^ dump[1] ^ dump[2];
    const uint8_t bcc1 = dump[4] ^ dump[5] ^ dump[6] ^ dump[7];
    if (dump[3] == bcc0 && dump[8] == bcc1) {
      char uidBuf[15];
      snprintf(uidBuf, sizeof(uidBuf), "%02X%02X%02X%02X%02X%02X%02X",
               dump[0], dump[1], dump[2], dump[4], dump[5], dump[6], dump[7]);
      uid = uidBuf;
    }
  }

  String suggested = uid.length()
      ? typeName + "_" + uid
      : typeName + "_slot_" + String(_slot + 1);

  String name = InputTextAction::popup("File name", suggested);
  if (InputTextAction::wasCancelled()) {
    free(dump);
    render();
    return;
  }

  Uni.Storage->makeDir("/unigeek/nfc");
  Uni.Storage->makeDir("/unigeek/nfc/dumps");

  const String base = _sanitizeDownloadName(name);
  String path = String("/unigeek/nfc/dumps/") + base + ".bin";

  if (Uni.Storage->exists(path.c_str())) {
    for (int n = 2; n < 1000; ++n) {
      String candidate =
          String("/unigeek/nfc/dumps/") + base + "_(" + n + ").bin";
      if (!Uni.Storage->exists(candidate.c_str())) {
        path = candidate;
        break;
      }
    }
  }

  fs::File f = Uni.Storage->open(path.c_str(), "w");
  if (!f) {
    free(dump);
    render();
    ShowStatusAction::show("Failed", 1600);
    render();
    return;
  }

  const size_t written = f.write(dump, dumpSize);
  f.close();
  free(dump);

  render();
  if (written != dumpSize) {
    ShowStatusAction::show("Failed", 1600);
    render();
    return;
  }

  const int slash = path.lastIndexOf('/');
  const String saved = (slash >= 0) ? path.substring(slash + 1) : path;
  ShowStatusAction::show(("Saved: " + saved).c_str(), 1600);
  render();
}

void ChameleonSlotEditScreen::_writeContent() {
  static const InputSelectAction::Option freqOpts[] = {
    {"HF", "hf"},
    {"LF", "lf"},
  };
  const char* f = InputSelectAction::popup("Load File to Slot", freqOpts, 2, nullptr);
  if (!f) { render(); return; }

  const bool lf = strcmp(f, "lf") == 0;
  const char* dir = lf ? "/unigeek/rfid/data" : "/unigeek/nfc/dumps";
  static constexpr uint8_t kMax = 10;
  uint8_t n = _browser.load(this, dir,
      BrowseFileView::Mode(BrowseFileView::Mode::FILE_ONLY, ".bin"));
  if (n == 0) {
    render();
    ShowStatusAction::show(lf ? "No saved LF data" : "No .bin in nfc/dumps", 1600);
    render();
    return;
  }

  const uint8_t count = (n < kMax) ? n : kMax;
  InputSelectAction::Option opts[kMax];
  String vals[kMax];
  for (uint8_t i = 0; i < count; ++i) {
    vals[i] = String(i);
    opts[i] = {_browser.entry(i).name.c_str(), vals[i].c_str()};
  }
  const char* r = InputSelectAction::popup(lf ? "LF Data" : "HF Dump", opts, count, nullptr);
  if (!r) { render(); return; }
  const uint8_t idx = (uint8_t)atoi(r);
  if (idx >= count) { render(); return; }
  const String path = _browser.entry(idx).path;

  render();
  bool ok = lf ? _writeLfFromBin(path.c_str()) : _writeHfFromBin(path.c_str());
  _rebuildLabels();
  render();

  ShowStatusAction::show(ok ? "Loaded to slot" : "Failed", 1600);
  render();

  if (ok) {
    int a = Achievement.inc("chameleon_slot_loaded");
    if (a == 1) Achievement.unlock("chameleon_slot_loaded");
  }
}

void ChameleonSlotEditScreen::_writeTag() {
  static const InputSelectAction::Option opts[] = {
    {"HF", "hf"},
    {"LF", "lf"},
  };
  const char* r = InputSelectAction::popup("Write to Tag", opts, 2, nullptr);
  if (!r) { render(); return; }
  const bool lf = strcmp(r, "lf") == 0;

  if (!lf) {
    if (_hfType == ChameleonClient::MFU_NTAG215) {
      Screen.push(new ChameleonMfuWriteScreen(_slot));
      return;
    }
    if (_hfType == 1001) {
      Screen.push(new ChameleonMfcWriteScreen(_slot));
      return;
    }
    render();
    ShowStatusAction::show("Tag not supported", 1600);
    render();
    return;
  }

  if (LFCodec::isSupportedT5577(_lfType)) {
    Screen.push(new ChameleonT5577WriteScreen(_slot, _lfType));
    return;
  }
  render();
  ShowStatusAction::show("Tag not supported", 1600);
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
    case 7:  _saveNicks();          break;
    case 8:  _viewContent();        break;
    case 9:  _viewData();           break;
    case 10: _writeContent();       break;
    case 11: _downloadDump();       break;
    case 12: _writeTag();           break;
    case 13: _loadDefault();        break;
    case 14: _deleteSlot(false);    break;
  }
}
