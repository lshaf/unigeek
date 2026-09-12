#pragma once
#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"

class ChameleonMfuToolsScreen : public ListScreen {
public:
  const char* title() override { return "Tag Operations"; }
  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  ListItem _items[4];
  BrowseFileView _browser;
  void _writeTag();
  void _eraseTag();
  void _writeFromFile();
  void _writeFromSlot();
};
