#include "ChameleonMfkey32Screen.h"
#include "utils/chameleon/ChameleonClient.h"
#include "ChameleonHFMenuScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "ui/actions/ShowStatusAction.h"

void ChameleonMfkey32Screen::_load() {
  uint32_t count = 0;
  if (ChameleonClient::get().mf1GetDetectCount(&count)) {
    _count = count;
  }
}

void ChameleonMfkey32Screen::_refresh() {
  _load();
  snprintf(_subs[0], sizeof(_subs[0]), "%s", _logEnabled ? "On" : "Off");
  snprintf(_subs[1], sizeof(_subs[1]), "%u records", (unsigned)_count);
  _subs[2][0] = 0;
  _subs[3][0] = 0;

  _items[0] = {"Logging",        _subs[0]};
  _items[1] = {"Records",        _subs[1]};
  _items[2] = {"Dump to SD",     nullptr};
  _items[3] = {"Refresh",        nullptr};
  setItems(_items);
}

void ChameleonMfkey32Screen::onInit() {
  _refresh();
  int n = Achievement.inc("chameleon_mfkey32_open");
  if (n == 1) Achievement.unlock("chameleon_mfkey32_open");
}

void ChameleonMfkey32Screen::onBack() {
  Screen.setScreen(new ChameleonHFMenuScreen());
}

void ChameleonMfkey32Screen::_toggle() {
  bool next = !_logEnabled;
  if (ChameleonClient::get().mf1SetDetectEnable(next)) {
    _logEnabled = next;
    ShowStatusAction::show(next ? "Logging ON" : "Logging OFF", 1000);
  } else {
    ShowStatusAction::show("Toggle failed", 1000);
  }
  _refresh();
  render();
}

void ChameleonMfkey32Screen::_dumpToSd() {
  auto& c = ChameleonClient::get();
  uint32_t count = 0;
  if (!c.mf1GetDetectCount(&count) || count == 0) {
    ShowStatusAction::show("No records", 1200);
    render();
    return;
  }
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("No storage", 1200);
    render();
    return;
  }
  Uni.Storage->makeDir("/unigeek/nfc/mfkey32");

  char path[96];
  snprintf(path, sizeof(path), "/unigeek/nfc/mfkey32/log_%lu.txt",
           (unsigned long)millis());
  fs::File f = Uni.Storage->open(path, "w");
  if (!f) { ShowStatusAction::show("Open failed", 1200); render(); return; }

  // Each record is 18 bytes: block, flags, uid(4), nt(4), nr(4), ar(4)
  uint32_t cap = count > 200 ? 200 : count;
  char hdr[48];
  snprintf(hdr, sizeof(hdr), "# records: %u\n", (unsigned)count);
  f.print(hdr);

  for (uint32_t i = 0; i < cap; i++) {
    uint8_t rec[18] = {};
    if (!c.mf1GetDetectRecord(i, rec)) break;
    char line[96];
    snprintf(line, sizeof(line),
             "b=%u f=%02X uid=%02X%02X%02X%02X nt=%02X%02X%02X%02X "
             "nr=%02X%02X%02X%02X ar=%02X%02X%02X%02X\n",
             rec[0], rec[1],
             rec[2], rec[3], rec[4], rec[5],
             rec[6], rec[7], rec[8], rec[9],
             rec[10], rec[11], rec[12], rec[13],
             rec[14], rec[15], rec[16], rec[17]);
    f.print(line);
  }
  f.close();

  ShowStatusAction::show("Dumped to SD", 1200);
  int n = Achievement.inc("chameleon_mfkey32_dump");
  if (n == 1) Achievement.unlock("chameleon_mfkey32_dump");
  render();
}

void ChameleonMfkey32Screen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: _toggle();   break;
    case 1: _refresh(); render(); break;
    case 2: _dumpToSd(); break;
    case 3: _refresh(); render(); break;
  }
}
