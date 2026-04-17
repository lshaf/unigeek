#pragma once
#include "BaseScreen.h"
#include "utils/NavCapabilities.h"

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
    render();
  }

  void setItems(ListItem* arr, uint8_t count)
  {
    _items         = arr;
    _count         = count;
    _selectedIndex = 0;
    _scrollOffset  = 0;
    render();
  }

  void onInit() override
  {
    render();
  }

  void onUpdate() override
  {
    if (Uni.Nav->wasPressed())
    {
      auto dir = Uni.Nav->readDirection();

      if (dir == INavigation::DIR_BACK)
      {
        onBack();
        return;
      }

      uint8_t eff = _effectiveCount();
      if (eff == 0) return;

      if (dir == INavigation::DIR_UP)
      {
        _selectedIndex = (_selectedIndex == 0) ? eff - 1 : _selectedIndex - 1;
        _scrollIfNeeded();
        onRender();
        if (Uni.Speaker) Uni.Speaker->beep();
      }
      else if (dir == INavigation::DIR_DOWN)
      {
        _selectedIndex = (_selectedIndex >= eff - 1) ? 0 : _selectedIndex + 1;
        _scrollIfNeeded();
        onRender();
        if (Uni.Speaker) Uni.Speaker->beep();
      }
      else if (dir == INavigation::DIR_LEFT)
      {
        uint8_t page = bodyH() / ITEM_H;
        _selectedIndex = (_selectedIndex >= page) ? _selectedIndex - page : 0;
        _scrollIfNeeded();
        onRender();
        if (Uni.Speaker) Uni.Speaker->beep();
      }
      else if (dir == INavigation::DIR_RIGHT)
      {
        uint8_t page = bodyH() / ITEM_H;
        uint8_t last = eff - 1;
        _selectedIndex = (_selectedIndex + page <= last) ? _selectedIndex + page : last;
        _scrollIfNeeded();
        onRender();
        if (Uni.Speaker) Uni.Speaker->beep();
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
    uint8_t eff          = _effectiveCount();
    uint8_t fullyVisible = bodyH() / ITEM_H;
    uint8_t leftover     = bodyH() - fullyVisible * ITEM_H;
    // If at least ~5px remain below the last full row, render a partial last
    // item (scroll boundary still uses fullyVisible so user can scroll it
    // fully into view — matching ScrollListView behavior).
    uint8_t maxRows      = fullyVisible + (leftover >= 5 ? 1 : 0);

    auto& lcd = Uni.Lcd;

    if (eff == 0) {
      lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
      return;
    }

    static const ListItem _backListItem = {"< Back", nullptr};

    int16_t usedH = 0;
    for (uint8_t i = 0; i < maxRows; i++)
    {
      uint8_t idx = i + _scrollOffset;
      if (idx >= eff) break;

      int16_t rowY    = i * ITEM_H;
      int16_t remain  = (int16_t)bodyH() - rowY;
      if (remain <= 0) break;
      int16_t rowH    = (remain < ITEM_H) ? remain : ITEM_H;

      const ListItem* item;
      if (_hasBackItem() && idx == _count)
        item = &_backListItem;
      else
        item = &_items[idx];

      bool     selected = (idx == _selectedIndex);
      uint16_t bg       = selected ? Config.getThemeColor() : TFT_BLACK;
      uint16_t fg       = selected ? TFT_WHITE : TFT_LIGHTGREY;

      Sprite sprite(&lcd);
      sprite.createSprite(bodyW(), rowH);
      sprite.fillSprite(TFT_BLACK);
      sprite.setTextDatum(TL_DATUM);

      if (selected)
      {
        sprite.fillRoundRect(0, 2, bodyW(), ITEM_H - 4, 3, bg);
      }

      sprite.setTextColor(fg, bg);

      if (item->sublabel)
      {
        sprite.drawString(item->label, 6, (ITEM_H / 2) - 4);
        sprite.setTextColor(selected ? TFT_CYAN : TFT_DARKGREY, bg);
        int16_t subX = bodyW() - 6 - sprite.textWidth(item->sublabel);
        sprite.drawString(item->sublabel, subX, (ITEM_H / 2) - 4);
      }
      else
      {
        sprite.drawString(item->label, 6, (ITEM_H / 2) - 4);
      }

      sprite.pushSprite(bodyX(), bodyY() + rowY);
      sprite.deleteSprite();
      usedH += rowH;
    }

    // Clear only the unused rows below the last item.
    if (usedH < (int16_t)bodyH())
      lcd.fillRect(bodyX(), bodyY() + usedH, bodyW(), bodyH() - usedH, TFT_BLACK);
  }

  virtual void onItemSelected(uint8_t index) = 0;
  virtual void onBack() {}

protected:
  uint8_t _selectedIndex = 0;

  // Update only the count after in-place array edits (SettingScreen pattern).
  // Clamps selection and adjusts scroll — does NOT call render(). Caller must.
  void setCount(uint8_t count)
  {
    _count = count;
    uint8_t eff = _effectiveCount();
    if (eff > 0 && _selectedIndex >= eff) _selectedIndex = eff - 1;
    _scrollIfNeeded();
  }

private:
  ListItem*     _items        = nullptr;
  uint8_t       _count        = 0;
  uint8_t       _scrollOffset = 0;

  static constexpr uint8_t ITEM_H = 22;

  bool _hasBackItem() { return NavCapabilities::hasBackItem(); }

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