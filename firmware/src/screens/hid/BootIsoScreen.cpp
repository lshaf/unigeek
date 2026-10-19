#include "BootIsoScreen.h"
#if defined(DEVICE_HAS_USB_HID) && defined(DEVICE_SUPPORTS_BOOT_ISO)
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "utils/usb/UsbMscUtil.h"
#include "utils/usb/BootIsoCdc.h"
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
    ShowStatusAction::show("Storage not available");
    Screen.goBack();
    return;
  }
  Uni.Storage->makeDir("/unigeek");
  Uni.Storage->makeDir("/unigeek/hid");
  const char* folder = _imgMode ? "/unigeek/hid/imgs" : "/unigeek/hid/isos";
  Uni.Storage->makeDir(folder);
  fs::File dir = Uni.Storage->open(folder, "r");
  if (dir && dir.isDirectory()) {
    fs::File f = dir.openNextFile();
    while (f && _count < MAX_ISOS) {
      String name = f.name();
      int slash = name.lastIndexOf('/');
      if (slash >= 0) name = name.substring(slash + 1);
      String ext = name;
      ext.toLowerCase();
      if (!f.isDirectory() && ext.endsWith(_imgMode ? ".img" : ".iso")) {
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
    ShowStatusAction::show(_imgMode ? "No IMG files available" : "No ISO files available");
    Screen.goBack();
    return;
  }
  setItems(_items, _count);
}

void BootIsoScreen::onItemSelected(uint8_t index) {
  if (_mounted || index >= _count) return;
  _selected = _names[index];
  String path = String(_imgMode ? "/unigeek/hid/imgs/" : "/unigeek/hid/isos/") + _selected;
  // Register HID before beginIso() calls USB.begin().
  USBKeyboardUtil& keyboard = bootKeyboard();
  if (!(_imgMode ? UsbMscUtil::instance().beginImg(Uni.Storage, path.c_str())
                  : UsbMscUtil::instance().beginIso(Uni.Storage, path.c_str()))) {
    ShowStatusAction::show(UsbMscUtil::instance().failReason() == UsbMscUtil::FAIL_USB_BUSY
                             ? "USB busy\nReboot required" : (_imgMode ? "Failed to open IMG" : "Failed to open ISO"));
    return;
  }
  keyboard.begin();
  _holdTriggered = false;
  _keySelection = 0;
  _activeBlock = 0;
  _insideBlock = false;
  _backHoldTriggered = false;
  _mounted = true;
#ifdef DEVICE_M5_CARDPUTER_ADV
  _keyboardRelay = false;
  _bootButtonWasDown = digitalRead(BTN_BOOT) == LOW;
  Uni.Nav->setSuppressKeys(false);
#endif
  render();
}

void BootIsoScreen::onUpdate() {
  if (!_mounted) { ListScreen::onUpdate(); return; }

#ifdef DEVICE_M5_CARDPUTER_ADV
  // BtnG0 activates the physical USB keyboard independently of Navigate.
  const bool bootDown = digitalRead(BTN_BOOT) == LOW;
  const bool bootPressed = bootDown && !_bootButtonWasDown;
  _bootButtonWasDown = bootDown;
  if (_keyboardRelay) {
    while (Uni.Keyboard && Uni.Keyboard->available()) {
      const char c = Uni.Keyboard->getKey();
      // In physical keyboard mode Backspace is a normal host key.
      bootKeyboard().write((uint8_t)c);
    }
    if (bootPressed) {
      bootKeyboard().releaseAll();
      _keyboardRelay = false;
      _insideBlock = false;
      _activeBlock = 0;
      Uni.Nav->setSuppressKeys(false);
      while (Uni.Keyboard && Uni.Keyboard->available()) Uni.Keyboard->getKey();
      render();
    }
    return;
  }
  if (bootPressed) {
    while (Uni.Keyboard && Uni.Keyboard->available()) Uni.Keyboard->getKey();
    _keyboardRelay = true;
    _insideBlock = false;
    _activeBlock = 1;
    Uni.Nav->setSuppressKeys(true);
    render();
    return;
  }
#endif

#ifndef DEVICE_HAS_KEYBOARD
  if (!_holdTriggered && Uni.Nav->isPressed() &&
      Uni.Nav->currentDirection() == INavigation::DIR_PRESS &&
      Uni.Nav->heldDuration() >= 750) {
    _holdTriggered = true;
    Uni.Nav->suppressCurrentPress();
    while (Uni.Nav->isPressed()) { Uni.update(); delay(10); }
    InputTextAction::popup("USB Keyboard", "", InputTextAction::INPUT_TEXT,
                           InputTextAction::PROFILE_HID, &bootKeyboard());
    render();
    return;
  }
#endif
  if (!Uni.Nav->wasPressed()) return;
  const auto dir = Uni.Nav->readDirection();
  if (dir == INavigation::DIR_BACK) {
    if (_insideBlock) {
      _insideBlock = false;
      render();
    } else {
      ejectIso();
    }
    return;
  }
  if (dir == INavigation::DIR_PRESS && _holdTriggered) {
    _holdTriggered = false;
    return;
  }
  if (dir == INavigation::DIR_UP || dir == INavigation::DIR_LEFT ||
      dir == INavigation::DIR_DOWN || dir == INavigation::DIR_RIGHT) {
    const bool forward = dir == INavigation::DIR_DOWN || dir == INavigation::DIR_RIGHT;
    if (!_insideBlock) {
      const uint8_t previousBlock = _activeBlock;
      _activeBlock = 1 - _activeBlock;
      // Only the outlines change; avoid clearing the whole display.
#ifdef DEVICE_M5_CARDPUTER_ADV
      if (previousBlock == 0) drawBlockBorder(0);
      else drawVirtualKey(0, 1);
      if (_activeBlock == 0) drawBlockBorder(0);
      else drawVirtualKey(0, 1);
#else
      drawBlockBorder(previousBlock);
      drawBlockBorder(_activeBlock);
#endif
    } else {
#ifdef DEVICE_M5_CARDPUTER_ADV
      if (_activeBlock == 1) {
        // Navigate forwards physical directional keys, without suppressing Uni.Nav.
        uint8_t key = KEY_UP_ARROW;
        if (dir == INavigation::DIR_DOWN) key = KEY_DOWN_ARROW;
        else if (dir == INavigation::DIR_LEFT) key = KEY_LEFT_ARROW;
        else if (dir == INavigation::DIR_RIGHT) key = KEY_RIGHT_ARROW;
        bootKeyboard().write(key);
        return;
      }
      // Boot Keys are a 4x2 grid: vertical moves preserve the column.
      if (_activeBlock == 0) {
        const uint8_t previous = _keySelection;
        if (dir == INavigation::DIR_UP || dir == INavigation::DIR_DOWN)
          _keySelection = (_keySelection + 4) % 8;
        else
          _keySelection = (_keySelection + (forward ? 1 : 7)) % 8;
        drawVirtualKey(previous, 0);
        drawVirtualKey(_keySelection, 0);
      }
#else
      const uint8_t count = _activeBlock == 0 ? 8 : 5;
      const uint8_t previous = _keySelection;
      _keySelection = (_keySelection + (forward ? 1 : count - 1)) % count;
      drawVirtualKey(previous, _activeBlock);
      drawVirtualKey(_keySelection, _activeBlock);
#endif
    }
  } else if (dir == INavigation::DIR_PRESS) {
    if (!_insideBlock) {
      _insideBlock = true;
      _keySelection = 0;
      render();
    } else if (_activeBlock == 0) {
      static const uint8_t bootKeys[8] = {
        KEY_ESC, KEY_DELETE, KEY_F1, KEY_F2, KEY_F9, KEY_F10, KEY_F11, KEY_F12
      };
      bootKeyboard().write(bootKeys[_keySelection]);
    } else {
#ifdef DEVICE_M5_CARDPUTER_ADV
      // Enter in Navigate is sent to the host; BACK exits the block.
      bootKeyboard().write(KEY_RETURN);
#else
      static const uint8_t navKeys[5] = {
        KEY_UP_ARROW, KEY_DOWN_ARROW, KEY_RETURN, KEY_LEFT_ARROW, KEY_RIGHT_ARROW
      };
      bootKeyboard().write(navKeys[_keySelection]);
#endif
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

  drawBlockBorder(0);
  for (uint8_t i = 0; i < 8; ++i) drawVirtualKey(i, 0);
#ifdef DEVICE_M5_CARDPUTER_ADV
  drawVirtualKey(0, 1);
#else
  drawBlockBorder(1);
  for (uint8_t i = 0; i < 5; ++i) drawVirtualKey(i, 1);
#endif
  lcd.drawFastHLine(x0, y0 + h - 16, w, TFT_DARKGREY);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
#ifdef DEVICE_M5_CARDPUTER_ADV
  lcd.drawString("[BtnG0] Keyboard", x0 + w / 4, y0 + h - 8);
#elif !defined(DEVICE_HAS_KEYBOARD)
  lcd.drawString("[Hold] Keyboard", x0 + w / 4, y0 + h - 8);
#endif
  lcd.drawString("[Back] Eject", x0 + 3 * w / 4, y0 + h - 8);
}

// A block is outlined while it has focus; individual keys are highlighted only
// after PRESS enters the block.
void BootIsoScreen::drawBlockBorder(uint8_t block) {
  auto& lcd = Uni.Lcd;
  const int x0 = bodyX(), y0 = bodyY(), w = bodyW(), h = bodyH();
  const int top = y0 + 17;
  const int footer = y0 + h - 18;
  const int available = max(40, footer - top - 5);
  const int bootH = (available * 57) / 100;
#ifdef DEVICE_M5_CARDPUTER_ADV
  if (block != 0) return; // Navigate has no enclosing block.
#endif
  const int by = block == 0 ? top : top + bootH + 3;
  const int bh = block == 0 ? bootH : available - bootH - 3;
  const bool focused = !_insideBlock && _activeBlock == block;
  lcd.drawRoundRect(x0 + 3, by, w - 6, bh, 4,
                    focused ? Config.getThemeColor() : TFT_DARKGREY);
}

void BootIsoScreen::drawVirtualKey(uint8_t index, uint8_t block) {
  if (!_mounted) return;
#ifdef DEVICE_M5_CARDPUTER_ADV
  if (block == 1) {
    // A single full-width key, not a second block.
    auto& lcd = Uni.Lcd;
    const int x0 = bodyX(), y0 = bodyY(), w = bodyW(), h = bodyH();
    const int top = y0 + 17;
    const int available = max(40, y0 + h - 18 - top - 5);
    const int bootH = (available * 57) / 100;
    const int navY = top + bootH + 3;
    const int navH = available - bootH - 3;
    const int keyX = x0 + 3;
    const int keyW = w - 6;
    const int keyH = min(27, max(12, navH - 4));
    const int keyY = navY + (navH - keyH) / 2;
    const bool active = _keyboardRelay || (_insideBlock && _activeBlock == 1);
    const bool focused = !_keyboardRelay && !_insideBlock && _activeBlock == 1;
    const uint16_t fill = active ? TFT_BLUE : TFT_DARKGREY;
    lcd.fillRoundRect(keyX, keyY, keyW, keyH, 3, fill);
    lcd.drawRoundRect(keyX, keyY, keyW, keyH, 3,
                      active ? TFT_WHITE : (focused ? Config.getThemeColor() : TFT_LIGHTGREY));
    lcd.setTextSize(1);
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextColor(TFT_WHITE, fill);
    lcd.drawString(_keyboardRelay ? "Keyboard" : "Navigate",
                   keyX + keyW / 2, keyY + keyH / 2);
    return;
  }
#endif
  if (index >= (block == 0 ? 8 : 5)) return;
  auto& lcd = Uni.Lcd;
  const int x0 = bodyX(), y0 = bodyY(), w = bodyW(), h = bodyH();
  const int top = y0 + 17;
  const int available = max(40, y0 + h - 18 - top - 5);
  const int bootH = (available * 57) / 100;
  const int by = block == 0 ? top : top + bootH + 3;
  const int bh = block == 0 ? bootH : available - bootH - 3;
  const int cols = block == 0 ? 4 : 5;
  const int rows = block == 0 ? 2 : 1;
  const int gap = 3;
  const int keyW = (w - 16 - (cols - 1) * gap) / cols;
  const int keyH = min(27, max(12, (bh - 8 - (rows - 1) * gap) / rows));
  const int gridW = cols * keyW + (cols - 1) * gap;
  const int gridH = rows * keyH + (rows - 1) * gap;
  const int x = x0 + (w - gridW) / 2 + (index % cols) * (keyW + gap);
  const int y = by + (bh - gridH) / 2 + (index / cols) * (keyH + gap);
  const bool selected = _insideBlock && _activeBlock == block && _keySelection == index;
  const uint16_t fill = selected ? Config.getThemeColor() : TFT_DARKGREY;
  lcd.fillRoundRect(x, y, keyW, keyH, 3, fill);
  lcd.drawRoundRect(x, y, keyW, keyH, 3, selected ? TFT_WHITE : TFT_LIGHTGREY);
  static const char* bootLabels[8] = {"ESC", "DEL", "F1", "F2", "F9", "F10", "F11", "F12"};
  static const char* navLabels[5] = {"", "", "Select", "", ""};
  lcd.setTextSize(1);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_WHITE, fill);
  if (block == 1 && index != 2) {
    // Draw arrows geometrically to avoid missing font glyphs.
    const int cx = x + keyW / 2;
    const int cy = y + keyH / 2;
    const int reach = min(keyW, keyH) / 4;
    const int wing = max(2, reach / 2);
    const uint16_t ink = TFT_WHITE;
    if (index == 0 || index == 1) {
      const int tipY = cy + (index == 0 ? -reach : reach);
      const int baseY = cy + (index == 0 ? reach : -reach);
      lcd.drawLine(cx, baseY, cx, tipY, ink);
      lcd.drawLine(cx, tipY, cx - wing, tipY + (index == 0 ? wing : -wing), ink);
      lcd.drawLine(cx, tipY, cx + wing, tipY + (index == 0 ? wing : -wing), ink);
    } else {
      const int tipX = cx + (index == 3 ? -reach : reach);
      const int baseX = cx + (index == 3 ? reach : -reach);
      lcd.drawLine(baseX, cy, tipX, cy, ink);
      lcd.drawLine(tipX, cy, tipX + (index == 3 ? wing : -wing), cy - wing, ink);
      lcd.drawLine(tipX, cy, tipX + (index == 3 ? wing : -wing), cy + wing, ink);
    }
  } else {
    lcd.drawString(block == 0 ? bootLabels[index] : navLabels[index],
                   x + keyW / 2, y + keyH / 2);
  }
}

void BootIsoScreen::ejectIso() {
  if (!_mounted) return;
  bootKeyboard().releaseAll();
#ifdef DEVICE_M5_CARDPUTER_ADV
  _keyboardRelay = false;
  Uni.Nav->setSuppressKeys(false);
#endif
  UsbMscUtil::instance().end();
  _mounted = false;
  Screen.goBack();
}

void BootIsoScreen::onBack() {
  if (_mounted) {
    if (_insideBlock) { _insideBlock = false; render(); }
    return;
  }
  Screen.goBack();
}
#endif
