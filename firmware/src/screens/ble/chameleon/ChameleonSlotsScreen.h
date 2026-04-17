#pragma once
#include "ui/templates/ListScreen.h"

class ChameleonSlotsScreen : public ListScreen {
public:
  const char* title() override { return "Slot Manager"; }

  void onInit()                      override;
  void onItemSelected(uint8_t index) override;
  void onBack()                      override;

private:
  static constexpr int kSlots = 8;

  ListItem _items[kSlots];
  char     _labels[kSlots][20];
  char     _sublabels[kSlots][28];
  uint8_t  _activeSlot = 0;

  void _load();
};
