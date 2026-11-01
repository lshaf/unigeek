#pragma once

#include "core/IKeyboard.h"
#include "pins_arduino.h"
#include <Wire.h>
#include <Adafruit_TCA8418.h>

// ─── Key map [row][col], row 0 = top (same physical layout as standard) ───
struct _AdvKbKey { char n; char s; };
static constexpr _AdvKbKey _ADV_KB_MAP[4][14] = {
  // row 0: ` 1 2 3 4 5 6 7 8 9 0 - = BS
  {{'`','~'},{'1','!'},{'2','@'},{'3','#'},{'4','$'},{'5','%'},
   {'6','^'},{'7','&'},{'8','*'},{'9','('},{'0',')'},
   {'-','_'},{'=','+'},{'\b','\b'}},
  // row 1: TAB q w e r t y u i o p [ ] backslash
  {{'\t','\t'},{'q','Q'},{'w','W'},{'e','E'},{'r','R'},{'t','T'},
   {'y','Y'},{'u','U'},{'i','I'},{'o','O'},{'p','P'},
   {'[','{'},  {']','}'},{'\\','|'}},
  // row 2: FN SHIFT a s d f g h j k l ; ' ENTER
  {{'\0','\0'},{'\0','\0'},{'a','A'},{'s','S'},{'d','D'},{'f','F'},
   {'g','G'},{'h','H'},{'j','J'},{'k','K'},{'l','L'},
   {';',':'},{'\'','"'},{'\n','\n'}},
  // row 3: CTRL OPT ALT z x c v b n m , . / SPACE
  {{'\0','\0'},{'\0','\0'},{'\0','\0'},{'z','Z'},{'x','X'},{'c','C'},
   {'v','V'},{'b','B'},{'n','N'},{'m','M'},{',','<'},
   {'.','>'},{'/','?'},{' ',' '}},
};

class KeyboardImpl : public IKeyboard, public Adafruit_TCA8418
{
public:
  void begin() override {
    Wire1.begin(KB_I2C_SDA, KB_I2C_SCL);
    Wire1.setClock(400000);
    delay(100);

    if (!Adafruit_TCA8418::begin(KB_I2C_ADDR, &Wire1)) return;

    this->matrix(7, 8);
    this->flush();
    this->enableInterrupts();

    _ready     = true;
    _shift     = false;
    _key       = 0;
    _available = false;
  }

  void update() override {
    if (!_ready) return;
    if (Adafruit_TCA8418::available() == 0) return;

    int raw = this->getEvent();
    if (raw == 0) return;

    bool    pressed = (raw & 0x80) != 0;
    uint8_t value   = (raw & 0x7F);

    uint8_t u = value % 10;
    uint8_t t = value / 10;

    if (u < 1 || u > 8 || t > 6) return;

    uint8_t u0  = u - 1;
    uint8_t row = u0 & 0x03;
    uint8_t col = (t << 1) | (u0 >> 2);

    if (row >= 4 || col >= 14) return;

    char n = _ADV_KB_MAP[row][col].n;

    // Modifiers: always process regardless of _available
    if (row == 2 && col == 0) { _fn   = pressed; return; }
    if (row == 2 && col == 1) { _shift = pressed; return; }
    if (row == 3 && col == 0) { _ctrl  = pressed; return; }
    if (row == 3 && col == 1) { _opt   = pressed; return; }
    if (row == 3 && col == 2) { _alt   = pressed; return; }

    // Release: always process — keeps _keyHeld accurate and unblocks stale keys
    if (!pressed) {
      if (n != '\0') {
        _keyHeld = false;
        if (_available) _available = false;
      }
      return;
    }

    // Press: only buffer if nothing is already pending
    if (_available) return;
    if (n == '\0') return;

    _key       = _shift ? _ADV_KB_MAP[row][col].s : n;
    _keyMods   = modifiers();
    _available = true;
    _keyHeld   = true;
  }

  bool available() override { return _injAvail() || _available; }
  char peekKey()   override { return _injAvail() ? _injPeek() : _key; }
  uint8_t modifiers() override {
    uint8_t m = MOD_NONE;
    if (_shift) m |= MOD_SHIFT;
    if (_fn)    m |= MOD_FN;
    if (_ctrl)  m |= MOD_CTRL;
    if (_alt)   m |= MOD_ALT;
    if (_opt)   m |= MOD_OPT;
    return m;
  }

  uint8_t keyModifiers() override { return _injAvail() ? (uint8_t)MOD_NONE : _keyMods; }

  char getKey() override {
    if (_injAvail()) return _injTake();
    _available = false;
    return _key;
  }

  bool isKeyHeld() const override { return _keyHeld; }

private:
  char _key       = 0;
  uint8_t _keyMods = MOD_NONE;
  bool _available = false;
  bool _shift     = false;
  bool _fn        = false;
  bool _ctrl      = false;
  bool _alt       = false;
  bool _opt       = false;
  bool _ready     = false;
  bool _keyHeld   = false;
};