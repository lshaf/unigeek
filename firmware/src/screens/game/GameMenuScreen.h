#pragma once

#include <Arduino.h>  // for DEVICE_HAS_SOUND
#include "ui/templates/ListScreen.h"

class GameMenuScreen : public ListScreen
{
public:
  const char* title()    override { return "Games"; }

  void onInit() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
#ifdef DEVICE_HAS_SOUND
  ListItem _items[9] = {
    {"HEX Decoder"},
    {"Wordle EN"},
    {"Wordle ID"},
    {"Flappy Bird"},
    {"Memory Sequence"},
    {"Number Guess"},
    {"Fishing"},
    {"Quenswer"},
    {"Music Composer"},
  };
#else
  ListItem _items[8] = {
    {"HEX Decoder"},
    {"Wordle EN"},
    {"Wordle ID"},
    {"Flappy Bird"},
    {"Memory Sequence"},
    {"Number Guess"},
    {"Fishing"},
    {"Quenswer"},
  };
#endif
};
