#include "ChameleonMfuAdvancedScreen.h"
#include "ChameleonMfuPagesScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/components/Header.h"

namespace {
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

void ChameleonMfuAdvancedScreen::onInit() {
  _items[0] = {"Read Pages"};
  _items[1] = {"Write Page"};
  setItems(_items);
}

void ChameleonMfuAdvancedScreen::_writePage() {
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


void ChameleonMfuAdvancedScreen::onItemSelected(uint8_t index) {
  if (index == 0) Screen.push(new ChameleonMfuPagesScreen());
  else if (index == 1) _writePage();
}

void ChameleonMfuAdvancedScreen::onBack() { Screen.goBack(); }
