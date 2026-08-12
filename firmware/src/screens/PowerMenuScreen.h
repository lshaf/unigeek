#pragma once

#include "ui/templates/ListScreen.h"

class PowerMenuScreen : public ListScreen
{
public:
  const char* title() override { return "Power"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;

private:
  enum Action : uint8_t {
    ACTION_DEEP_SLEEP,
    ACTION_POWER_OFF,
  };

  ListItem _items[2];
  Action   _actions[2];
  uint8_t  _count = 0;
};
