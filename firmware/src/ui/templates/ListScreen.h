#pragma once
#include "BaseScreen.h"
#include "core/ScreenManager.h"

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
    _items            = arr;
    _count            = N;
    _selectedIndex    = 0;
    _scrollOffset     = 0;
    _partialTopActive = false;
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

  void setItems(ListItem* arr, uint8_t count, uint8_t selectedIdx)
  {
    _items         = arr;
    _count         = count;
    uint8_t eff    = count;
    _selectedIndex = (eff > 0 && selectedIdx < eff) ? selectedIdx : 0;
    _scrollOffset  = 0;
    _partialTopActive = false;
    _scrollIfNeeded();
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
        onItemSelected(_selectedIndex);
      }
    }
  }

  void onRender() override
  {
    uint8_t eff          = _effectiveCount();
    uint8_t fullyVisible = bodyH() / ITEM_H;
    int16_t leftover     = (int16_t)bodyH() - fullyVisible * (int16_t)ITEM_H;
    bool    hasPartial   = leftover >= 5;
    bool    hasScrollbar = eff > fullyVisible;
    int16_t listW        = hasScrollbar ? bodyW() - 4 : bodyW();

    auto& lcd = Uni.Lcd;

    if (eff == 0) {
      lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
      return;
    }

    auto renderRow = [&](uint8_t idx, int16_t screenY, int16_t rowH, int16_t dy) {
      const ListItem* item     = &_items[idx];
      bool     selected = (idx == _selectedIndex);
      uint16_t bg       = selected ? Config.getThemeColor() : TFT_BLACK;
      uint16_t fg       = selected ? TFT_WHITE : TFT_LIGHTGREY;

      Sprite sprite(&lcd);
      sprite.createSprite(listW, rowH);
      sprite.fillSprite(TFT_BLACK);
      sprite.setTextDatum(TL_DATUM);

      if (selected)
        sprite.fillRoundRect(0, 2 + dy, listW, ITEM_H - 4, 3, bg);

      sprite.setTextColor(fg, bg);

      if (item->sublabel)
      {
        sprite.drawString(item->label, 6, (ITEM_H / 2) - 4 + dy);
        sprite.setTextColor(selected ? TFT_CYAN : TFT_DARKGREY, bg);
        int16_t subX = listW - 6 - sprite.textWidth(item->sublabel);
        sprite.drawString(item->sublabel, subX, (ITEM_H / 2) - 4 + dy);
      }
      else
      {
        sprite.drawString(item->label, 6, (ITEM_H / 2) - 4 + dy);
      }

      sprite.pushSprite(bodyX(), bodyY() + screenY);
      sprite.deleteSprite();
    };

    int16_t curY  = 0;
    int16_t usedH = 0;

    bool showPartialTop = hasPartial && _scrollOffset > 0 && _partialTopActive;

    // Scrolled down: show bottom `leftover` px of row above, top clipped.
    if (showPartialTop)
    {
      int16_t dy = -(int16_t)(ITEM_H - leftover);
      renderRow(_scrollOffset - 1, 0, leftover, dy);
      curY = leftover;
    }

    // Full rows.
    uint8_t rendered = 0;
    for (uint8_t i = 0; i < fullyVisible; i++)
    {
      uint8_t idx = i + _scrollOffset;
      if (idx >= eff) break;
      renderRow(idx, curY + i * ITEM_H, ITEM_H, 0);
      rendered++;
    }
    usedH = curY + rendered * ITEM_H;

    // Peek of next row at bottom (when no partial top is shown).
    if (hasPartial && !showPartialTop)
    {
      uint8_t idx = _scrollOffset + rendered;
      if (idx < eff)
      {
        renderRow(idx, usedH, leftover, 0);
        usedH += leftover;
      }
    }

    if (usedH < (int16_t)bodyH())
      lcd.fillRect(bodyX(), bodyY() + usedH, bodyW(), bodyH() - usedH, TFT_BLACK);

    if (hasScrollbar) {
      static constexpr uint8_t SB_W = 3;
      int16_t sbX = bodyX() + bodyW() - SB_W;
      int16_t sbY = bodyY();
      int16_t sbH = bodyH();
      lcd.fillRect(sbX, sbY, SB_W, sbH, 0x2104);
      int16_t thumbH = sbH * (int16_t)fullyVisible / (int16_t)eff;
      if (thumbH < 8) thumbH = 8;
      int16_t thumbY = sbY + ((int16_t)_scrollOffset * (sbH - thumbH)) / (int16_t)(eff - fullyVisible);
      lcd.fillRect(sbX, thumbY, SB_W, thumbH, Config.getThemeColor());
    }
  }

  virtual void onItemSelected(uint8_t index) = 0;
  virtual void onBack() { Screen.goBack(); }

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
  ListItem*     _items            = nullptr;
  uint8_t       _count            = 0;
  uint8_t       _scrollOffset     = 0;
  bool          _partialTopActive = false;

  static constexpr uint8_t ITEM_H = 22;

  uint8_t _effectiveCount()
  {
    return _count;
  }

  void _scrollIfNeeded()
  {
    uint8_t visible = bodyH() / ITEM_H;
    uint8_t eff     = _effectiveCount();
    if (_selectedIndex < _scrollOffset) {
      _scrollOffset     = _selectedIndex;
      _partialTopActive = false;
    } else if (_selectedIndex >= _scrollOffset + visible) {
      _scrollOffset     = _selectedIndex - visible + 1;
      _partialTopActive = true;
    }
    (void)eff;
  }
};