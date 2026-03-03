// cpp
#include <Arduino.h>

#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/ConfigManager.h"

#include "screens/MainMenuScreen.h"

void _checkStorageFallback() {
  if (Uni.Storage && !Uni.Storage->isAvailable() && Uni.StorageLFS)
    Uni.Storage = Uni.StorageLFS;
}

void setup() {
  Serial.begin(115200);
  Uni.begin();
  _checkStorageFallback();
  Config.load(Uni.Storage);
  Uni.applyNavMode();
  Uni.Lcd.setBrightness((uint8_t)Config.get(APP_CONFIG_BRIGHTNESS, APP_CONFIG_BRIGHTNESS_DEFAULT).toInt());
  if (Uni.Speaker) Uni.Speaker->setVolume((uint8_t)Config.get(APP_CONFIG_VOLUME, APP_CONFIG_VOLUME_DEFAULT).toInt());
  Screen.setScreen(new MainMenuScreen());
}

void loop() {
  Uni.update();

  // ── Activity detection (non-consuming) ────────────────────────────────────
  static bool          _lcdOff    = false;
  static unsigned long _lastActive = millis();

  bool active = Uni.Nav->isPressed();
#ifdef DEVICE_HAS_KEYBOARD
  if (Uni.Keyboard && Uni.Keyboard->available()) active = true;
#endif
  if (active) _lastActive = millis();

  // ── Power saving ──────────────────────────────────────────────────────────
  IScreen* _cur       = Screen.current();
  bool _psInhibit     = _cur && _cur->inhibitPowerSave();
  bool _poInhibit     = _psInhibit || (_cur && _cur->inhibitPowerOff());

  if (!_psInhibit && Config.get(APP_CONFIG_ENABLE_POWER_SAVING, APP_CONFIG_ENABLE_POWER_SAVING_DEFAULT).toInt()) {
    unsigned long idle    = millis() - _lastActive;
    unsigned long dispMs  = (unsigned long)Config.get(APP_CONFIG_INTERVAL_DISPLAY_OFF, APP_CONFIG_INTERVAL_DISPLAY_OFF_DEFAULT).toInt() * 1000UL;
    unsigned long powerMs = dispMs + (unsigned long)Config.get(APP_CONFIG_INTERVAL_POWER_OFF, APP_CONFIG_INTERVAL_POWER_OFF_DEFAULT).toInt() * 1000UL;

    if (!_lcdOff && idle > dispMs) {
      Uni.Lcd.setBrightness(0);
      _lcdOff = true;
    } else if (_lcdOff && idle <= dispMs) {
      Uni.Lcd.setBrightness((uint8_t)Config.get(APP_CONFIG_BRIGHTNESS, APP_CONFIG_BRIGHTNESS_DEFAULT).toInt());
      _lcdOff = false;
      // Consume wake-up event then skip this frame entirely so Screen.update()
      // never sees it — avoids race between isPressed() peek and wasPressed() consume.
      Uni.Nav->wasPressed(); Uni.Nav->readDirection();
#ifdef DEVICE_HAS_KEYBOARD
      if (Uni.Keyboard && Uni.Keyboard->available()) Uni.Keyboard->getKey();
#endif
      return;
    }

    if (!_poInhibit && _lcdOff && idle > powerMs) {
      Uni.Power.powerOff();
    }
  } else if (_lcdOff) {
    // power saving disabled or inhibited while display was sleeping — restore brightness
    Uni.Lcd.setBrightness((uint8_t)Config.get(APP_CONFIG_BRIGHTNESS, APP_CONFIG_BRIGHTNESS_DEFAULT).toInt());
    _lcdOff = false;
  }

  // ── Screen update (skipped while display is off) ──────────────────────────
  if (!_lcdOff) {
    Screen.update();
  }
}
