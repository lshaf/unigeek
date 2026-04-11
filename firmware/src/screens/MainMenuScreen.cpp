//
// Created by L Shaf on 2026-02-23.
//

#include "MainMenuScreen.h"
#include "screens/wifi/WifiMenuScreen.h"
#include "screens/ble/BLEMenuScreen.h"
#include "screens/keyboard/KeyboardMenuScreen.h"
#include "screens/game/GameMenuScreen.h"
#include "screens/module/ModuleMenuScreen.h"
#include "screens/utility/UtilityMenuScreen.h"
#include "screens/setting/SettingScreen.h"
#include "screens/CharacterScreen.h"
#include "ui/components/Icon.h"

static void drawBackIcon(TFT_eSPI& lcd, int16_t x, int16_t y, bool active) {
  uint16_t color = active ? TFT_CYAN : TFT_WHITE;
  lcd.drawLine(x + 14, y + 4, x + 8, y + 10, color);
  lcd.drawLine(x + 8, y + 10, x + 14, y + 16, color);
  lcd.drawLine(x + 15, y + 4, x + 9, y + 10, color);
  lcd.drawLine(x + 9, y + 10, x + 15, y + 16, color);
}

void MainMenuScreen::onInit() {
  _items[0] = {"Wifi", Icons::drawWifi};
  _items[1] = {"Bluetooth", Icons::drawBluetooth};
  _items[2] = {"Keyboard", Icons::drawKeyboard};
  _items[3] = {"Modules", Icons::drawModule};
  _items[4] = {"Utility", Icons::drawUtility};
  _items[5] = {"Games", Icons::drawGame};
  _items[6] = {"Settings", Icons::drawSetting};
#ifdef APP_MENU_POWER_OFF
  _items[7] = {"Power Off", Icons::drawPower};
#endif

  _selectedIndex = 0;
  _scrollOffset = 0;

  _calculateLayout();
}

bool MainMenuScreen::_hasBackItem()
{
#ifdef DEVICE_HAS_KEYBOARD
  return false;
#else
#ifdef DEVICE_HAS_NAV_MODE_SWITCH
  if (Config.get(APP_CONFIG_NAV_MODE, APP_CONFIG_NAV_MODE_DEFAULT) == "encoder")
    return false;
#endif
  return true;
#endif
}

uint8_t MainMenuScreen::_effectiveCount()
{
  return ITEM_COUNT + (_hasBackItem() ? 1 : 0);
}

void MainMenuScreen::_calculateLayout()
{
  uint16_t itemW = 54; // Width needed for a 24x24 icon + label
  uint16_t itemH = 46; // Height needed for a 24x24 icon + label + padding

  _cols = bodyW() / itemW;
  if (_cols == 0) _cols = 1;

  _visibleRows = bodyH() / itemH;
  if (_visibleRows == 0) _visibleRows = 1;

  _rows = (_effectiveCount() + _cols - 1) / _cols;
}

void MainMenuScreen::_scrollIfNeeded()
{
  uint8_t currentRow = _selectedIndex / _cols;
  if (currentRow < _scrollOffset) {
    _scrollOffset = currentRow;
  } else if (currentRow >= _scrollOffset + _visibleRows) {
    _scrollOffset = currentRow - _visibleRows + 1;
  }
}

void MainMenuScreen::onUpdate() {
  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();

    if (dir == INavigation::DIR_BACK) {
      onBack();
      return;
    }

    uint8_t eff = _effectiveCount();
    if (eff == 0) return;

    if (dir == INavigation::DIR_UP) {
      if (_selectedIndex >= _cols) {
        _selectedIndex -= _cols;
      } else {
        // Wrap to the bottom column if possible
        uint8_t bottomRow = (_rows - 1);
        uint8_t newIndex = bottomRow * _cols + _selectedIndex;
        if (newIndex >= eff) newIndex -= _cols;
        _selectedIndex = newIndex;
      }
      _scrollIfNeeded();
      onRender();
      if (Uni.Speaker) Uni.Speaker->beep();
    }
    else if (dir == INavigation::DIR_DOWN) {
      uint8_t nextIndex = _selectedIndex + _cols;
      if (nextIndex < eff) {
        _selectedIndex = nextIndex;
      } else {
        // Wrap to the top column
        _selectedIndex = _selectedIndex % _cols;
      }
      _scrollIfNeeded();
      onRender();
      if (Uni.Speaker) Uni.Speaker->beep();
    }
    else if (dir == INavigation::DIR_LEFT) {
      _selectedIndex = (_selectedIndex == 0) ? eff - 1 : _selectedIndex - 1;
      _scrollIfNeeded();
      onRender();
      if (Uni.Speaker) Uni.Speaker->beep();
    }
    else if (dir == INavigation::DIR_RIGHT) {
      _selectedIndex = (_selectedIndex >= eff - 1) ? 0 : _selectedIndex + 1;
      _scrollIfNeeded();
      onRender();
      if (Uni.Speaker) Uni.Speaker->beep();
    }
    else if (dir == INavigation::DIR_PRESS) {
#ifndef DEVICE_HAS_KEYBOARD
      if (_hasBackItem() && _selectedIndex == ITEM_COUNT) { onBack(); return; }
#endif
      onItemSelected(_selectedIndex);
    }
  }
}

void MainMenuScreen::onRender() {
  uint8_t eff = _effectiveCount();

  auto& lcd = Uni.Lcd;
  TFT_eSprite sprite(&lcd);
  sprite.createSprite(bodyW(), bodyH());
  sprite.fillSprite(TFT_BLACK);

  if (eff == 0) {
    sprite.pushSprite(bodyX(), bodyY());
    sprite.deleteSprite();
    return;
  }

  uint16_t itemW = bodyW() / _cols;
  uint16_t itemH = 46;

  static const GridItem _backGridItem = {"Back", drawBackIcon};

  for (uint8_t r = 0; r < _visibleRows; r++) {
    uint8_t rowIdx = r + _scrollOffset;
    if (rowIdx >= _rows) break;

    for (uint8_t c = 0; c < _cols; c++) {
      uint8_t idx = rowIdx * _cols + c;
      if (idx >= eff) break;

      const GridItem* item;
      if (_hasBackItem() && idx == ITEM_COUNT)
        item = &_backGridItem;
      else
        item = &_items[idx];

      bool selected = (idx == _selectedIndex);
      int16_t itemX = c * itemW;
      int16_t itemY = r * itemH;

      uint16_t bg = selected ? Config.getThemeColor() : TFT_BLACK;
      uint16_t fg = selected ? TFT_WHITE : TFT_LIGHTGREY;

      if (selected) {
        sprite.fillRoundRect(itemX + 2, itemY + 2, itemW - 4, itemH - 4, 4, bg);
      }

      int16_t iconX = itemX + (itemW - 24) / 2;
      int16_t iconY = itemY + 6;

      item->drawIcon(sprite, iconX, iconY, selected);

      sprite.setTextColor(fg, bg);
      sprite.setTextDatum(TC_DATUM);
      sprite.drawString(item->label, itemX + itemW / 2, itemY + 32, 1);
    }
  }

  sprite.pushSprite(bodyX(), bodyY());
  sprite.deleteSprite();
}

void MainMenuScreen::onBack() {
  Screen.setScreen(new CharacterScreen());
}

void MainMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
  case 0: Screen.setScreen(new WifiMenuScreen());      break;
  case 1: Screen.setScreen(new BLEMenuScreen());       break;
  case 2: Screen.setScreen(new KeyboardMenuScreen());  break;
  case 3: Screen.setScreen(new ModuleMenuScreen());    break;
  case 4: Screen.setScreen(new UtilityMenuScreen());   break;
  case 5: Screen.setScreen(new GameMenuScreen());      break;
  case 6: Screen.setScreen(new SettingScreen());       break;
#ifdef APP_MENU_POWER_OFF
  case 7: Uni.Power.powerOff(); break;
#endif
  }
}