//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "ui/templates/BaseScreen.h"
#include "pins_arduino.h"

class MainMenuScreen : public BaseScreen
{
public:
  const char* title() override { return "Main Menu"; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onRestore() override;

  void onBack();
  void onItemSelected(uint8_t index);

private:
  typedef void (*DrawIconFunc)(Sprite& lcd, int16_t x, int16_t y, uint16_t color);

  struct GridItem
  {
    const char* label;
    DrawIconFunc drawIcon;
  };

// MINI_BUILD (4 MB flash boards) compiles the Games menu out entirely, so it
// no longer occupies a grid slot. Keep this in step with the slot indices in
// MainMenuScreen.cpp.
#ifdef MINI_BUILD
#  define APP_MAIN_MENU_GAMES 0
#  define APP_MAIN_MENU_LUA   0
#else
#  define APP_MAIN_MENU_GAMES 1
#  define APP_MAIN_MENU_LUA   1
#endif

// Always present: Wifi, Bluetooth, HID, Modules, Utility, Settings (6), plus
// the optional Lua/Games slots and the trailing Home/Power entries.

#if (defined(DEVICE_HAS_LIGHT_SLEEP) || defined(DEVICE_HAS_DEEP_SLEEP) || defined(DEVICE_HAS_POWER_OFF) || defined(APP_MENU_POWER_OFF)) && defined(DEVICE_HAS_TOUCH_NAV)
  static const uint8_t ITEM_COUNT = 8 + APP_MAIN_MENU_LUA + APP_MAIN_MENU_GAMES;
#elif defined(DEVICE_HAS_LIGHT_SLEEP) || defined(DEVICE_HAS_DEEP_SLEEP) || defined(DEVICE_HAS_POWER_OFF) || defined(APP_MENU_POWER_OFF) || defined(DEVICE_HAS_TOUCH_NAV)
  static const uint8_t ITEM_COUNT = 7 + APP_MAIN_MENU_LUA + APP_MAIN_MENU_GAMES;
#else
  static const uint8_t ITEM_COUNT = 6 + APP_MAIN_MENU_LUA + APP_MAIN_MENU_GAMES;
#endif

  GridItem _items[ITEM_COUNT];

  uint8_t _selectedIndex    = 0;
  uint8_t _scrollOffset     = 0;
  bool    _partialTopActive = false;

  uint8_t  _cols = 1;
  uint8_t  _rows = 1;
  uint8_t  _visibleRows = 1;
  uint16_t _itemH = 40;
  uint16_t _itemW = 54;

  void _calculateLayout();
  void _scrollIfNeeded();
#ifdef DEVICE_HAS_TOUCH_NAV
  int16_t _itemAtTouch(int16_t tx, int16_t ty);
#endif
};
