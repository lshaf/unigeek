#pragma once
#include "ui/templates/ListScreen.h"
#include "core/ScreenManager.h"

class ChameleonMfcAttacksScreen : public ListScreen {
public:
  const char* title() override { return "Attacks"; }

  void onInit()                      override;
  void onItemSelected(uint8_t index) override;
  void onBack()                      override { Screen.goBack(); }

private:
  ListItem _items[3];
};
