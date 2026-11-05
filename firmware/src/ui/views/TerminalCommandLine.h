#pragma once

#include "core/Device.h"
#include "core/IKeyboard.h"

class TerminalCommandLine
{
public:
  enum Action : uint8_t {
    ACTION_NONE,
    ACTION_CHANGED,
    ACTION_SUBMIT,
    ACTION_EXIT,
    ACTION_SCROLL_UP,
    ACTION_SCROLL_DOWN,
    ACTION_SCROLL_LEFT,
    ACTION_SCROLL_RIGHT,
    ACTION_CONTROL,      // send controlByte() now, outside the line
  };

  static constexpr int MAX_LEN = 256;

  const String& text() const { return _text; }
  // Byte to send for ACTION_CONTROL (0x01-0x1F).
  uint8_t controlByte() const { return _control; }
  void clear() { _text = ""; }

  Action consume(IKeyboard* keyboard) {
    if (!keyboard || !keyboard->available()) return ACTION_NONE;

    uint8_t modifiers = keyboard->modifiers();
    char c = keyboard->getKey();

    // The Cardputer family uses Fn + ; . , / as the four arrow keys, and
    // Fn + ` as Esc (sent immediately).
    if (modifiers & IKeyboard::MOD_FN) {
      if (c == ';') return ACTION_SCROLL_UP;
      if (c == '.') return ACTION_SCROLL_DOWN;
      if (c == ',') return ACTION_SCROLL_LEFT;
      if (c == '/') return ACTION_SCROLL_RIGHT;
      if (c == '`' || c == '~') {
        _control = 0x1b;
        return ACTION_CONTROL;
      }
    }

    // Ctrl + key is sent immediately as its control character, like the
    // virtual terminal keyboard does (Ctrl+C = 0x03, Ctrl+[ = Esc, ...).
    // Ctrl+C also drops the line being typed.
    if (modifiers & IKeyboard::MOD_CTRL) {
      const char upper = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
      if (upper >= 'A' && upper <= '_') {  // no Ctrl+@ (NUL)
        _control = (uint8_t)(upper & 0x1f);
        if (_control == 0x03) _text = "";
        return ACTION_CONTROL;
      }
      return ACTION_NONE;  // no control character for this key
    }

    if (c == '\b' || c == 0x7f) {
      if (_text.length() > 0) {
        _text.remove(_text.length() - 1);
        return ACTION_CHANGED;
      }
      return ACTION_EXIT;
    }

    if (c == '\n' || c == '\r') return ACTION_SUBMIT;

    if (c >= 0x20 && c <= 0x7e && (int)_text.length() < MAX_LEN) {
      _text += c;
      return ACTION_CHANGED;
    }

    return ACTION_NONE;
  }

  void render(int x, int y, int w, int h, bool remoteClosed, bool keyboardDevice) {
    auto& lcd = Uni.Lcd;
    lcd.fillRect(x, y, w, h, TFT_BLACK);
    lcd.setTextFont(1);
    lcd.setTextSize(1);
    lcd.setTextDatum(TL_DATUM);
    lcd.setTextColor(remoteClosed ? TFT_DARKGREY : TFT_WHITE, TFT_BLACK);

    // Hint convention: obvious actions get no hint (leaving with Back after
    // the session closed); the others use [Press] / [Hold] / [Back], centered
    // in dark grey like the other footers.
    if (remoteClosed) return;  // input line left empty

    if (!keyboardDevice) {
      lcd.setTextDatum(MC_DATUM);
      lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
      lcd.drawString("[Press] Type", x + w / 2, y + h / 2);
      lcd.setTextDatum(TL_DATUM);
      return;
    }

    String line = "> " + _text + "_";

    int maxChars = max(4, (w - 8) / 6);
    if ((int)line.length() > maxChars) {
      if (maxChars > 3) {
        int keep = maxChars - 3;
        line = ">.." + line.substring(line.length() - keep);
      } else {
        line = line.substring(0, maxChars);
      }
    }

    lcd.drawString(line.c_str(), x + 4, y + max(0, (h - 8) / 2));
  }

private:
  String  _text;
  uint8_t _control = 0;
};
