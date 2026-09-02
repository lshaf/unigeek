#include "ChameleonMfuToolsScreen.h"
#include "ChameleonMfuScreen.h"
#include "ChameleonMfuWriteScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"
#include "utils/nfc/NfcDumpBuilder.h"

namespace {
static constexpr uint16_t kNtag215WritablePages = 126;

void _mfuEraseProgress(uint16_t done, uint16_t total) {
  char msg[36];
  snprintf(msg, sizeof(msg), "Erasing %u/%u pages",
           (unsigned)done, (unsigned)kNtag215WritablePages);
  const int pct = total ? (int)((uint32_t)done * 100u / total) : 0;
  ProgressView::progress(msg, pct);
}
}

void ChameleonMfuToolsScreen::onInit() {
  _items[0] = {"Read Tag"};
  _items[1] = {"Write to Tag"};
  _items[2] = {"Erase Tag"};
  setItems(_items);
}

void ChameleonMfuToolsScreen::_writeFromFile() {
  static constexpr uint8_t kMax = 10;
  uint8_t n = _browser.load(this, "/unigeek/nfc/dumps", ".bin");
  if (!n) {
    render();
    ShowStatusAction::show("No .bin in nfc/dumps", 1500);
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
    ShowStatusAction::show("Could not read slots", 1500);
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
    ShowStatusAction::show("Unsupported tag type", 1500);
    render();
    return;
  }

  Screen.push(new ChameleonMfuWriteScreen(slot));
}

void ChameleonMfuToolsScreen::_writeTag() {
  static const InputSelectAction::Option opts[] = {
    {"from File", "file"},
    {"from Slot", "slot"},
  };
  const char* r = InputSelectAction::popup("Write to Tag", opts, 2, nullptr);
  if (!r) { render(); return; }
  if (strcmp(r, "file") == 0) _writeFromFile();
  else _writeFromSlot();
}


void ChameleonMfuToolsScreen::_eraseTag() {
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
  lcd.drawString("Place NTAG215...", bx + bw / 2, by + bh / 2);

  ChameleonClient::MfuTagInfo info = {};
  if (!c.mfuDetect(&info) ||
      info.type != ChameleonClient::MFU_NTAG215 ||
      info.pages != 135) {
    if (restoreMode) c.setMode(previousMode);
    render();
    ShowStatusAction::show("Target must be NTAG215", 1500);
    render();
    return;
  }

  uint8_t* image = (uint8_t*)malloc(NfcDumpBuilder::NTAG215_SIZE);
  if (!image) {
    if (restoreMode) c.setMode(previousMode);
    render();
    ShowStatusAction::show("Out of memory", 1500);
    render();
    return;
  }

  size_t imageLen = 0;
  const bool built = NfcDumpBuilder::buildNtag215(
      info.uid, nullptr, 0, image, imageLen, NfcDumpBuilder::NTAG215_SIZE);

  if (!built || imageLen != NfcDumpBuilder::NTAG215_SIZE) {
    free(image);
    if (restoreMode) c.setMode(previousMode);
    render();
    ShowStatusAction::show("Cannot build empty tag", 1500);
    render();
    return;
  }

  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  ProgressView::init();
  ProgressView::progress("Erasing 0/126 pages", 0);
  const bool ok = c.mfuWriteNtag215User(
      image, (uint16_t)imageLen, _mfuEraseProgress, &info);
  ProgressView::finish();

  free(image);
  if (restoreMode) c.setMode(previousMode);

  render();
  ShowStatusAction::show(ok ? "Tag erased" : "Erase failed", 1600);
  render();
}

void ChameleonMfuToolsScreen::onItemSelected(uint8_t index) {
  if (index == 0) Screen.push(new ChameleonMfuScreen());
  else if (index == 1) _writeTag();
  else if (index == 2) _eraseTag();
}

void ChameleonMfuToolsScreen::onBack() { Screen.goBack(); }
