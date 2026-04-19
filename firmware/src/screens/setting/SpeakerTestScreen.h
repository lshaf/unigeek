#pragma once

#include "ui/templates/ListScreen.h"

class SpeakerTestScreen : public ListScreen {
public:
  const char* title() override { return "Speaker Test"; }
  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  ListItem _items[4] = {
    {"Play Win"},
    {"Play Lose"},
    {"Play Notification"},
    {"Play Beep"},
  };
};