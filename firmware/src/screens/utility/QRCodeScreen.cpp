#include "QRCodeScreen.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/utility/UtilityMenuScreen.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/actions/ShowQRCodeAction.h"

void QRCodeScreen::onInit() {
  _state = STATE_MENU;
  _refreshMenu();
  setItems(_menuItems);
}

void QRCodeScreen::onBack() {
  if (_state == STATE_SELECT_FILE) {
    if (_currentPath == _qrPath) {
      _state = STATE_MENU;
      _refreshMenu();
      setItems(_menuItems);
    } else {
      int last = _currentPath.lastIndexOf('/');
      _currentPath = (last > 0) ? _currentPath.substring(0, last) : String(_qrPath);
      _scanFiles(_currentPath);
    }
    return;
  }
  Screen.goBack();
}

void QRCodeScreen::onItemSelected(uint8_t index) {
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
          Uni.Storage->makeDir(_qrPath);
          _currentPath = _qrPath;
          _scanFiles(_currentPath);
        }
        break;
    }
  } else if (_state == STATE_SELECT_FILE) {
    if (index >= _browser.count()) return;
    if (_browser.entry(index).isDir) {
      _currentPath = _browser.entry(index).path;
      _scanFiles(_currentPath);
    } else {
      _generateFromFile(_browser.entry(index).path);
    }
  }
}

void QRCodeScreen::_refreshMenu() {
  _menuItems[0].sublabel = (_mode == MODE_WRITE) ? "Write" : "File";
  _menuItems[1].sublabel = _inverted ? "Yes" : "No";
}

void QRCodeScreen::_generate() {
  String text = InputTextAction::popup("QR Content");
  if (text.length() == 0) { render(); return; }
  ShowQRCodeAction::show(text.c_str(), text.c_str(), _inverted);
  int n = Achievement.inc("qr_write_generated");
  if (n == 1) Achievement.unlock("qr_write_generated");
  render();
}

void QRCodeScreen::_scanFiles(const String& path) {
  _state = STATE_SELECT_FILE;
  _currentPath = path;
  uint8_t n = _browser.load(this, path, nullptr, "FILE");
  if (n == 0) {
    ShowStatusAction::show("Cannot open directory", 1500);
    _state = STATE_MENU;
    render();
    return;
  }
  setItems(_browser.items(), n);
}

void QRCodeScreen::_generateFromFile(const String& path) {
  String data = Uni.Storage->readFile(path.c_str());
  if (data.isEmpty()) {
    ShowStatusAction::show("Cannot open file", 1500);
    render();
    return;
  }
  if (data.length() > 1800) {
    ShowStatusAction::show("File too large", 1500);
    render();
    return;
  }

  ShowQRCodeAction::show(path.c_str(), data.c_str(), _inverted);
  int n = Achievement.inc("qr_file_generated");
  if (n == 1) Achievement.unlock("qr_file_generated");
  render();
}
