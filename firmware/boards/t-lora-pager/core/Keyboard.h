//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/IKeyboard.h"
#include <Wire.h>

// TCA8418 registers
#define TCA8418_ADDR        0x34
#define TCA8418_REG_CFG     0x01
#define TCA8418_REG_INT_STAT 0x02
#define TCA8418_REG_KEY_LCK_EC 0x03
#define TCA8418_REG_KEY_EVENT 0x04

class KeyboardImpl : public IKeyboard
{
public:
  void begin() override
  {
    Wire.begin(GROVE_SDA, GROVE_SCL);
    Wire.setClock(400000);

    // enable key events, auto-increment
    _writeReg(TCA8418_REG_CFG, 0x3E);
  }

  void update() override
  {
    // check if key event available
    uint8_t eventCount = _readReg(TCA8418_REG_KEY_LCK_EC) & 0x0F;
    if (eventCount == 0)
    {
      _key = 0;
      _available = false;
      return;
    }

    uint8_t event = _readReg(TCA8418_REG_KEY_EVENT);
    bool pressed = (event & 0x80); // bit7: 1=press, 0=release

    if (!pressed)
    {
      // only fire on release, same as INavigation wasPressed
      _key = _mapKey(event & 0x7F);
      _available = (_key != 0);
    }

    // clear interrupt
    _writeReg(TCA8418_REG_INT_STAT, 0x01);
  }

  bool available() override { return _available; }

  char getKey() override
  {
    char k = _key;
    _key = 0;
    _available = false;
    return k;
  }

private:
  char _key = 0;
  bool _available = false;

  char _mapKey(uint8_t code)
  {
    // TCA8418 key matrix map for T-Lora Pager QWERTY layout
    // rows 0-6, cols 0-9
    static const char keymap[70] = {
      'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',
      'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', '\b',
      '\0', 'z', 'x', 'c', 'v', 'b', 'n', 'm', '\0', '\n',
      '\0', '\0', '\0', ' ', ' ', ' ', '\0', '\0', '\0', '\0',
      '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
      '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
      '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0',
    };
    if (code == 0 || code > 70) return 0;
    return keymap[code - 1];
  }

  uint8_t _readReg(uint8_t reg)
  {
    Wire.beginTransmission(TCA8418_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)TCA8418_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0;
  }

  void _writeReg(uint8_t reg, uint8_t val)
  {
    Wire.beginTransmission(TCA8418_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
  }
};
