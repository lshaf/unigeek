#include "WebFileManagerScreen.h"
#include "core/ScreenManager.h"
#include "core/Device.h"
#include "core/ConfigManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/network/NetworkMenuScreen.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include <WiFi.h>

void WebFileManagerScreen::onInit() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("No storage available");
    Screen.goBack();
    return;
  }
  _showMenu();
}

void WebFileManagerScreen::onBack() {
  if (_state == STATE_RUNNING) {
    _stop();
  } else {
    Screen.goBack();
  }
}

void WebFileManagerScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: {
      String pw = InputTextAction::popup("Web Password",
        Config.get(APP_CONFIG_WEB_PASSWORD, APP_CONFIG_WEB_PASSWORD_DEFAULT).c_str());
      if (pw.length() > 0) {
        Config.set(APP_CONFIG_WEB_PASSWORD, pw);
        Config.save(Uni.Storage);
      }
      _showMenu();
      break;
    }
    case 1:
      _start();
      break;
  }
}

// ── private ────────────────────────────────────────────

void WebFileManagerScreen::_showMenu() {
  _state = STATE_MENU;

  _passwordSub = Config.get(APP_CONFIG_WEB_PASSWORD, APP_CONFIG_WEB_PASSWORD_DEFAULT);
  _menuItems[0] = {"Password", _passwordSub.c_str()};
  _menuItems[1] = {"Start"};
  setItems(_menuItems, 2);
}

void WebFileManagerScreen::onRender() {
  if (_state == STATE_RUNNING) { _drawRunning(); return; }
  ListScreen::onRender();
}

void WebFileManagerScreen::_drawRunning() {
  auto& lcd = Uni.Lcd;
  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);

  int cx   = bodyX() + bodyW() / 2;
  int midY = bodyY() + bodyH() / 2;

  lcd.setTextFont(1);
  lcd.setTextDatum(TC_DATUM);
  lcd.setTextColor(TFT_GREEN, TFT_BLACK);
  lcd.drawString(_ipUrl,   cx, midY - 10);
  lcd.drawString(_mdnsUrl, cx, midY + 4);

  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.drawString("BACK to stop", cx, bodyY() + bodyH() - 14);
}

void WebFileManagerScreen::_start() {
  ShowStatusAction::show("Starting server...", 0);
  if (!_server.begin()) {
    ShowStatusAction::show(_server.getError().c_str());
    _showMenu();
    return;
  }
  _state = STATE_RUNNING;
  int nw = Achievement.inc("wifi_wfm_started");
  if (nw == 1) Achievement.unlock("wifi_wfm_started");
  _ipUrl   = "http://" + WiFi.localIP().toString() + ":8080/";
  _mdnsUrl = "http://unigeek.local:8080/";
  setItems(nullptr, 0);
  render();
}

void WebFileManagerScreen::_stop() {
  _server.end();
  _showMenu();
}
