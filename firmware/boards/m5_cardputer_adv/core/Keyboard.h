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
    if (!_ready || _available) return;
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

    if (row == 2 && col == 1) { _shift = pressed; return; }
    if (!pressed) return;
    if (n == '\0') return;

    _key       = _shift ? _ADV_KB_MAP[row][col].s : n;
    _available = true;
  }

  bool available() override { return _available; }
  char peekKey()   override { return _key; }

  char getKey() override {
    _available = false;
    return _key;
  }

private:
  char _key       = 0;
  bool _available = false;
  bool _shift     = false;
  bool _ready     = false;
};