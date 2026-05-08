//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "ui/templates/BaseScreen.h"

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

#if defined(APP_MENU_POWER_OFF) && defined(DEVICE_HAS_TOUCH_NAV)
  static const uint8_t ITEM_COUNT = 10;
#elif defined(APP_MENU_POWER_OFF) || defined(DEVICE_HAS_TOUCH_NAV)
  static const uint8_t ITEM_COUNT = 9;
#else
  static const uint8_t ITEM_COUNT = 8;
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
