#include "NetworkInformationScreen.h"

#include <WiFi.h>
#include "core/AchievementManager.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowQRCodeAction.h"
#include "screens/wifi/network/ConnectivityCheckScreen.h"

void NetworkInformationScreen::onInit() {
  _load();
  int ni = Achievement.inc("wifi_info_viewed");
  if (ni == 1) Achievement.unlock("wifi_info_viewed");
}

void NetworkInformationScreen::_load() {
  _rows[0]  = {"IP",       WiFi.localIP().toString()};
  _rows[1]  = {"DNS",      WiFi.dnsIP().toString()};
  _rows[2]  = {"Gateway",  WiFi.gatewayIP().toString()};
  _rows[3]  = {"Subnet",   WiFi.subnetMask().toString()};
  _rows[4]  = {"Channel",  String(WiFi.channel())};
  _rows[5]  = {"SSID",     WiFi.SSID()};
  _rows[6]  = {"Password", WiFi.psk()};
  _rows[7]  = {"RSSI",     String(WiFi.RSSI()) + " dBm"};
  _rows[8]  = {"Hostname", String(WiFi.getHostname())};
  _rows[9]  = {"MAC",      WiFi.macAddress()};
  _rows[10] = {"BSSID",    WiFi.BSSIDstr()};
  _scrollView.setRows(_rows, 11);
}

void NetworkInformationScreen::onUpdate() {
  if (!Uni.Nav->wasPressed()) return;
  const auto dir = Uni.Nav->readDirection();
  if (dir == INavigation::DIR_BACK) { Screen.goBack(); return; }
  if (dir == INavigation::DIR_PRESS) { _openOptions(); return; }
  _scrollView.onNav(dir);
  _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH() - kFooterH);
}

void NetworkInformationScreen::onRender() {
  _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH() - kFooterH);
  _renderFooter();
}

void NetworkInformationScreen::_renderFooter() {
  auto& lcd = Uni.Lcd;
  const int x = bodyX(), y = bodyY(), w = bodyW(), h = bodyH();
  const int top = y + h - kFooterH;
  lcd.fillRect(x, top, w, kFooterH, TFT_BLACK);
  lcd.drawFastHLine(x, top, w, TFT_DARKGREY);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.setTextDatum(BC_DATUM);
  lcd.drawString("[Press] Options", x + w / 2, y + h - 1);
  lcd.setTextDatum(TL_DATUM);
}

void NetworkInformationScreen::_openOptions() {
  static const InputSelectAction::Option kOptions[] = {
    {"Connectivity Check", "check", nullptr, 0, false},
    {"QR Code", "qr", nullptr, 0, false},
  };
  const char* choice = InputSelectAction::popup("Options", kOptions, 2);
  if (!choice) { render(); return; }
  if (strcmp(choice, "check") == 0) {
    Screen.push(new ConnectivityCheckScreen());
    return;
  }
  _showQRCode();
}

// The network as a Wi-Fi QR code (WIFI:T:...;S:...;P:...;;), to join it from a
// phone's camera.
void NetworkInformationScreen::_showQRCode() {
  String ssid = WiFi.SSID();
  String pass = WiFi.psk();

  String content = "WIFI:T:";
  content += (pass.length() > 0) ? "WPA" : "nopass";
  content += ";S:";
  content += ssid;
  content += ";P:";
  content += pass;
  content += ";;";

  ShowQRCodeAction::show(ssid.c_str(), content.c_str());
  int nq = Achievement.inc("wifi_qr_shown");
  if (nq == 1) Achievement.unlock("wifi_qr_shown");
  render();
}
