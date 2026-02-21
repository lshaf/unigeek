#pragma once
#include "BaseScreen.h"

class ListScreen : public BaseScreen
{
public:
  struct ListItem
  {
    const char* label;
    const char* sublabel; // optional second line, nullptr to hide
  };

  void onInit() override
  {
    _selectedIndex = 0;
    _scrollOffset = 0;
  }

  void onUpdate() override
  {
    if (Uni.Nav->wasPressed())
    {
      auto dir = Uni.Nav->readDirection();

      if (dir == INavigation::DIR_UP)
      {
        if (_selectedIndex > 0)
        {
          _selectedIndex--;
          _scrollIfNeeded();
          render();
        }
      }
      else if (dir == INavigation::DIR_DOWN)
      {
        if (_selectedIndex < itemCount() - 1)
        {
          _selectedIndex++;
          _scrollIfNeeded();
          render();
        }
      }
      else if (dir == INavigation::DIR_PRESS)
      {
        onItemSelected(_selectedIndex);
      }
    }
  }

  void onRender() override
  {
    auto& lcd = Uni.Lcd;
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);

    uint8_t visible = bodyH() / ITEM_H;

    for (uint8_t i = 0; i < visible; i++)
    {
      uint8_t idx = i + _scrollOffset;
      if (idx >= itemCount()) break;

      bool selected = (idx == _selectedIndex);
      uint16_t y = bodyY() + (i * ITEM_H);
      uint16_t bg = selected ? TFT_BLUE : TFT_BLACK;
      uint16_t fg = selected ? TFT_WHITE : TFT_LIGHTGREY;

      lcd.fillRect(bodyX(), y, bodyW(), ITEM_H, bg);
      lcd.setTextColor(fg, bg);

      // main label
      lcd.setTextSize(2);
      lcd.setCursor(bodyX() + 8, y + 6);
      lcd.print(items()[idx].label);

      // sublabel if present
      if (items()[idx].sublabel)
      {
        lcd.setTextSize(1);
        lcd.setTextColor(TFT_DARKGREY, bg);
        lcd.setCursor(bodyX() + 8, y + 22);
        lcd.print(items()[idx].sublabel);
      }

      // divider
      lcd.drawFastHLine(bodyX(), y + ITEM_H - 1, bodyW(), TFT_DARKGREY);
    }
  }

  // subclass must implement these
  virtual ListItem* items() = 0;
  virtual uint8_t itemCount() = 0;
  virtual void onItemSelected(uint8_t index) = 0;

protected:
  uint8_t _selectedIndex = 0;
  uint8_t _scrollOffset = 0;

  static constexpr uint8_t ITEM_H = 36;

private:
  void _scrollIfNeeded()
  {
    uint8_t visible = bodyH() / ITEM_H;
    if (_selectedIndex < _scrollOffset)
      _scrollOffset = _selectedIndex;
    else if (_selectedIndex >= _scrollOffset + visible)
      _scrollOffset = _selectedIndex - visible + 1;
  }
};
