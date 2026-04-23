#include "BarcodeScreen.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/utility/UtilityMenuScreen.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/actions/ShowBarcodeAction.h"

void BarcodeScreen::onInit() {
  _state = STATE_MENU;
  _refreshMenu();
  setItems(_menuItems);
}

void BarcodeScreen::onBack() {
  if (_state == STATE_SELECT_FILE) {
    if (_currentPath == _barcodePath) {
      _state = STATE_MENU;
      _refreshMenu();
      setItems(_menuItems);
    } else {
      int last = _currentPath.lastIndexOf('/');
      _currentPath = (last > 0) ? _currentPath.substring(0, last) : String(_barcodePath);
      _scanFiles(_currentPath);
    }
    return;
  }
  Screen.goBack();
}

void BarcodeScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_MENU) {
    switch (index) {
      case 0:
        _mode = (_mode == MODE_WRITE) ? MODE_FILE : MODE_WRITE;
        _refreshMenu();
        render();
        break;
      case 1:
        _inverted = !_inverted;
        _refreshMenu();
        render();
        break;
      case 2:
        if (_mode == MODE_WRITE) {
          _generate();
        } else {
          if (!Uni.Storage || !Uni.Storage->isAvailable()) {
            ShowStatusAction::show("No storage available", 1500);
            render();
            return;
          }
          Uni.Storage->makeDir(_barcodePath);
          _currentPath = _barcodePath;
          _scanFiles(_currentPath);
        }
        break;
    }
  } else if (_state == STATE_SELECT_FILE) {
    if (index >= _fileCount) return;
    String path = _currentPath + "/" + _fileEntries[index].name;
    if (_fileEntries[index].isDir) {
      _currentPath = path;
      _scanFiles(_currentPath);
    } else {
      _generateFromFile(path);
    }
  }
}

void BarcodeScreen::_refreshMenu() {
  _menuItems[0].sublabel = (_mode == MODE_WRITE) ? "Write" : "File";
  _menuItems[1].sublabel = _inverted ? "Yes" : "No";
}

void BarcodeScreen::_generate() {
  String text = InputTextAction::popup("Barcode Content");
  if (text.length() == 0) { render(); return; }
  ShowBarcodeAction::show(text.c_str(), text.c_str(), _inverted);
  int n = Achievement.inc("barcode_generated");
  if (n == 1) Achievement.unlock("barcode_generated");
  render();
}

void BarcodeScreen::_scanFiles(const String& path) {
  _state = STATE_SELECT_FILE;
  _fileCount = 0;

  IStorage::DirEntry entries[MAX_FILES];
  uint8_t count = Uni.Storage->listDir(path.c_str(), entries, MAX_FILES);

  if (count == 0) {
    ShowStatusAction::show("Cannot open directory", 1500);
    _state = STATE_MENU;
    render();
    return;
  }

  for (uint8_t i = 0; i < count; i++) {
    strncpy(_fileEntries[i].name, entries[i].name.c_str(), sizeof(_fileEntries[0].name) - 1);
    _fileEntries[i].name[sizeof(_fileEntries[0].name) - 1] = '\0';
    _fileEntries[i].isDir = entries[i].isDir;
    _fileItems[i] = { _fileEntries[i].name, entries[i].isDir ? "DIR" : "FILE" };
  }
  _fileCount = count;

  setItems(_fileItems, _fileCount);
}

void BarcodeScreen::_generateFromFile(const String& path) {
  String data = Uni.Storage->readFile(path.c_str());
  if (data.isEmpty()) {
    ShowStatusAction::show("Cannot open file", 1500);
    render();
    return;
  }

  // Use first line only — barcodes are single-line
  int nl = data.indexOf('\n');
  if (nl >= 0) data = data.substring(0, nl);
  data.trim();

  if (data.length() == 0) {
    ShowStatusAction::show("File is empty", 1500);
    render();
    return;
  }

  ShowBarcodeAction::show(path.c_str(), data.c_str(), _inverted);
  int n = Achievement.inc("barcode_file_generated");
  if (n == 1) Achievement.unlock("barcode_file_generated");
  render();
}

