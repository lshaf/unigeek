#include "ChameleonSlotViewScreen.h"
#include "utils/chameleon/ChameleonClient.h"
#include "ChameleonSlotEditScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "ui/actions/ShowStatusAction.h"

void ChameleonSlotViewScreen::_addRow(const char* label, const String& value) {
  if (_rowCount >= MAX_ROWS) return;
  _labels[_rowCount] = label;
  _values[_rowCount] = value;
  _rows[_rowCount]   = { _labels[_rowCount].c_str(), _values[_rowCount] };
  _rowCount++;
}

void ChameleonSlotViewScreen::_runHF() {
  auto& c = ChameleonClient::get();
  c.setActiveSlot(_slot);
  c.setMode(0); // emulator mode so slot memory is accessible

  // Read slot tag types to infer block count
  ChameleonClient::SlotTypes types[8] = {};
  c.getSlotTypes(types);
  uint16_t t = types[_slot].hfType;

  uint16_t total = 64;                 // default 1K
  if (t == 1003)      total = 256;     // 4K
  else if (t == 1002) total = 128;     // 2K
  else if (t == 1000) total = 20;      // Mini
  else if (t == 1001) total = 64;      // 1K
  else if (t == 0)    total = 0;       // empty

  char head[20];
  snprintf(head, sizeof(head), "%s", ChameleonClient::tagTypeName(t));
  _addRow("Type", head);

  if (total == 0) {
    _addRow("Data", "(empty)");
    return;
  }

  uint8_t buf[128];
  uint16_t got = 0;
  while (got < total) {
    uint8_t chunk = (total - got > 8) ? 8 : (uint8_t)(total - got);
    if (!c.mf1GetBlockData((uint8_t)got, chunk, buf)) {
      _addRow("Error", String("read fail @") + got);
      break;
    }
    for (uint8_t b = 0; b < chunk; b++) {
      const uint8_t* p = buf + b * 16;
      char lbl[8];
      snprintf(lbl, sizeof(lbl), "B%03u", got + b);
      // Show as two halves separated so it fits in value column:
      //   AABBCCDDEEFFGGHH  IIJJKKLLMMNNOOPP
      char hex[40];
      snprintf(hex, sizeof(hex),
               "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
               p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],
               p[8],p[9],p[10],p[11],p[12],p[13],p[14],p[15]);
      _addRow(lbl, hex);
    }
    got += chunk;
  }
}

void ChameleonSlotViewScreen::_runLF() {
  auto& c = ChameleonClient::get();
  c.setActiveSlot(_slot);

  ChameleonClient::SlotTypes types[8] = {};
  c.getSlotTypes(types);
  uint16_t t = types[_slot].lfType;

  _addRow("Type", ChameleonClient::tagTypeName(t));

  char hex[40] = {};
  if (t == 100) {
    uint8_t uid[5] = {};
    if (c.getEM410XSlot(uid)) {
      snprintf(hex, sizeof(hex), "%02X:%02X:%02X:%02X:%02X",
               uid[0], uid[1], uid[2], uid[3], uid[4]);
      _addRow("UID", hex);
      uint64_t dec = 0;
      for (int i = 0; i < 5; i++) dec = (dec << 8) | uid[i];
      char ds[24];
      snprintf(ds, sizeof(ds), "%llu", (unsigned long long)dec);
      _addRow("Dec", ds);
    } else {
      _addRow("UID", "(unavailable)");
    }
  } else if (t == 200) {
    uint8_t pl[13] = {}; uint8_t plen = 0;
    if (c.getHIDProxSlot(pl, &plen)) {
      String v;
      for (uint8_t i = 0; i < plen; i++) {
        char h[4]; snprintf(h, sizeof(h), "%02X", pl[i]); v += h;
      }
      _addRow("Payload", v);
    } else {
      _addRow("Payload", "(unavailable)");
    }
  } else if (t == 0) {
    _addRow("UID", "(empty)");
  } else {
    // Viking / unknown — try Viking
    uint8_t uid[4] = {}; uint8_t ulen = 0;
    if (c.getVikingSlot(uid, &ulen) && ulen > 0) {
      String v;
      for (uint8_t i = 0; i < ulen; i++) {
        char h[4]; snprintf(h, sizeof(h), "%02X", uid[i]); v += h;
      }
      _addRow("UID", v);
    }
  }
}

void ChameleonSlotViewScreen::onInit() {
  snprintf(_title, sizeof(_title), "Slot %d %s", _slot + 1, _lf ? "LF" : "HF");
  _rowCount = 0;
  _loading  = true;
  _ready    = false;

  // Draw placeholder so user sees progress.
  _addRow("Reading", "...");
  _scrollView.setRows(_rows, _rowCount);
  render();

  _rowCount = 0;
  if (_lf) _runLF();
  else     _runHF();

  _scrollView.setRows(_rows, _rowCount);
  _loading = false;
  _ready   = true;
  render();

  int n = Achievement.inc("chameleon_slot_viewed");
  if (n == 1) Achievement.unlock("chameleon_slot_viewed");
}

void ChameleonSlotViewScreen::onUpdate() {
  if (_loading) return;

  if (Uni.Nav->isPressed() && Uni.Nav->heldDuration() >= 1000) {
    Uni.Nav->suppressCurrentPress();
    Screen.setScreen(new ChameleonSlotEditScreen(_slot));
    return;
  }

  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK ||
        dir == INavigation::DIR_LEFT ||
        dir == INavigation::DIR_PRESS) {
      Screen.setScreen(new ChameleonSlotEditScreen(_slot));
      return;
    }
    _scrollView.onNav(dir);
  }
}

void ChameleonSlotViewScreen::onRender() {
  _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
}
