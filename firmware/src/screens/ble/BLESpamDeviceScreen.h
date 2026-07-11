#pragma once

#include "ui/templates/ListScreen.h"

// Device/preset picker for an attack. Writes the chosen index back through
// `out` (-1 = Random / All) and pops, so the config screen can read it.
class BLESpamDeviceScreen : public ListScreen {
public:
  BLESpamDeviceScreen(int attack, int* out) : _attack(attack), _out(out) {}
  const char* title() override { return _title; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  static constexpr int MAX_ITEMS = 40;
  int      _attack;
  int*     _out;
  int      _count = 0;
  char     _title[24] = "Select Device";
  ListItem _items[MAX_ITEMS];
};
