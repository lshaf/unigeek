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
    if (_available) {
      // Key buffered but nobody consumed it — clear if physically released
      if (!_anyNonModHeld()) _available = false;
      if (_available) return;
    }

    // wait for all non-modifier keys released before accepting next key
    if (_waitRelease) {
      if (_anyNonModHeld()) return;
      _waitRelease = false;
    }

    bool shiftSeen = false, fnSeen = false, ctrlSeen = false, altSeen = false, optSeen = false;

    // first pass: detect modifiers
    for (int i = 0; i < 8; i++) {
      _setOutput(i);
      uint8_t bits = _readInput();
      if (!bits) continue;
      uint8_t yf = 3 - ((i > 3) ? (i - 4) : i);
      bool    x1 = (i > 3);
      for (int j = 0; j < 7; j++) {
        if (!(bits & (1 << j))) continue;
        uint8_t x = x1 ? _KB_X1[j] : _KB_X2[j];
        if (yf == 2 && x == 0) fnSeen    = true;
        if (yf == 2 && x == 1) shiftSeen = true;
        if (yf == 3 && x == 0) ctrlSeen  = true;
        if (yf == 3 && x == 1) optSeen   = true;
        if (yf == 3 && x == 2) altSeen   = true;
      }
    }
    _shift = shiftSeen;
    _fn    = fnSeen;
    _ctrl  = ctrlSeen;
    _alt   = altSeen;
    _opt   = optSeen;

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
        _keyMods   = modifiers();
        _available = true;
        _setOutput(0); // restore
        return;
      }
    }
    _setOutput(0); // restore
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
    _available   = false;
    _waitRelease = true;
    return _key;
  }

  bool isKeyHeld() const override { return _waitRelease; }

private:
  char _key        = 0;
  uint8_t _keyMods = MOD_NONE;
  bool _available  = false;
  bool _shift      = false;
  bool _fn         = false;
  bool _ctrl       = false;
  bool _alt        = false;
  bool _opt        = false;
  bool _waitRelease = false;

  bool _anyNonModHeld() {
    for (int i = 0; i < 8; i++) {
      _setOutput(i);
      uint8_t bits = _readInput();
      if (!bits) continue;
      uint8_t yf = 3 - ((i > 3) ? (i - 4) : i);
      bool    x1 = (i > 3);
      for (int j = 0; j < 7; j++) {
        if (!(bits & (1 << j))) continue;
        uint8_t x = x1 ? _KB_X1[j] : _KB_X2[j];
        if (_KB_MAP[yf][x].n != '\0') { _setOutput(0); return true; }
      }
    }
    _setOutput(0);
    return false;
  }

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
