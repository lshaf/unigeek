#pragma once
#include "ui/templates/ListScreen.h"

class ChameleonMenuScreen : public ListScreen {
public:
  const char* title() override { return "Chameleon Ultra"; }

  void onInit()                      override;
  void onUpdate()                    override;
  void onRender()                    override;
  void onItemSelected(uint8_t index) override;
  void onBack()                      override;

private:
  ListItem _items[6];
  bool     _toScan = false;
};