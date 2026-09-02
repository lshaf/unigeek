#pragma once

#include "ui/templates/ListScreen.h"
#include "pins_arduino.h"

class PowerMenuScreen : public ListScreen
{
public:
  const char* title() override { return "Power"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;

private:
  enum Action : uint8_t {
    ACTION_LIGHT_SLEEP,
    ACTION_DEEP_SLEEP,
    ACTION_POWER_OFF,
  };

  ListItem _items[3];
  Action   _actions[3];
  uint8_t  _count = 0;
};
