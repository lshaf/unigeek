#include "ChameleonMfuToolsScreen.h"
#include "ChameleonMfuScreen.h"
#include "ChameleonMfuWriteScreen.h"
#include "ChameleonMfuPagesScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/components/Header.h"
#include "ui/views/ProgressView.h"
#include "utils/nfc/NfcDumpBuilder.h"

namespace {
static constexpr uint16_t kNtag215WritablePages = 126;

void _mfuEraseProgress(uint16_t done, uint16_t total) {
  char msg[36];
  snprintf(msg, sizeof(msg), "Erasing pages (%u/%u)...",
           (unsigned)done, (unsigned)kNtag215WritablePages);
  const int pct = total ? (int)((uint32_t)done * 100u / total) : 0;
  ProgressView::progress(msg, pct);
}

const char* _mfuSensitivePageLabel(uint16_t type, uint16_t page) {
  uint16_t dynamicLock = 0xFFFF;
  uint16_t config0 = 0xFFFF;

  switch (type) {
    case ChameleonClient::MFU_NTAG210:
    case ChameleonClient::MFU_ULTRALIGHT_EV1_11:
      config0 = 16; break;
    case ChameleonClient::MFU_NTAG212:
    case ChameleonClient::MFU_ULTRALIGHT_EV1_21:
      dynamicLock = 36; config0 = 37; break;
    case ChameleonClient::MFU_NTAG213:
      dynamicLock = 40; config0 = 41; break;
    case ChameleonClient::MFU_NTAG215:
      dynamicLock = 130; config0 = 131; break;
    case ChameleonClient::MFU_NTAG216:
      dynamicLock = 226; config0 = 227; break;
    case ChameleonClient::MFU_ULTRALIGHT_C:
      if (page >= 40 && page <= 43) return "Security/config page";
      if (page >= 44 && page <= 47) return "3DES key page";
      return nullptr;
    default:
      return nullptr;
  }

  if (page == dynamicLock) return "Dynamic lock page";
  if (page == config0 || page == config0 + 1) return "Configuration page";
  if (page == config0 + 2) return "Password page";
  if (page == config0 + 3) return "PACK page";
  return nullptr;
}

bool _confirmMfuSensitiveWrite(uint16_t type, uint16_t page) {
  const char* label = _mfuSensitivePageLabel(type, page);
  if (!label) return true;

  static const InputSelectAction::Option opts[] = {
    {"Write anyway", "write"},
  };
  String title = String("Warning: ") + label;
  const char* choice = InputSelectAction::popup(title.c_str(), opts, 1, nullptr);
  return choice && strcmp(choice, "write") == 0;
}
}

void ChameleonMfuToolsScreen::onInit() {
  _items[0] = {"Read Tag"};
  _items[1] = {"Write to Tag"};
  _items[2] = {"Erase Tag"};
  _items[3] = {"Read Pages"};
  _items[4] = {"Write Page"};
  setItems(_items);
}

void ChameleonMfuToolsScreen::_writeFromFile() {
  static constexpr uint8_t kMax = 10;
  uint8_t n = _browser.load(this, "/unigeek/nfc/dumps", BrowseFileView::Mode(BrowseFileView::Mode::FILE_ONLY, ".bin", 540));
  if (!n) {
    render();
    ShowStatusAction::show("No NTAG215 .bin", 1500);
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
    {"From File", "file"},
    {"From Slot", "slot"},
  };
  const char* r = InputSelectAction::popup("Write to Tag", opts, 2, nullptr);
  if (!r) { render(); return; }
  if (strcmp(r, "file") == 0) _writeFromFile();
  else _writeFromSlot();
}


void ChameleonMfuToolsScreen::_eraseTag() {
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
  lcd.drawString("Place tag on reader...", bx + bw / 2, by + bh / 2);

  ChameleonClient::MfuTagInfo info = {};
  if (!c.mfuDetect(&info) ||
      info.type != ChameleonClient::MFU_NTAG215 ||
      info.pages != 135) {
    if (restoreMode) c.setMode(previousMode);
    render();
    ShowStatusAction::show("Tag must be NTAG215", 1500);
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
  ProgressView::progress("Erasing pages (0/126)...", 0);
  const bool ok = c.mfuWriteNtag215User(
      image, (uint16_t)imageLen, _mfuEraseProgress, &info);
  ProgressView::finish();

  free(image);
  if (restoreMode) c.setMode(previousMode);

  render();
  ShowStatusAction::show(ok ? "Tag erased" : "Erase failed", 1600);
  render();
}

void ChameleonMfuToolsScreen::_writePage() {
  Header header; header.render("Write Page");
  auto& c = ChameleonClient::get();

  uint8_t previousMode = 0;
  const bool restoreMode = c.getMode(&previousMode);
  c.setMode(1);

  auto& lcd = Uni.Lcd;
  const int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Place tag on reader...", bx + bw / 2, by + bh / 2);

  ChameleonClient::MfuTagInfo info = {};
  if (!c.mfuDetect(&info) || info.pages <= 4) {
    if (restoreMode) c.setMode(previousMode);
    render();
    ShowStatusAction::show("Unsupported / no tag", 1400);
    render();
    return;
  }

  String title = String("Page (4..") + String(info.pages - 1) + ")";
  const int page = InputNumberAction::popup(title.c_str(), 4, info.pages - 1, 4);
  if (InputNumberAction::wasCancelled()) {
    if (restoreMode) c.setMode(previousMode);
    render();
    return;
  }

  if (!_confirmMfuSensitiveWrite(info.type, (uint16_t)page)) {
    if (restoreMode) c.setMode(previousMode);
    render();
    return;
  }

  String hex = InputTextAction::popup("Page data (8 hex)", "", InputTextAction::INPUT_HEX);
  if (InputTextAction::wasCancelled()) {
    if (restoreMode) c.setMode(previousMode);
    render();
    return;
  }
  hex.replace(" ", "");
  hex.replace(":", "");
  if (hex.length() != 8) {
    if (restoreMode) c.setMode(previousMode);
    render();
    ShowStatusAction::show("Need 8 hex chars", 1200);
    render();
    return;
  }

  uint8_t data[4] = {};
  for (uint8_t i = 0; i < 4; ++i) {
    char b[3] = {hex[i * 2], hex[i * 2 + 1], 0};
    char* end = nullptr;
    unsigned long v = strtoul(b, &end, 16);
    if (!end || *end) {
      if (restoreMode) c.setMode(previousMode);
      render();
      ShowStatusAction::show("Bad hex", 1200);
      render();
      return;
    }
    data[i] = (uint8_t)v;
  }

  render();
  const bool ok = c.mfuWritePage((uint8_t)page, data);
  if (restoreMode) c.setMode(previousMode);
  render();
  ShowStatusAction::show(ok ? "Page written" : "Write failed", 1400);
  render();
}

void ChameleonMfuToolsScreen::onItemSelected(uint8_t index) {
  if (index == 0) Screen.push(new ChameleonMfuScreen());
  else if (index == 1) _writeTag();
  else if (index == 2) _eraseTag();
  else if (index == 3) Screen.push(new ChameleonMfuPagesScreen());
  else if (index == 4) _writePage();
}

void ChameleonMfuToolsScreen::onBack() { Screen.goBack(); }
