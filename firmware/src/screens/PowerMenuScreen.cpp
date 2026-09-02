#include "PowerMenuScreen.h"
#include "core/Device.h"
#include "core/ConfigManager.h"

void PowerMenuScreen::onInit()
{
  _count = 0;

#ifdef DEVICE_HAS_LIGHT_SLEEP
  _items[_count]    = {"Light Sleep"};
  _actions[_count]  = ACTION_LIGHT_SLEEP;
  _count++;
#endif

#ifdef DEVICE_HAS_DEEP_SLEEP
  _items[_count]   = {"Deep Sleep"};
  _actions[_count] = ACTION_DEEP_SLEEP;
  _count++;
#endif

#ifdef DEVICE_HAS_POWER_OFF
  _items[_count]   = {"Power Off"};
  _actions[_count] = ACTION_POWER_OFF;
  _count++;
#endif

  setItems(_items, _count);
}

void PowerMenuScreen::onItemSelected(uint8_t index)
{
  if (index >= _count) return;

  switch (_actions[index])
  {
    case ACTION_LIGHT_SLEEP: {
      // Preserve configured brightness and use the display abstraction
      // rather than manipulating the PWM-controlled backlight pin directly.
      uint8_t brightness =
        (uint8_t)Config.get(APP_CONFIG_BRIGHTNESS, APP_CONFIG_BRIGHTNESS_DEFAULT).toInt();

      Uni.Lcd.setBrightness(0);
      Uni.Power.lightSleep();
      Uni.Lcd.setBrightness(brightness);
      break;
    }

    case ACTION_DEEP_SLEEP:
      Uni.Power.deepSleep();
      break;

    case ACTION_POWER_OFF:
      Uni.Power.powerOff();
      break;
  }
}
