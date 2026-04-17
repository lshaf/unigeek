#pragma once
#include "ui/templates/ListScreen.h"

class ChameleonMfkey32Screen : public ListScreen {
public:
  const char* title() override { return "MFKey32 Log"; }

  void onInit()                      override;
  void onItemSelected(uint8_t index) override;
  void onBack()                      override;

private:
  ListItem _items[4];
  char     _subs[4][16];
  bool     _logEnabled = false;
  uint32_t _count      = 0;

  void _load();
  void _refresh();
  void _toggle();
  void _dumpToSd();
};
