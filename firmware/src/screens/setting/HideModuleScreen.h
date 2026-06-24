#pragma once

#include "ui/templates/ListScreen.h"
#include "screens/module/ModuleRegistry.h"

// Toggle which entries appear in the Modules menu. State is persisted in the
// APP_CONFIG_HIDDEN_MODULES bitmask so hidden modules stay hidden across reboots.
class HideModuleScreen : public ListScreen
{
public:
  const char* title() override { return "Hide Module"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  void _refresh();

  String   _subs[ModuleRegistry::MOD_COUNT];
  ListItem _items[ModuleRegistry::MOD_COUNT];
};
