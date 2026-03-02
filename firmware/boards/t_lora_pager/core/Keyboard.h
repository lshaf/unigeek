//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/IKeyboard.h"
#include <Wire.h>
#include <Adafruit_TCA8418.h>

#define KB_ROWS        4
#define KB_COLS        10
#define KB_KEY_FN      20   // row=2 col=0
#define KB_KEY_SHIFT   28   // row=2 col=8
#define KB_KEY_BACK    29   // row=2 col=9

struct KB_KeyValue_t {
  const char normal;
  const char shifted;
  const char fn;
};

static constexpr KB_KeyValue_t _kb_map[KB_ROWS][KB_COLS] = {
  // row 0
  {
    {'q', 'Q', '1'},
    {'w', 'W', '2'},
    {'e', 'E', '3'},
    {'r', 'R', '4'},
    {'t', 'T', '5'},
    {'y', 'Y', '6'},
    {'u', 'U', '7'},
    {'i', 'I', '8'},
    {'o', 'O', '9'},
    {'p', 'P', '0'},
  },
  // row 1
  {
    {'a', 'A', '*'},
    {'s', 'S', '/'},
    {'d', 'D', '+'},
    {'f', 'F', '-'},
    {'g', 'G', '='},
    {'h', 'H', ':'},
    {'j', 'J', '\''},
    {'k', 'K', '"'},
    {'l', 'L', '@'},
    {'\n','\n', '&'},
  },
  // row 2
  {
    {'\0','\0','\0'},
    {'z', 'Z', '_'},
    {'x', 'X', '$'},
    {'c', 'C', ';'},
    {'v', 'V', '?'},
    {'b', 'B', '!'},
    {'n', 'N', ','},
    {'m', 'M', '.'},
    {'\0','\0','\0'},
    {'\b','\b', '#'},
  },
  // row 3
  {
    {' ', ' ', ' '},
  },
};

class KeyboardImpl : public IKeyboard, public Adafruit_TCA8418
{
public:
  void begin() override {
    Wire.begin(GROVE_SDA, GROVE_SCL);
    Wire.setClock(400000);

    if (!Adafruit_TCA8418::begin(TCA8418_DEFAULT_ADDR, &Wire)) return;

    this->matrix(KB_ROWS, KB_COLS);
    this->flush();

    pinMode(KB_BL, OUTPUT);
    analogWrite(KB_BL, 127);

    _fnPressed = false;
    _shiftHeld = false;
    _capsLock  = false;
    _key       = 0;
    _available = false;
  }

  void update() override {
    if (Adafruit_TCA8418::available() == 0) return;

    int raw = this->getEvent();
    if (raw == 0) return;

    bool pressed = (raw & 0x80) != 0;
    uint8_t k    = (raw & 0x7F) - 1;

    if (k / KB_COLS >= KB_ROWS) return;

    // ── special keys ──────────────────────────────────────
    if (k == KB_KEY_FN) {
      if (pressed) _fnPressed = !_fnPressed;
      return;
    }

    if (k == KB_KEY_SHIFT) {
      _shiftHeld = pressed;
      if (_fnPressed && pressed) _capsLock = !_capsLock;
      return;
    }

    if (!pressed) return;

    // ── resolve character ──────────────────────────────────
    uint8_t row = k / KB_COLS;
    uint8_t col = k % KB_COLS;

    char c;
    if (_fnPressed)                  c = _kb_map[row][col].fn;
    else if (_shiftHeld ^ _capsLock) c = _kb_map[row][col].shifted;
    else                             c = _kb_map[row][col].normal;

    if (c == '\0') return;

    _key       = c;
    _available = true;
  }

  bool available() override { return _available; }
  char peekKey()   override { return _key; }

  char getKey() override {
    char k     = _key;
    _key       = 0;
    _available = false;
    return k;
  }

private:
  char _key       = 0;
  bool _available = false;
  bool _fnPressed = false;
  bool _shiftHeld = false;
  bool _capsLock  = false;
};