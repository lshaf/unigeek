#pragma once

#include "ui/templates/ListScreen.h"

// Pre-spam config: shows every setting (device, TX power, MAC
// randomization, interval, custom name) for the chosen attack, with a Start
// row at the bottom that launches the run screen.
class BLESpamConfigScreen : public ListScreen {
public:
  explicit BLESpamConfigScreen(int attack) : _attack(attack) {}
  const char* title() override { return _title; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onRestore() override;   // refresh device sublabel after the picker pops
  void onBack() override;

private:
  int      _attack;
  int      _device    = -1;   // -1 = Random / All
  bool     _hasDevice = false;
  int      _count     = 0;
  char     _title[24] = "BLE Spam";
  ListItem _items[8];
  String   _deviceSub, _txSub, _macSub, _advSub, _gapSub, _nameSub;

  void _rebuild();
};
