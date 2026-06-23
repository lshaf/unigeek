#pragma once

#include "ui/templates/ListScreen.h"
#include "screens/module/ModuleRegistry.h"

class ModuleMenuScreen : public ListScreen
{
public:
  const char* title() override { return "Modules"; }

  void onInit() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  // Built fresh on each entry: only the modules not hidden in config are
  // listed, with _ids mapping each visible row back to its ModuleRegistry id.
  ListItem _items[ModuleRegistry::MOD_COUNT];
  uint8_t  _ids[ModuleRegistry::MOD_COUNT];
  uint8_t  _visibleCount = 0;
};
