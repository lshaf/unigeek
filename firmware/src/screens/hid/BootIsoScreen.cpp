#include "BootIsoScreen.h"
#ifdef DEVICE_HAS_USB_HID
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "utils/usb/UsbMscUtil.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/actions/InputTextAction.h"
#include "utils/keyboard/USBKeyboardUtil.h"

// Persistent: TinyUSB retains the HID device registration for the entire boot.
static USBKeyboardUtil& bootKeyboard() {
  static USBKeyboardUtil keyboard(true);
  return keyboard;
}


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
  // Register HID before beginIso() calls USB.begin().
  USBKeyboardUtil& keyboard = bootKeyboard();
  if (!UsbMscUtil::instance().beginIso(Uni.Storage, path.c_str())) {
    ShowStatusAction::show(UsbMscUtil::instance().failReason() == UsbMscUtil::FAIL_USB_BUSY
                             ? "USB busy: reboot required" : "Cannot open ISO");
    return;
  }
  keyboard.begin();
  _holdTriggered = false;
  _bootSelection = 0;
  _navMode = 0;
  _backHoldTriggered = false;
  _mounted = true;
  render();
}

void BootIsoScreen::onUpdate() {
  if (!_mounted) { ListScreen::onUpdate(); return; }
  // Long BACK returns to Boot Keys. Suppress its release to avoid switching modes.
  if (_navMode && !_backHoldTriggered && Uni.Nav->isPressed() &&
      Uni.Nav->currentDirection() == INavigation::DIR_BACK &&
      Uni.Nav->heldDuration() >= 750) {
    _backHoldTriggered = true;
    Uni.Nav->suppressCurrentPress();
    _navMode = 0;
    _bootSelection = 0;
    render();
    return;
  }
#ifndef DEVICE_HAS_KEYBOARD
  // Hold encoder PRESS opens the existing virtual HID keyboard.
  if (_navMode && !_holdTriggered && Uni.Nav->isPressed() &&
      Uni.Nav->currentDirection() == INavigation::DIR_PRESS &&
      Uni.Nav->heldDuration() >= 750) {
    _holdTriggered = true;
    Uni.Nav->suppressCurrentPress();
    while (Uni.Nav->isPressed()) { Uni.update(); delay(10); }
    InputTextAction::popup("USB Keyboard", "", InputTextAction::INPUT_TEXT,
                           InputTextAction::PROFILE_HID, &bootKeyboard());
    render();
  }
#endif
  if (!Uni.Nav->wasPressed()) return;
  const auto dir = Uni.Nav->readDirection();
  if (dir == INavigation::DIR_BACK) {
    // The long BACK release is already suppressed by suppressCurrentPress().
    // Do not consume the next short BACK on the Boot Keys screen.
    onBack();
    return;
  }
  if (dir == INavigation::DIR_PRESS && _holdTriggered) {
    _holdTriggered = false;
    return;
  }
  if (_navMode) {
    if (dir == INavigation::DIR_PRESS) {
      bootKeyboard().write(KEY_RETURN);
    } else if (dir == INavigation::DIR_DOWN || dir == INavigation::DIR_RIGHT) {
      bootKeyboard().write(_navMode == 1 ? KEY_DOWN_ARROW : KEY_RIGHT_ARROW);
    } else if (dir == INavigation::DIR_UP || dir == INavigation::DIR_LEFT) {
      bootKeyboard().write(_navMode == 1 ? KEY_UP_ARROW : KEY_LEFT_ARROW);
    }
    return;
  }
  if (dir == INavigation::DIR_RIGHT || dir == INavigation::DIR_DOWN) {
    const uint8_t previous = _bootSelection;
    _bootSelection = (_bootSelection + 1) % 8;
    drawVirtualKey(previous);
    drawVirtualKey(_bootSelection);
  } else if (dir == INavigation::DIR_LEFT || dir == INavigation::DIR_UP) {
    const uint8_t previous = _bootSelection;
    _bootSelection = (_bootSelection + 7) % 8;
    drawVirtualKey(previous);
    drawVirtualKey(_bootSelection);
  } else if (dir == INavigation::DIR_PRESS) {
    if (_bootSelection == 7) {
      _navMode = 1;
      render();
    } else {
      static const uint8_t bootKeys[7] = {
        KEY_ESC, KEY_DELETE, KEY_F1, KEY_F2, KEY_F10, KEY_F11, KEY_F12
      };
      bootKeyboard().write(bootKeys[_bootSelection]);
    }
  }
}

void BootIsoScreen::onRender() {
  if (!_mounted) { ListScreen::onRender(); return; }
  auto& lcd = Uni.Lcd;
  const int x0 = bodyX(), y0 = bodyY();
  const int w = bodyW(), h = bodyH();
  lcd.fillRect(x0, y0, w, h, TFT_BLACK);
  lcd.setTextSize(1);
  lcd.setTextDatum(TL_DATUM);
  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  String label = _selected;
  const int maxNameW = max(0, w - 12);
  if (lcd.textWidth(label) > maxNameW) {
    const String ellipsis = "...";
    while (label.length() && lcd.textWidth(label + ellipsis) > maxNameW)
      label.remove(label.length() - 1);
    label += ellipsis;
  }
  lcd.drawString(label, x0 + 6, y0 + 3);

  const int footerH = _navMode ? 32 : 17;
  if (!_navMode) {
    for (uint8_t i = 0; i < 8; ++i) drawVirtualKey(i);
  } else {
    // Minimal navigation label centered in the content area above the hints.
    const int midX = x0 + w / 2;
    const int midY = y0 + 30 + (h - 30 - footerH) / 2;
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.drawString(_navMode == 1 ? "UP/DOWN" : "LEFT/RIGHT", midX, midY);
  }
  lcd.drawFastHLine(x0, y0 + h - footerH, w, TFT_DARKGREY);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  if (!_navMode) {
    lcd.drawString("[Back] Eject", x0 + w / 2, y0 + h - footerH / 2);
  } else {
#ifndef DEVICE_HAS_KEYBOARD
    lcd.drawString("[Hold] Keyboard", x0 + w / 4, y0 + h - 24);
#endif
    lcd.drawString(_navMode == 1 ? "[Back] Switch" : "[Back] Switch Keys",
                   x0 + (3 * w) / 4, y0 + h - 24);
    lcd.drawString("[Hold Back] Boot Keys", x0 + w / 2, y0 + h - 8);
  }
}

// Repaint only one key on encoder rotation; do not clear the whole body.
void BootIsoScreen::drawVirtualKey(uint8_t index) {
  if (!_mounted || _navMode || index >= 8) return;
  auto& lcd = Uni.Lcd;
  const int x0 = bodyX(), y0 = bodyY();
  const int w = bodyW(), h = bodyH();
  const int usableH = h - 17 - 30 - 3;
  static const char* labels[8] = {
    "ESC", "DEL", "F1", "F2", "F10", "F11", "F12", "NAV"
  };
  const int gap = 3;
  const int keyW = (w - 12 - 3 * gap) / 4;
  const int keyH = min(32, max(16, (usableH - gap) / 2));
  const int gridW = 4 * keyW + 3 * gap;
  const int gridH = 2 * keyH + gap;
  const int x = x0 + (w - gridW) / 2;
  const int y = y0 + 30 + (usableH - gridH) / 2;
  const int bx = x + (index % 4) * (keyW + gap);
  const int by = y + (index / 4) * (keyH + gap);
  const bool selected = _bootSelection == index;
  const uint16_t fill = selected ? Config.getThemeColor() : TFT_DARKGREY;
  lcd.fillRoundRect(bx, by, keyW, keyH, 3, fill);
  lcd.drawRoundRect(bx, by, keyW, keyH, 3,
                    selected ? TFT_WHITE : TFT_LIGHTGREY);
  lcd.setTextSize(1);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_WHITE, fill);
  lcd.drawString(labels[index], bx + keyW / 2, by + keyH / 2);
}

void BootIsoScreen::onBack() {
  if (_mounted && _navMode) {
    _navMode = _navMode == 1 ? 2 : 1;
    render();
    return;
  }
  if (_mounted) {
    bootKeyboard().releaseAll();
    UsbMscUtil::instance().end();
    _mounted = false;
  }
  Screen.goBack();
}
#endif
