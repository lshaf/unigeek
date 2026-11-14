#pragma once

#include "ui/templates/ListScreen.h"

// Interference tools for the joined network. Kept one level down so the
// Network menu matches the other modules: observation first, then this.
class NetworkAttacksPranksMenuScreen : public ListScreen {
public:
  const char* title() override { return "Attacks & Pranks"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  ListItem _items[2] = {
    {"Attacks"},
    {"Pranks"},
  };
};
