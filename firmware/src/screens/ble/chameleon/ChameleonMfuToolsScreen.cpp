#include "ui/actions/ConfirmAction.h"
#include "ChameleonMfuAdvancedScreen.h"
#include "ChameleonMfuAuthUtils.h"
#include "ChameleonMfuScreen.h"
#include "ChameleonMfuToolsScreen.h"
#include "ChameleonMfuWriteScreen.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/components/Header.h"
#include "ui/components/TagPrompt.h"
#include "ui/views/ProgressView.h"
#include "utils/ble/ChameleonClient.h"
#include "utils/nfc/HfDumpBuilder.h"

namespace {
static constexpr uint16_t kNtag215WritablePages = 126;

void _mfuEraseProgress(uint16_t done, uint16_t total) {
  char msg[36];
  snprintf(msg, sizeof(msg), "Erasing pages (%u/%u)...",
           (unsigned)done, (unsigned)kNtag215WritablePages);
  const int pct = total ? (int)((uint32_t)done * 100u / total) : 0;
  ProgressView::progress(msg, pct);
}

}

void ChameleonMfuToolsScreen::onInit() {
  _items[0] = {"Read Tag"};
  _items[1] = {"Write to Tag"};
  _items[2] = {"Erase Tag"};
  _items[3] = {"Advanced"};
  setItems(_items);
}

void ChameleonMfuToolsScreen::_writeFromFile() {
  static constexpr uint8_t kMax = 10;
  uint8_t n = _browser.load(this, "/unigeek/nfc/dumps", BrowseFileView::Mode(BrowseFileView::Mode::FILE_ONLY, ".bin", 540));
  if (!n) {
    render();
    ShowStatusAction::show("No NTAG215 dumps", 1600);
    render();
    return;
  }
  const uint8_t count = n < kMax ? n : kMax;
  InputSelectAction::Option opts[kMax];
  String vals[kMax];
  for (uint8_t i = 0; i < count; ++i) {
    vals[i] = String(i);
    opts[i] = {_browser.entry(i).name.c_str(), vals[i].c_str()};
  }
  const char* r = InputSelectAction::popup("NTAG215 dump", opts, count, nullptr);
  if (!r) { render(); return; }
  const uint8_t idx = (uint8_t)atoi(r);
  if (idx >= count) { render(); return; }
  String path = _browser.entry(idx).path;
  render();
  Screen.push(new ChameleonMfuWriteScreen(path));
}

void ChameleonMfuToolsScreen::_writeFromSlot() {
  auto& c = ChameleonClient::get();
  ChameleonClient::SlotTypes types[8] = {};
  if (!c.getSlotTypes(types)) {
    render();
    ShowStatusAction::show("Could not read slots", 1600);
    render();
    return;
  }

  InputSelectAction::Option opts[8];
  String labels[8];
  String vals[8];
  for (uint8_t i = 0; i < 8; ++i) {
    labels[i] = String("Slot ") + (i + 1) + " - " +
                ChameleonClient::tagTypeName(types[i].hfType);
    vals[i] = String(i);
    opts[i] = {labels[i].c_str(), vals[i].c_str()};
  }

  const char* r = InputSelectAction::popup("Source slot", opts, 8, nullptr);
  if (!r) { render(); return; }
  const uint8_t slot = (uint8_t)atoi(r);
  if (slot >= 8) { render(); return; }

  const uint16_t hfType = types[slot].hfType;
  if (hfType == 0) {
    render();
    ShowStatusAction::show("Empty slot", 1200);
    render();
    return;
  }
  if (!(hfType == ChameleonClient::MFU_NTAG215)) {
    render();
    ShowStatusAction::show("Tag not supported", 1600);
    render();
    return;
  }

  Screen.push(new ChameleonMfuWriteScreen(slot));
}

void ChameleonMfuToolsScreen::_writeTag() {
  static const InputSelectAction::Option opts[] = {
    {"From File", "file"},
    {"From Slot", "slot"},
  };
  const char* r = InputSelectAction::popup("Write to Tag", opts, 2, nullptr);
  if (!r) { render(); return; }
  if (strcmp(r, "file") == 0) _writeFromFile();
  else _writeFromSlot();
}

void ChameleonMfuToolsScreen::_eraseTag() {
  if (!ConfirmAction::popup("Erase tag?", "This action cannot be undone")) { render(); return; }
  Header header; header.render("Erase Tag");
  auto& c = ChameleonClient::get();

  uint8_t previousMode = 0;
  const bool restoreMode = c.getMode(&previousMode);
  c.setMode(1);

  auto& lcd = Uni.Lcd;
  const int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  TagPrompt::show("Waiting for tag...", bx, by, bw, bh, "Erase Tag");

  ChameleonClient::MfuTagInfo info = {};
  uint8_t uid[7] = {}, uidLen = 0, atqa[2] = {}, sak = 0;
  bool found = false;
  const uint32_t start = millis();
  while (millis() - start < 5000) {
    Uni.update();
    if (Uni.Nav->wasPressed() && Uni.Nav->readDirection() == INavigation::DIR_BACK) {
      if (restoreMode) c.setMode(previousMode);
      render();
      return;
    }
    if (c.scan14A(uid, &uidLen, atqa, &sak)) { found = true; break; }
    delay(50);
  }
  const bool supported = found && sak == 0x00 && c.mfuDetect(&info) &&
                         info.type == ChameleonClient::MFU_NTAG215 && info.pages == 135;
  if (!supported) {
    if (restoreMode) c.setMode(previousMode);
    render();
    ShowStatusAction::show(found ? "Tag not supported" : "Tag not detected", found ? 1600 : 1200);
    render();
    return;
  }

  uint8_t* image = (uint8_t*)malloc(HfDumpBuilder::NTAG215_SIZE);
  if (!image) {
    if (restoreMode) c.setMode(previousMode);
    render();
    ShowStatusAction::show("Out of memory", 1600);
    render();
    return;
  }

  size_t imageLen = 0;
  const bool built = HfDumpBuilder::buildNtag215(
      info.uid, nullptr, 0, image, imageLen, HfDumpBuilder::NTAG215_SIZE);

  if (!built || imageLen != HfDumpBuilder::NTAG215_SIZE) {
    free(image);
    if (restoreMode) c.setMode(previousMode);
    render();
    ShowStatusAction::show("Cannot build empty tag", 1600);
    render();
    return;
  }

  uint8_t pwd[4] = {}; bool usePwd = false;
  if (!ChameleonMfuAuthUtils::prepare(c, info, false, pwd, usePwd)) {
    free(image);
    if (restoreMode) c.setMode(previousMode);
    render();
    return;
  }

  render();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  ProgressView::init();
  ProgressView::progress("Erasing pages (0/126)...", 0);
  const bool ok = c.mfuWriteNtag215User(
      image, (uint16_t)imageLen, _mfuEraseProgress, &info,
      usePwd ? pwd : nullptr);
  ProgressView::finish();

  free(image);
  if (restoreMode) c.setMode(previousMode);

  render();
  ShowStatusAction::show(ok ? "Tag erased" : "Failed", 1600);
}

void ChameleonMfuToolsScreen::onItemSelected(uint8_t index) {
  if (index == 0) Screen.push(new ChameleonMfuScreen());
  else if (index == 1) _writeTag();
  else if (index == 2) _eraseTag();
  else if (index == 3) Screen.push(new ChameleonMfuAdvancedScreen());
}

void ChameleonMfuToolsScreen::onBack() { Screen.goBack(); }
