//
// Created by L Shaf on 2026-02-23.
//

#include "MainMenuScreen.h"
#include "core/ScreenManager.h"
#include "screens/wifi/WifiMenuScreen.h"
#include "screens/ble/BLEMenuScreen.h"
#include "screens/keyboard/KeyboardMenuScreen.h"
#include "screens/game/GameMenuScreen.h"
#include "screens/module/ModuleMenuScreen.h"
#include "screens/utility/UtilityMenuScreen.h"
#include "screens/setting/SettingScreen.h"
#include "screens/CharacterScreen.h"
#include "ui/components/Icon.h"


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

  _selectedIndex    = 0;
  _scrollOffset     = 0;
  _partialTopActive = false;

  _calculateLayout();
}

void MainMenuScreen::_calculateLayout()
{
  // Measure widest label — item must be at least as wide as text + 2px each side.
  // textWidth() respects the current textSize on Uni.Lcd; force size 1 to match
  // how the sprite draws labels (sprites start at textSize=1 by default).
  Uni.Lcd.setTextSize(1);
  uint16_t maxTextW = 0;
  for (uint8_t i = 0; i < ITEM_COUNT; i++) {
    uint16_t tw = Uni.Lcd.textWidth(_items[i].label);
    if (tw > maxTextW) maxTextW = tw;
  }
  uint16_t minItemW = maxTextW + 4;
  if (minItemW < 30) minItemW = 30; // floor so the 24px icon has room

  // Item: 5px top + 24px icon + 3px gap + 8px text + 4px bottom = 44px
  _itemH = 44;

  _cols = bodyW() / minItemW;
  if (_cols == 0) _cols = 1;
  _itemW = bodyW() / _cols;

  _visibleRows = bodyH() / _itemH;
  if (_visibleRows == 0) _visibleRows = 1;

  _rows = (ITEM_COUNT + _cols - 1) / _cols;
}

void MainMenuScreen::_scrollIfNeeded()
{
  uint8_t currentRow = _selectedIndex / _cols;
  if (currentRow < _scrollOffset) {
    _scrollOffset     = currentRow;
    _partialTopActive = false;
  } else if (currentRow >= _scrollOffset + _visibleRows) {
    _scrollOffset     = currentRow - _visibleRows + 1;
    _partialTopActive = true;
  }
}

void MainMenuScreen::onUpdate() {
  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();

    if (dir == INavigation::DIR_BACK) {
      onBack();
      return;
    }

    constexpr uint8_t eff = ITEM_COUNT;

#ifdef DEVICE_HAS_4WAY_NAV
    if (dir == INavigation::DIR_UP) {
      if (_selectedIndex >= _cols) {
        _selectedIndex -= _cols;
      } else {
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
      } else if (_selectedIndex == eff - 1) {
        _selectedIndex = _selectedIndex % _cols;
      } else {
        _selectedIndex = eff - 1;
      }
      _scrollIfNeeded();
      onRender();
      if (Uni.Speaker) Uni.Speaker->beep();
    }
    else
#endif
    if (dir == INavigation::DIR_LEFT || dir == INavigation::DIR_UP) {
      _selectedIndex = (_selectedIndex == 0) ? eff - 1 : _selectedIndex - 1;
      _scrollIfNeeded();
      onRender();
      if (Uni.Speaker) Uni.Speaker->beep();
    }
    else if (dir == INavigation::DIR_RIGHT || dir == INavigation::DIR_DOWN) {
      _selectedIndex = (_selectedIndex >= eff - 1) ? 0 : _selectedIndex + 1;
      _scrollIfNeeded();
      onRender();
      if (Uni.Speaker) Uni.Speaker->beep();
    }
    else if (dir == INavigation::DIR_PRESS) {
      onItemSelected(_selectedIndex);
    }
  }
}

void MainMenuScreen::onRender() {
  constexpr uint8_t eff = ITEM_COUNT;
  auto& lcd = Uni.Lcd;

  int16_t leftover = (int16_t)bodyH() - _visibleRows * (int16_t)_itemH;
  bool hasPartial  = leftover >= 5;

  auto renderRow = [&](uint8_t rowIdx, int16_t screenY, int16_t rowH, int16_t dy) {
    uint8_t renderedCols = 0;
    for (uint8_t c = 0; c < _cols; c++) {
      uint8_t idx = rowIdx * _cols + c;
      if (idx >= eff) break;

      const GridItem* item = &_items[idx];
      bool     sel = (idx == _selectedIndex);
      uint16_t bg  = sel ? Config.getThemeColor() : TFT_BLACK;
      uint16_t fg  = sel ? TFT_WHITE : TFT_LIGHTGREY;

      Sprite sprite(&lcd);
      sprite.createSprite(_itemW, rowH);
      sprite.fillSprite(TFT_BLACK);
      sprite.setTextDatum(TC_DATUM);

      if (sel) sprite.fillRoundRect(1, 1 + dy, _itemW - 2, _itemH - 2, 3, bg);
      item->drawIcon(sprite, (_itemW - 24) / 2, 5 + dy, fg);
      sprite.setTextColor(fg, bg);
      sprite.drawString(item->label, _itemW / 2, 32 + dy);

      sprite.pushSprite(bodyX() + c * _itemW, bodyY() + screenY);
      sprite.deleteSprite();
      renderedCols++;
    }
    if (renderedCols < _cols)
      lcd.fillRect(bodyX() + renderedCols * _itemW, bodyY() + screenY,
                   bodyW() - renderedCols * _itemW, rowH, TFT_BLACK);
  };

  int16_t curY  = 0;
  int16_t usedH = 0;

  bool showPartialTop = hasPartial && _scrollOffset > 0 && _partialTopActive;

  // Scrolled down: show bottom `leftover` px of row above, top clipped.
  if (showPartialTop) {
    int16_t dy = -(int16_t)(_itemH - leftover);
    renderRow(_scrollOffset - 1, 0, leftover, dy);
    curY = leftover;
  }

  // Full rows.
  uint8_t renderedRows = 0;
  for (uint8_t r = 0; r < _visibleRows; r++) {
    uint8_t rowIdx = r + _scrollOffset;
    if (rowIdx >= _rows) break;
    renderRow(rowIdx, curY + r * _itemH, _itemH, 0);
    renderedRows++;
  }
  usedH = curY + renderedRows * _itemH;

  // Peek of next row at bottom (when no partial top is shown).
  if (hasPartial && !showPartialTop) {
    uint8_t rowIdx = _scrollOffset + renderedRows;
    if (rowIdx < _rows) {
      renderRow(rowIdx, usedH, leftover, 0);
      usedH += leftover;
    }
  }

  if (usedH < (int16_t)bodyH())
    lcd.fillRect(bodyX(), bodyY() + usedH, bodyW(), bodyH() - usedH, TFT_BLACK);
}

void MainMenuScreen::onBack() {
  Screen.setScreen(new CharacterScreen());
}

void MainMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
  case 0: Screen.setScreen(new WifiMenuScreen());     break;
  case 1: Screen.setScreen(new BLEMenuScreen());      break;
  case 2: Screen.setScreen(new KeyboardMenuScreen()); break;
  case 3: Screen.setScreen(new ModuleMenuScreen());   break;
  case 4: Screen.setScreen(new UtilityMenuScreen());  break;
  case 5: Screen.setScreen(new GameMenuScreen());     break;
  case 6: Screen.setScreen(new SettingScreen());      break;
#ifdef APP_MENU_POWER_OFF
  case 7: Uni.Power.powerOff(); break;
#endif
  }
}