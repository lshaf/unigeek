#include "BootIsoScreen.h"
#ifdef DEVICE_HAS_USB_HID
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "utils/usb/UsbMscUtil.h"
#include "ui/actions/ShowStatusAction.h"

void BootIsoScreen::onInit() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage unavailable");
    Screen.goBack();
    return;
  }
  Uni.Storage->makeDir("/unigeek");
  Uni.Storage->makeDir("/unigeek/hid");
  Uni.Storage->makeDir("/unigeek/hid/isos");
  fs::File dir = Uni.Storage->open("/unigeek/hid/isos", "r");
  if (dir && dir.isDirectory()) {
    fs::File f = dir.openNextFile();
    while (f && _count < MAX_ISOS) {
      String name = f.name();
      int slash = name.lastIndexOf('/');
      if (slash >= 0) name = name.substring(slash + 1);
      String ext = name;
      ext.toLowerCase();
      if (!f.isDirectory() && ext.endsWith(".iso")) {
        _names[_count] = name;
        _items[_count].label = _names[_count].c_str();
        ++_count;
      }
      f.close();
      f = dir.openNextFile();
    }
    if (f) f.close();
    dir.close();
  }
  if (!_count) {
    ShowStatusAction::show("No ISO files found");
    Screen.goBack();
    return;
  }
  setItems(_items, _count);
}

void BootIsoScreen::onItemSelected(uint8_t index) {
  if (_mounted || index >= _count) return;
  _selected = _names[index];
  String path = "/unigeek/hid/isos/" + _selected;
  if (!UsbMscUtil::instance().beginIso(Uni.Storage, path.c_str())) {
    ShowStatusAction::show(UsbMscUtil::instance().failReason() == UsbMscUtil::FAIL_USB_BUSY
                             ? "USB busy: reboot required" : "Cannot open ISO");
    return;
  }
  _mounted = true;
  render();
}

void BootIsoScreen::onUpdate() {
  if (!_mounted) { ListScreen::onUpdate(); return; }
  if (Uni.Nav->wasPressed() && (Uni.Nav->readDirection() == INavigation::DIR_BACK ||
                               Uni.Nav->readDirection() == INavigation::DIR_PRESS)) onBack();
}

void BootIsoScreen::onRender() {
  if (!_mounted) { ListScreen::onRender(); return; }
  auto& lcd = Uni.Lcd;
  const int cx = bodyX() + bodyW() / 2;
  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_GREEN, TFT_BLACK);
  lcd.drawString("ISO mounted", cx, bodyY() + 25);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  lcd.drawString(_selected, cx, bodyY() + 48);
  lcd.drawString("Select USB boot on PC", cx, bodyY() + 72);
  lcd.drawString("[Back] Eject", cx, bodyY() + 96);
}

void BootIsoScreen::onBack() {
  if (_mounted) {
    UsbMscUtil::instance().end();
    _mounted = false;
  }
  Screen.goBack();
}
#endif
