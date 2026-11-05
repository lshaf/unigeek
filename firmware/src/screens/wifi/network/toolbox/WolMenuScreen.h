#pragma once
#include <vector>
#include "ui/templates/ListScreen.h"
#include "utils/network/SavedHosts.h"

// Wake-on-LAN front screen: New or one of the saved targets (name + MAC).
// Picking a saved one opens the WoL screen filled in.
class WolMenuScreen : public ListScreen {
public:
  const char* title() override { return "Wake-on-LAN"; }
  void onInit() override;
  void onRestore() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  static constexpr const char* DIR = "/unigeek/wifi/remote/wol";
  static constexpr uint8_t MAX_SAVED = 32;
  enum State : uint8_t { STATE_MENU, STATE_SAVED };
  State    _state = STATE_MENU;
  ListItem _menu[2] = {{"New"}, {"Saved"}};
  std::vector<SavedHost> _hosts;
  ListItem _items[MAX_SAVED];
  String   _details[MAX_SAVED];
  void _showMenu(uint8_t sel = 0);
  void _showSaved();
  void _open(const SavedHost* host);
};
