#include "WifiAnalyzerScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/WifiMenuScreen.h"
#include "ui/actions/ShowStatusAction.h"

#include <WiFi.h>

void WifiAnalyzerScreen::onInit()
{
  _scan();
}

void WifiAnalyzerScreen::onUpdate()
{
  if (_state == STATE_INFO) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) { _showScan(); return; }
#ifndef DEVICE_HAS_KEYBOARD
      if (dir == INavigation::DIR_PRESS) { _showScan(); return; }
#endif
      _scrollView.onNav(dir);
    }
    return;
  }
  ListScreen::onUpdate();
}

void WifiAnalyzerScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_SCAN && index < _entryCount) {
    _showInfo(index);
  }
}

void WifiAnalyzerScreen::onRender()
{
  if (_state == STATE_INFO) {
    _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  ListScreen::onRender();
}

void WifiAnalyzerScreen::onBack()
{
  if (_state == STATE_INFO) {
    _showScan();
  } else {
    WiFi.scanDelete();
    Screen.goBack();
  }
}

// ── Private ────────────────────────────────────────────────────────────────

void WifiAnalyzerScreen::_scan()
{
  ShowStatusAction::show("Scanning...", 0);

  WiFi.mode(WIFI_STA);
  int total = WiFi.scanNetworks(false, true);
  if (total > MAX_SCAN) total = MAX_SCAN;
  _entryCount = total;

  for (int i = 0; i < total; i++) {
    int32_t rssi = WiFi.RSSI(i);
    const char* strength;
    if      (rssi >= -50) strength = "Very Good";
    else if (rssi >= -60) strength = "Good";
    else if (rssi >= -70) strength = "Better";
    else if (rssi >= -80) strength = "Low";
    else                  strength = "Very Low";

    snprintf(_entries[i].ssid,    sizeof(_entries[i].ssid),    "%s", WiFi.SSID(i).c_str());
    snprintf(_entries[i].bssid,   sizeof(_entries[i].bssid),   "%s", WiFi.BSSIDstr(i).c_str());
    snprintf(_entries[i].rssi,    sizeof(_entries[i].rssi),    "[%d] %s", (int)rssi, strength);
    snprintf(_entries[i].channel, sizeof(_entries[i].channel), "%d", (int)WiFi.channel(i));

    switch (WiFi.encryptionType(i)) {
      case WIFI_AUTH_OPEN:           snprintf(_entries[i].encryption, sizeof(_entries[i].encryption), "OPEN");            break;
      case WIFI_AUTH_WEP:            snprintf(_entries[i].encryption, sizeof(_entries[i].encryption), "WEP");             break;
      case WIFI_AUTH_WPA_PSK:        snprintf(_entries[i].encryption, sizeof(_entries[i].encryption), "WPA_PSK");         break;
      case WIFI_AUTH_WPA2_PSK:       snprintf(_entries[i].encryption, sizeof(_entries[i].encryption), "WPA2_PSK");        break;
      case WIFI_AUTH_WPA_WPA2_PSK:   snprintf(_entries[i].encryption, sizeof(_entries[i].encryption), "WPA_WPA2_PSK");    break;
      case WIFI_AUTH_WPA2_ENTERPRISE:snprintf(_entries[i].encryption, sizeof(_entries[i].encryption), "WPA2_ENTERPRISE"); break;
      case WIFI_AUTH_WPA3_PSK:       snprintf(_entries[i].encryption, sizeof(_entries[i].encryption), "WPA3_PSK");        break;
      default:                       snprintf(_entries[i].encryption, sizeof(_entries[i].encryption), "UNKNOWN");         break;
    }
  }

  int na = Achievement.inc("wifi_analyzer_scan");
  if (na == 1) Achievement.unlock("wifi_analyzer_scan");
  if (total >= 20) {
    int n20 = Achievement.inc("wifi_analyzer_20aps");
    if (n20 == 1) Achievement.unlock("wifi_analyzer_20aps");
  }

  _showScan();
}

void WifiAnalyzerScreen::_showScan()
{
  _state = STATE_SCAN;
  strncpy(_title, "WiFi Analyzer", sizeof(_title));

  for (int i = 0; i < _entryCount; i++) {
    _scanItems[i] = {_entries[i].ssid, _entries[i].bssid};
  }

  setItems(_scanItems, _entryCount);
}

void WifiAnalyzerScreen::_showInfo(int index)
{
  _state = STATE_INFO;
  strncpy(_title, "WiFi Info", sizeof(_title));

  _infoRows[0] = {"SSID",       _entries[index].ssid};
  _infoRows[1] = {"BSSID",      _entries[index].bssid};
  _infoRows[2] = {"RSSI",       _entries[index].rssi};
  _infoRows[3] = {"Channel",    _entries[index].channel};
  _infoRows[4] = {"Encryption", _entries[index].encryption};

  int nd = Achievement.inc("wifi_analyzer_detail");
  if (nd == 1) Achievement.unlock("wifi_analyzer_detail");

  setItems(nullptr, 0);
  _scrollView.setRows(_infoRows, 5);
  render();
}