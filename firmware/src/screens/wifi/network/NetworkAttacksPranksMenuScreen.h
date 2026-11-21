#pragma once

#include "ui/templates/ListScreen.h"

// Attacks first, pranks after. One list so the Network menu does not need a
// second hop just to choose between the two.
class NetworkAttacksPranksMenuScreen : public ListScreen {
public:
  const char* title() override { return "Attacks & Pranks"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
#ifdef HAS_NET_TOOLS
  ListItem _items[7] = {
#else
  ListItem _items[6] = {
#endif
    {"MITM Attack"},
    {"NetCut"},
    {"DHCP Attack"},
#ifdef HAS_NET_TOOLS
    {"Responder"},
#endif
    {"Cast Bomb"},
    {"Bonjour Spam"},
    {"Printer Prank"},
  };
};
