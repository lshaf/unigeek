#pragma once

#include "core/IKeyboard.h"
#include <driver/gpio.h>

// ─── GPIO matrix (from bruce Keyboard.h) ─────────────────
static constexpr uint8_t _KB_OUT[3] = {8, 9, 11};
static constexpr uint8_t _KB_IN[7]  = {13, 15, 3, 4, 5, 6, 7};
static constexpr uint8_t _KB_X1[7]  = {0, 2, 4,  6,  8, 10, 12};
static constexpr uint8_t _KB_X2[7]  = {1, 3, 5,  7,  9, 11, 13};

// ─── Key map [row][col], row 0 = top ─────────────────────
struct _KbKey { char n; char s; };
static constexpr _KbKey _KB_MAP[4][14] = {
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

class KeyboardImpl : public IKeyboard
{
public:
  void begin() override {
    for (uint8_t p : _KB_OUT) {
      gpio_reset_pin((gpio_num_t)p);
      gpio_set_direction((gpio_num_t)p, GPIO_MODE_OUTPUT);
      gpio_set_pull_mode((gpio_num_t)p, GPIO_PULLUP_PULLDOWN);
      gpio_set_level((gpio_num_t)p, 0);
    }
    for (uint8_t p : _KB_IN) {
      gpio_reset_pin((gpio_num_t)p);
      gpio_set_direction((gpio_num_t)p, GPIO_MODE_INPUT);
      gpio_set_pull_mode((gpio_num_t)p, GPIO_PULLUP_ONLY);
    }
  }

  void update() override {
    if (_available) return;

    // wait for all keys released before accepting next key
    if (_waitRelease) {
      for (int i = 0; i < 8; i++) {
        _setOutput(i);
        if (_readInput()) { _setOutput(0); return; }
      }
      _setOutput(0);
      _waitRelease = false;
    }

    bool shiftSeen = false;

    // first pass: detect shift
    for (int i = 0; i < 8; i++) {
      _setOutput(i);
      uint8_t bits = _readInput();
      if (!bits) continue;
      uint8_t yf = 3 - ((i > 3) ? (i - 4) : i);
      bool    x1 = (i > 3);
      for (int j = 0; j < 7; j++) {
        if (!(bits & (1 << j))) continue;
        uint8_t x = x1 ? _KB_X1[j] : _KB_X2[j];
        if (yf == 2 && x == 1) shiftSeen = true; // SHIFT key
      }
    }
    _shift = shiftSeen;

    // second pass: resolve char
    for (int i = 0; i < 8; i++) {
      _setOutput(i);
      uint8_t bits = _readInput();
      if (!bits) continue;
      uint8_t yf = 3 - ((i > 3) ? (i - 4) : i);
      bool    x1 = (i > 3);
      for (int j = 0; j < 7; j++) {
        if (!(bits & (1 << j))) continue;
        uint8_t x = x1 ? _KB_X1[j] : _KB_X2[j];
        char n = _KB_MAP[yf][x].n;
        if (n == '\0') continue; // modifier key
        char c = _shift ? _KB_MAP[yf][x].s : n;
        _key       = c;
        _available = true;
        _setOutput(0); // restore
        return;
      }
    }
    _setOutput(0); // restore
  }

  bool available() override { return _available; }
  char peekKey()   override { return _key; }

  char getKey() override {
    _available   = false;
    _waitRelease = true;
    return _key;
  }

private:
  char _key        = 0;
  bool _available  = false;
  bool _shift      = false;
  bool _waitRelease = false;

  void _setOutput(int i) {
    gpio_set_level((gpio_num_t)_KB_OUT[0], (i >> 0) & 1);
    gpio_set_level((gpio_num_t)_KB_OUT[1], (i >> 1) & 1);
    gpio_set_level((gpio_num_t)_KB_OUT[2], (i >> 2) & 1);
  }

  uint8_t _readInput() {
    uint8_t bits = 0;
    for (int j = 0; j < 7; j++) {
      if (!gpio_get_level((gpio_num_t)_KB_IN[j]))
        bits |= (1 << j);
    }
    return bits;
  }
};
