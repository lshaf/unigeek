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
  _padSelection = 0;
  _bootSelection = 7;
  _showDpad = false;
  _mounted = true;
  render();
}

void BootIsoScreen::onUpdate() {
  if (!_mounted) { ListScreen::onUpdate(); return; }
  // Hold opens the existing on-screen HID keyboard; release must not send Enter.
  if (!_holdTriggered && Uni.Nav->isPressed() &&
      Uni.Nav->currentDirection() == INavigation::DIR_PRESS &&
      Uni.Nav->heldDuration() >= 750) {
    _holdTriggered = true;
    // Consume the D-pad Hold before entering the keyboard's own event loop.
    // Otherwise it sees the still-held Select and types a shifted 'A'.
    Uni.Nav->suppressCurrentPress();
    while (Uni.Nav->isPressed()) {
      Uni.update();
      delay(10);
    }
    InputTextAction::popup("USB Keyboard", "", InputTextAction::INPUT_TEXT,
                           InputTextAction::PROFILE_HID, &bootKeyboard());
    render();
  }
  if (!Uni.Nav->wasPressed()) return;
  const auto dir = Uni.Nav->readDirection();
  if (dir == INavigation::DIR_BACK) { onBack(); return; }
  if (dir == INavigation::DIR_PRESS && _holdTriggered) {
    _holdTriggered = false;
    return;
  }
  // Rotation only changes the selection in the active view.
  const uint8_t count = _showDpad ? 5 : 8;
  uint8_t& selection = _showDpad ? _padSelection : _bootSelection;
  if (dir == INavigation::DIR_RIGHT || dir == INavigation::DIR_DOWN) {
    const uint8_t previous = selection;
    selection = (selection + 1) % count;
    drawVirtualKey(previous);
    drawVirtualKey(selection);
  } else if (dir == INavigation::DIR_LEFT || dir == INavigation::DIR_UP) {
    const uint8_t previous = selection;
    selection = (selection + count - 1) % count;
    drawVirtualKey(previous);
    drawVirtualKey(selection);
  } else if (dir == INavigation::DIR_PRESS) {
    if (_showDpad) {
      switch (_padSelection) {
        case 0: bootKeyboard().write(KEY_RETURN); break;
        case 1: bootKeyboard().write(KEY_UP_ARROW); break;
        case 2: bootKeyboard().write(KEY_RIGHT_ARROW); break;
        case 3: bootKeyboard().write(KEY_DOWN_ARROW); break;
        case 4: bootKeyboard().write(KEY_LEFT_ARROW); break;
      }
    } else if (_bootSelection == 7) {
      _showDpad = true;
      render();
    } else {
      static const uint8_t bootKeys[7] = {
        KEY_ESC, KEY_DELETE, KEY_F1, KEY_F2, KEY_F10, KEY_F11, KEY_F12
      };
      bootKeyboard().write(bootKeys[_bootSelection]);
    }
    // A key press never moves the selection.
  }
}

void BootIsoScreen::onRender() {
  if (!_mounted) { ListScreen::onRender(); return; }
  auto& lcd = Uni.Lcd;
  // Preserve the standard header and left status bar from BaseScreen.
  const int x0 = bodyX();
  const int y0 = bodyY();
  const int w = bodyW();
  const int h = bodyH();
  lcd.fillRect(x0, y0, w, h, TFT_BLACK);
  lcd.setTextSize(1);
  lcd.setTextDatum(TL_DATUM);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  // Show the full ISO filename when it fits; truncate only if necessary.
  String label = _selected;
  const int maxNameW = max(0, w - 12);
  if (lcd.textWidth(label) > maxNameW) {
    const String ellipsis = "...";
    while (label.length() > 0 &&
           lcd.textWidth(label + ellipsis) > maxNameW)
      label.remove(label.length() - 1);
    label += ellipsis;
  }
  lcd.drawString(label, x0 + 6, y0 + 3);

  const int footerH = 17;
  const int top = 30;
  const int usableH = h - footerH - top - 3;
  const uint8_t count = _showDpad ? 5 : 8;
  for (uint8_t i = 0; i < count; ++i) drawVirtualKey(i);
  lcd.drawFastHLine(x0, y0 + h - footerH, w, TFT_DARKGREY);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  lcd.drawString("[Hold] Keyboard", x0 + w / 4, y0 + h - footerH / 2);
  lcd.drawString(_showDpad ? "[Back] Boot Keys" : "[Back] Eject",
                 x0 + (3 * w) / 4, y0 + h - footerH / 2);
}

// Repaint only one key on encoder rotation; do not clear the whole body.
void BootIsoScreen::drawVirtualKey(uint8_t index) {
  if (!_mounted || index >= (_showDpad ? 5 : 8)) return;
  auto& lcd = Uni.Lcd;
  const int x0 = bodyX(), y0 = bodyY();
  const int w = bodyW(), h = bodyH();
  const int usableH = h - 17 - 30 - 3;
  int bx, by, bw, bh, cx, cy;
  const char* label;
  bool selected;
  if (_showDpad) {
    static const char* names[5] = {"ENT", "^", ">", "v", "<"};
    const int cell = min((w - 24) / 3, usableH / 3);
    const int side = max(12, cell);
    const int x = x0 + (w - 3 * side) / 2;
    const int y = y0 + 30 + (usableH - 3 * side) / 2;
    const int xs[5] = {x + side, x + side, x + 2 * side, x + side, x};
    const int ys[5] = {y + side, y, y + side, y + 2 * side, y + side};
    bx = xs[index] + 1; by = ys[index] + 1;
    bw = side - 2; bh = side - 2;
    cx = xs[index] + side / 2; cy = ys[index] + side / 2;
    label = names[index]; selected = _padSelection == index;
  } else {
    static const char* labels[8] = {
      "ESC", "DEL", "F1", "F2", "F10", "F11", "F12", "D-PAD"
    };
    const int gap = 3;
    const int keyW = (w - 12 - 3 * gap) / 4;
    const int keyH = min(32, max(16, (usableH - gap) / 2));
    const int gridW = 4 * keyW + 3 * gap;
    const int gridH = 2 * keyH + gap;
    const int x = x0 + (w - gridW) / 2;
    const int y = y0 + 30 + (usableH - gridH) / 2;
    bx = x + (index % 4) * (keyW + gap);
    by = y + (index / 4) * (keyH + gap);
    bw = keyW; bh = keyH;
    cx = bx + keyW / 2; cy = by + keyH / 2;
    label = labels[index]; selected = _bootSelection == index;
  }
  const uint16_t fill = selected ? Config.getThemeColor() : TFT_DARKGREY;
  lcd.fillRoundRect(bx, by, bw, bh, 3, fill);
  lcd.drawRoundRect(bx, by, bw, bh, 3,
                    selected ? TFT_WHITE : TFT_LIGHTGREY);
  lcd.setTextSize(1);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_WHITE, fill);
  lcd.drawString(label, cx, cy);
}

void BootIsoScreen::onBack() {
  if (_mounted && _showDpad) {
    _showDpad = false;
    _bootSelection = 7;
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
