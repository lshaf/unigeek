#include "ChameleonSlotEditScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "ChameleonSlotsScreen.h"
#include "ChameleonSlotViewScreen.h"
#include "ChameleonSlotContentScreen.h"
#include "ChameleonMfuWriteScreen.h"
#include "ChameleonMfcWriteScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"

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

  snprintf(_labels[8], sizeof(_labels[8]), "View Content");
  _subs[8][0] = 0;
  snprintf(_labels[9], sizeof(_labels[9]), "View Data");
  _subs[9][0] = 0;
  snprintf(_labels[10], sizeof(_labels[10]), "Load Dump to Slot");
  _subs[10][0] = 0;
  snprintf(_labels[11], sizeof(_labels[11]), "Download Dump from Slot");
  _subs[11][0] = 0;
  snprintf(_labels[12], sizeof(_labels[12]), "Write to Tag");
  _subs[12][0] = 0;
  snprintf(_labels[13], sizeof(_labels[13]), "Reset Slot Data");
  _subs[13][0] = 0;
  snprintf(_labels[14], sizeof(_labels[14]), "Delete Dump from Slot");
  _subs[14][0] = 0;

  for (int i = 0; i < kCount; i++) {
    _items[i].label = _labels[i];
    _items[i].sublabel = _subs[i][0] ? _subs[i] : nullptr;
  }
}

void ChameleonSlotEditScreen::onInit() {
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

  bool ok = ChameleonClient::get().setSlotTagType(_slot, v);
  if (ok) {
    if (lf) _lfType = v; else _hfType = v;
  }
  _rebuildLabels();
  render();
  if (!ok) {
    ShowStatusAction::show("Set type failed", 1200);
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
    ShowStatusAction::show("Set nick failed", 1200);
    render();
  }
}

void ChameleonSlotEditScreen::_loadDefault() {
  static const InputSelectAction::Option opts[] = {
    {"HF Data", "hf"},
    {"LF Data", "lf"},
  };
  const char* r = InputSelectAction::popup("Reset Slot Data", opts, 2, nullptr);
  if (!r) { render(); return; }
  bool lf = (strcmp(r, "lf") == 0);
  uint16_t t = lf ? _lfType : _hfType;
  if (t == 0) {
    render();
    ShowStatusAction::show("Set type first", 1200);
    render();
    return;
  }
  bool ok = ChameleonClient::get().setSlotDataDefault(_slot, t);
  render();
  ShowStatusAction::show(ok ? "Data reset" : "Reset failed", 1200);
  render();
}

void ChameleonSlotEditScreen::_deleteSlot(bool) {
  static const InputSelectAction::Option opts[] = {
    {"Delete HF", "hf"},
    {"Delete LF", "lf"},
  };
  const char* r = InputSelectAction::popup("Delete Dump from Slot", opts, 2, nullptr);
  if (!r) { render(); return; }
  bool lf = (strcmp(r, "lf") == 0);
  uint8_t freq = lf ? 1 : 2;
  bool ok = ChameleonClient::get().deleteSlot(_slot, freq);
  if (ok) {
    if (lf) { _lfType = 0; _lfEnabled = false; _lfNick[0] = 0; }
    else    { _hfType = 0; _hfEnabled = false; _hfNick[0] = 0; }
  }
  _rebuildLabels();
  render();
  ShowStatusAction::show(ok ? "Deleted" : "Delete failed", 1200);
  render();
}

void ChameleonSlotEditScreen::_saveNicks() {
  bool ok = ChameleonClient::get().saveSlotNicks();
  render();
  ShowStatusAction::show(ok ? "Nicks saved" : "Save failed", 1200);
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
    // Existing Classic path: block 0 carries the anti-collision fields used by
    // UniGeek's Classic .bin format.
    uint8_t block0[16] = {};
    if (f.read(block0, 16) != 16) { f.close(); restoreContext(); return false; }
    const uint8_t uidLen = 4;
    uint8_t acoPayload[11] = {};
    acoPayload[0] = uidLen;
    memcpy(acoPayload + 1, block0, uidLen);
    acoPayload[1 + uidLen] = block0[6];
    acoPayload[2 + uidLen] = block0[7];
    acoPayload[3 + uidLen] = block0[5];
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

  uint8_t previousSlot = 0;
  uint8_t previousMode = 0;
  const bool restoreSlot =
      c.getActiveSlot(&previousSlot) && previousSlot != _slot;
  const bool restoreMode = c.getMode(&previousMode);

  auto restoreContext = [&]() {
    if (restoreSlot) c.setActiveSlot(previousSlot);
    if (restoreMode) c.setMode(previousMode);
  };

  if (!c.setSlotTagType(_slot, 100) ||
      !c.setActiveSlot(_slot) ||
      !c.setEM410XSlot(uid) ||
      !c.setSlotEnable(_slot, 1, true) ||
      !c.setMode(0)) {
    restoreContext();
    return false;
  }

  _lfType    = 100;
  _lfEnabled = true;
  restoreContext();
  return true;
}

void ChameleonSlotEditScreen::_viewContent() {
  // First implementation: interpreted HF content for MIFARE Classic slots.
  Screen.push(new ChameleonSlotContentScreen(_slot));
}

void ChameleonSlotEditScreen::_viewData() {
  static const InputSelectAction::Option opts[] = {
    {"HF Data", "hf"},
    {"LF Data", "lf"},
  };
  const char* r = InputSelectAction::popup("View Data", opts, 2, nullptr);
  if (!r) { render(); return; }
  bool lf = (strcmp(r, "lf") == 0);
  Screen.push(new ChameleonSlotViewScreen(_slot, lf));
}


void ChameleonSlotEditScreen::_downloadDump() {
  const uint16_t dumpSize = _dumpSizeForType(_hfType);
  if (dumpSize == 0) {
    render();
    ShowStatusAction::show("Tag type not supported", 1400);
    render();
    return;
  }

  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    render();
    ShowStatusAction::show("Storage unavailable", 1400);
    render();
    return;
  }

  auto& c = ChameleonClient::get();

  uint8_t previousSlot = 0;
  const bool restoreSlot =
      c.getActiveSlot(&previousSlot) && previousSlot != _slot;

  if (!c.setActiveSlot(_slot)) {
    render();
    ShowStatusAction::show("Select slot failed", 1400);
    render();
    return;
  }

  uint8_t* dump = (uint8_t*)malloc(dumpSize);
  if (!dump) {
    if (restoreSlot) c.setActiveSlot(previousSlot);
    render();
    ShowStatusAction::show("Out of memory", 1400);
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

    if (ok)
      ProgressView::progress("Download complete", 100);
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

    if (ok)
      ProgressView::progress("Download complete", 100);
  }

  ProgressView::finish();

  if (restoreSlot) c.setActiveSlot(previousSlot);

  if (!ok) {
    free(dump);
    render();
    ShowStatusAction::show("Download failed", 1500);
    render();
    return;
  }

  String typeName = ChameleonClient::tagTypeName(_hfType);
  typeName.toLowerCase();
  typeName.replace("-", "");
  String suggested = typeName + "_slot_" + String(_slot + 1);

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
    ShowStatusAction::show("Save failed", 1500);
    render();
    return;
  }

  const size_t written = f.write(dump, dumpSize);
  f.close();
  free(dump);

  render();
  if (written != dumpSize) {
    ShowStatusAction::show("Save failed", 1500);
    render();
    return;
  }

  const int slash = path.lastIndexOf('/');
  const String saved = (slash >= 0) ? path.substring(slash + 1) : path;
  ShowStatusAction::show(("Saved: " + saved).c_str(), 1700);
  render();
}

void ChameleonSlotEditScreen::_writeContent() {
  static const InputSelectAction::Option freqOpts[] = {
    {"HF from .bin",  "hf"},
    {"LF EM410X UID", "lf"},
  };
  const char* f = InputSelectAction::popup("Load source", freqOpts, 2, nullptr);
  if (!f) { render(); return; }

  if (strcmp(f, "hf") == 0) {
    // Pick a .bin file from the dumps dir via BrowseFileView (sorted + filtered).
    static constexpr uint8_t kMax = 10;
    uint8_t n = _browser.load(this, "/unigeek/nfc/dumps", ".bin");
    if (n == 0) {
      render();
      ShowStatusAction::show("No .bin in nfc/dumps", 1500);
      render();
      return;
    }
    uint8_t count = (n < kMax) ? n : kMax;
    InputSelectAction::Option opts[kMax];
    String vals[kMax];
    for (uint8_t i = 0; i < count; i++) {
      vals[i] = String(i);
      opts[i] = { _browser.entry(i).name.c_str(), vals[i].c_str() };
    }
    const char* r = InputSelectAction::popup("HF dump", opts, count, nullptr);
    if (!r) { render(); return; }
    uint8_t idx = (uint8_t)atoi(r);
    if (idx >= count) { render(); return; }
    String path = _browser.entry(idx).path;

    // The file picker is an overlay and leaves its cleared region behind.
    // Restore the Slot Edit screen before the blocking BLE load begins.
    render();

    bool ok = _writeHfFromBin(path.c_str());

    _rebuildLabels();
    render();

    ShowStatusAction::show(ok ? "Dump loaded" : "Dump load failed", 1500);
    render();

    if (ok) {
      int n = Achievement.inc("chameleon_slot_loaded");
      if (n == 1) Achievement.unlock("chameleon_slot_loaded");
    }
  } else {
    String hex = InputTextAction::popup("EM410X UID (10 hex)");
    if (InputTextAction::wasCancelled() || hex.length() == 0) { render(); return; }

    // Restore the Slot Edit screen before the blocking BLE write begins.
    render();

    bool ok = _writeLfFromHex(hex.c_str());

    _rebuildLabels();
    render();

    ShowStatusAction::show(ok ? "Dump loaded" : "Dump load failed", 1500);
    render();

    if (ok) {
      int n = Achievement.inc("chameleon_slot_loaded");
      if (n == 1) Achievement.unlock("chameleon_slot_loaded");
    }
  }
}

void ChameleonSlotEditScreen::_writeTag() {
  if (_hfType == ChameleonClient::MFU_NTAG215) {
    Screen.push(new ChameleonMfuWriteScreen(_slot));
    return;
  }
  if (_hfType == 1001) {
    Screen.push(new ChameleonMfcWriteScreen(_slot));
    return;
  }
  render();
  ShowStatusAction::show("Tag type not supported", 1400);
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
