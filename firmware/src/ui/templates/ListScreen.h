#pragma once
#include "BaseScreen.h"

class ListScreen : public BaseScreen
{
public:
  struct ListItem
  {
    const char* label;
    const char* sublabel;
  };

  template <size_t N>
  void setItems(ListItem (&arr)[N])
  {
    _items = arr;
    _count = N;
    _selectedIndex = 0;
    _scrollOffset = 0;
    onRender();
  }

  void setItems(ListItem* arr, uint8_t count)
  {
    _items         = arr;
    _count         = count;
    _selectedIndex = 0;
    _scrollOffset  = 0;
    onRender();
  }

  void onInit() override
  {
    onRender();
  }

  void onUpdate() override
  {
#ifdef DEVICE_HAS_KEYBOARD
    if (Uni.Keyboard && Uni.Keyboard->available()) {
      char c = Uni.Keyboard->getKey();
      if (c == '\b') { onBack(); return; }
    }
#endif

    uint8_t eff = _effectiveCount();
    if (eff == 0) return;

    if (Uni.Nav->wasPressed())
    {
      auto dir = Uni.Nav->readDirection();

      if (dir == INavigation::DIR_UP && _selectedIndex > 0)
      {
        _selectedIndex--;
        _scrollIfNeeded();
        onRender();
      }
      else if (dir == INavigation::DIR_DOWN && _selectedIndex < eff - 1)
      {
        _selectedIndex++;
        _scrollIfNeeded();
        onRender();
      }
      else if (dir == INavigation::DIR_PRESS)
      {
#ifndef DEVICE_HAS_KEYBOARD
        if (_hasBackItem() && _selectedIndex == _count) { onBack(); return; }
#endif
        onItemSelected(_selectedIndex);
      }
    }
  }

  void onRender() override
  {
    uint8_t eff = _effectiveCount();
    if (eff == 0) return;

    auto& lcd = Uni.Lcd;
    lcd.setTextDatum(TL_DATUM);
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);

    uint8_t visible = bodyH() / ITEM_H;

    static const ListItem _backListItem = {"< Back", nullptr};

    for (uint8_t i = 0; i < visible; i++)
    {
      uint8_t idx = i + _scrollOffset;
      if (idx >= eff) break;

      const ListItem* item;
      if (_hasBackItem() && idx == _count)
        item = &_backListItem;
      else
        item = &_items[idx];

      bool selected = (idx == _selectedIndex);
      int16_t itemTop = bodyY() + (i * ITEM_H);
      uint16_t bg = selected ? Config.getThemeColor() : TFT_BLACK;
      uint16_t fg = selected ? TFT_WHITE : TFT_LIGHTGREY;

      if (selected)
      {
        lcd.fillRoundRect(
          bodyX(),
          itemTop + 2,
          bodyW(),
          ITEM_H - 4,
          3,
          TFT_NAVY
        );
      }

      lcd.setTextColor(fg, bg);

      if (item->sublabel)
      {
        lcd.drawString(item->label,
                       bodyX() + 6,
                       itemTop + (ITEM_H / 2) - 4, 1);

        lcd.setTextColor(selected ? TFT_CYAN : TFT_DARKGREY, bg);
        int16_t subX = bodyX() + bodyW() - 6
          - lcd.textWidth(item->sublabel, 1);
        lcd.drawString(item->sublabel,
                       subX,
                       itemTop + (ITEM_H / 2) - 4, 1);
      }
      else
      {
        lcd.drawString(item->label,
                       bodyX() + 6,
                       itemTop + (ITEM_H / 2) - 4, 1);
      }
    }
  }

  virtual void onItemSelected(uint8_t index) = 0;
  virtual void onBack() {}
  virtual bool hasBackItem() { return true; }

protected:
  uint8_t _selectedIndex = 0;

private:
  ListItem* _items = nullptr;
  uint8_t _count = 0;
  uint8_t _scrollOffset = 0;

  static constexpr uint8_t ITEM_H = 22;

  bool _hasBackItem()
  {
#ifdef DEVICE_HAS_KEYBOARD
    return false;
#else
    return hasBackItem();
#endif
  }

  uint8_t _effectiveCount()
  {
    return _count + (_hasBackItem() ? 1 : 0);
  }

  void _scrollIfNeeded()
  {
    uint8_t visible = bodyH() / ITEM_H;
    uint8_t eff     = _effectiveCount();
    if (_selectedIndex < _scrollOffset)
      _scrollOffset = _selectedIndex;
    else if (_selectedIndex >= _scrollOffset + visible)
      _scrollOffset = _selectedIndex - visible + 1;
    (void)eff;
  }
};