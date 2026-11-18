//
// Created by L Shaf on 2026-02-23.
//

#pragma once
#include <stdint.h>

class IKeyboard
{
public:
  enum Modifier : uint8_t {
    MOD_NONE  = 0,
    MOD_SHIFT = 1 << 0,
    MOD_FN    = 1 << 1,
    MOD_CAPS  = 1 << 2,
    MOD_CTRL  = 1 << 3,
    MOD_ALT   = 1 << 4,
    MOD_OPT   = 1 << 5,
  };

  virtual ~IKeyboard() = default;
  virtual void begin()  = 0;
  virtual void update() = 0;
  virtual bool available() = 0;
  virtual char peekKey()   = 0;  // read without consuming
  virtual char getKey()    = 0;
  virtual uint8_t modifiers() { return MOD_NONE; }
  // Modifiers that were held when the pending key (the one peekKey()/getKey()
  // returns) was pressed. Call before getKey(). Unlike modifiers(), this does
  // not depend on the modifier still being held when the key is consumed, so
  // a quick Ctrl+C is not lost. Injected (web remote) keys carry no modifiers.
  virtual uint8_t keyModifiers() { return _injAvail() ? (uint8_t)MOD_NONE : modifiers(); }
  virtual bool isKeyHeld() const { return false; }  // true while the last consumed key is still physically held

  // Inject a key from the web remote into a small FIFO that impls drain ahead
  // of hardware in available()/peekKey()/getKey(). Because nav and text input
  // share one keyboard instance, injected keys drive both (on Cardputer the
  // Navigation consumes ;./,/\n/\b for direction, InputText reads the rest for
  // typing). No-op on boards without a keyboard (Uni.Keyboard == nullptr).
  void inject(char c) {
    if (_injCount < kInjCap) { _injBuf[_injHead] = c; _injHead = (_injHead + 1) % kInjCap; _injCount++; }
  }

protected:
  // Impls call these at the top of available()/peekKey()/getKey().
  bool _injAvail() const { return _injCount > 0; }
  char _injPeek()  const { return _injCount > 0 ? _injBuf[_injTail] : 0; }
  char _injTake()  { if (!_injCount) return 0; char c = _injBuf[_injTail]; _injTail = (_injTail + 1) % kInjCap; _injCount--; return c; }

private:
  static constexpr uint8_t kInjCap = 16;
  char    _injBuf[kInjCap];
  uint8_t _injHead = 0, _injTail = 0, _injCount = 0;
};