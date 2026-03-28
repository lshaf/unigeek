#pragma once

#include "ui/templates/ListScreen.h"

class BarcodeScreen : public ListScreen
{
public:
  const char* title() override { return "Barcode"; }

  void onInit() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  enum State { STATE_MENU, STATE_SELECT_FILE };
  enum Mode  { MODE_WRITE, MODE_FILE };

  State  _state    = STATE_MENU;
  Mode   _mode     = MODE_WRITE;
  bool   _inverted = false;
  String _currentPath;

  static constexpr const char* _barcodePath = "/unigeek/barcode";
  static constexpr int         MAX_FILES    = 40;

  struct FileEntry { char name[64]; bool isDir; };
  FileEntry _fileEntries[MAX_FILES];
  uint8_t   _fileCount = 0;
  ListItem  _fileItems[MAX_FILES];

  ListItem _menuItems[3] = {
    {"Mode",     "Write"},
    {"Inverted", "No"},
    {"Generate"},
  };

  void _refreshMenu();
  void _generate();
  void _scanFiles(const String& path);
  void _generateFromFile(const String& path);
};

