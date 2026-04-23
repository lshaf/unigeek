#include "WigleScreen.h"
#include <WiFi.h>
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/network/NetworkMenuScreen.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"

void WigleScreen::onInit() {
  int n = Achievement.inc("wifi_wigle_visit");
  if (n == 1) Achievement.unlock("wifi_wigle_visit");
  _showMenu();
}

void WigleScreen::onUpdate() {
  if (_state == STATE_STATS) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        _showMenu();
      } else {
        _statsView.onNav(dir);
      }
    }
    return;
  }
  ListScreen::onUpdate();
}

void WigleScreen::onRender() {
  if (_state == STATE_STATS) {
    _statsView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  ListScreen::onRender();
}

void WigleScreen::onBack() {
  if (_state == STATE_UPLOAD) {
    _showMenu();
    return;
  }
  Screen.goBack();
}

void WigleScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_MENU) {
    switch (index) {
      case 0:
        _editToken();
        render();
        break;
      case 1:
        _showStats();
        render();
        break;
      case 2:
        _showUploadMenu();
        render();
        break;
    }
  } else if (_state == STATE_UPLOAD) {
    _uploadFile(index);
    render();
  }
}

void WigleScreen::_showMenu() {
  _state = STATE_MENU;
  _tokenSub = WigleUtil::tokenSublabel(Uni.Storage);
  _menuItems[0] = {"Wigle Token", _tokenSub.c_str()};
  setItems(_menuItems);
}

void WigleScreen::_editToken() {
  String current = WigleUtil::readToken(Uni.Storage);
  String token = InputTextAction::popup("Wigle API Token", current);
  if (token.length() == 0) return;
  token.trim();
  WigleUtil::saveToken(Uni.Storage, token);
  _tokenSub = WigleUtil::tokenSublabel(Uni.Storage);
  _menuItems[0] = {"Wigle Token", _tokenSub.c_str()};
  ShowStatusAction::show("Token saved");
}

void WigleScreen::_showStats() {
  uint8_t count = WigleUtil::fetchStats(_statsRows, WigleUtil::MAX_STAT_ROWS);
  if (count == 0) return;

  _statsView.setRows(_statsRows, count);
  _state = STATE_STATS;
  render();
}

void WigleScreen::_showUploadMenu() {
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Not connected");
    return;
  }
  String token = WigleUtil::readToken(Uni.Storage);
  if (token.length() == 0) {
    ShowStatusAction::show("Set Wigle token first");
    return;
  }

  _state = STATE_UPLOAD;
  _fileCount = WigleUtil::listFiles(Uni.Storage, _fileNames, _fileLabels,
                                     _fileUploaded, WigleUtil::MAX_FILES);

  if (_fileCount == 0) {
    ShowStatusAction::show("No wardrive files found");
    _showMenu();
    return;
  }

  for (uint8_t i = 0; i < _fileCount; i++) {
    _uploadItems[i] = {_fileLabels[i].c_str(), _fileUploaded[i] ? "Uploaded" : nullptr};
  }
  setItems(_uploadItems, _fileCount);
}

void WigleScreen::_uploadFile(uint8_t index) {
  if (index >= _fileCount) return;
  WigleUtil::uploadFile(Uni.Storage, _fileNames[index]);
  int n = Achievement.inc("gps_wigle_upload");
  if (n == 1)   Achievement.unlock("gps_wigle_upload");
  if (n == 5)   Achievement.unlock("gps_wigle_5");
  if (n == 20)  Achievement.unlock("gps_wigle_20");
  if (n == 50)  Achievement.unlock("gps_wigle_50");
  if (n == 100) Achievement.unlock("gps_wigle_100");
  _showUploadMenu();
}

