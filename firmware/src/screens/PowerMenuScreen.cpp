#include "PowerMenuScreen.h"
#include "core/Device.h"

void PowerMenuScreen::onInit()
{
  _count = 0;

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
    case ACTION_DEEP_SLEEP:
      Uni.Power.deepSleep();
      break;

    case ACTION_POWER_OFF:
      Uni.Power.powerOff();
      break;
  }
}
