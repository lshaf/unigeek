#pragma once

#include "ui/templates/ListScreen.h"

class GameMenuScreen : public ListScreen
{
public:
  const char* title()    override { return "Games"; }

  void onInit() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  ListItem _items[7] = {
    {"HEX Decoder"},
    {"Wordle EN"},
    {"Wordle ID"},
    {"Flappy Bird"},
    {"Memory Sequence"},
    {"Number Guess"},
    {"Fishing"},
  };
};
