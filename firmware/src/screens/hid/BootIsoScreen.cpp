#include "BootIsoScreen.h"
#ifdef DEVICE_HAS_USB_HID
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
  _keySelection = 0;
  _activeBlock = 0;
  _insideBlock = false;
  _backHoldTriggered = false;
  _mounted = true;
  render();
}

void BootIsoScreen::onUpdate() {
  if (!_mounted) { ListScreen::onUpdate(); return; }

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
      drawBlockBorder(previousBlock);
      drawBlockBorder(_activeBlock);
    } else {
      const uint8_t count = _activeBlock == 0 ? 8 : 5;
      const uint8_t previous = _keySelection;
      _keySelection = (_keySelection + (forward ? 1 : count - 1)) % count;
      drawVirtualKey(previous, _activeBlock);
      drawVirtualKey(_keySelection, _activeBlock);
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
      static const uint8_t navKeys[5] = {
        KEY_UP_ARROW, KEY_DOWN_ARROW, KEY_RETURN, KEY_LEFT_ARROW, KEY_RIGHT_ARROW
      };
      bootKeyboard().write(navKeys[_keySelection]);
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
  drawBlockBorder(1);
  for (uint8_t i = 0; i < 8; ++i) drawVirtualKey(i, 0);
  for (uint8_t i = 0; i < 5; ++i) drawVirtualKey(i, 1);
  lcd.drawFastHLine(x0, y0 + h - 16, w, TFT_DARKGREY);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
#ifndef DEVICE_HAS_KEYBOARD
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
  const int by = block == 0 ? top : top + bootH + 3;
  const int bh = block == 0 ? bootH : available - bootH - 3;
  const bool focused = !_insideBlock && _activeBlock == block;
  lcd.drawRoundRect(x0 + 3, by, w - 6, bh, 4,
                    focused ? Config.getThemeColor() : TFT_DARKGREY);
}

void BootIsoScreen::drawVirtualKey(uint8_t index, uint8_t block) {
  if (!_mounted) return;
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
