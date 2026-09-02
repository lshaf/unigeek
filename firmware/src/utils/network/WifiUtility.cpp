#include "utils/network/WifiUtility.h"
#include "core/Device.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include <esp_wifi.h>

// ── Scan ──────────────────────────────────────────────────────────────────

uint8_t WifiUtility::scan(ScannedWifi* out, uint8_t maxCount)
{
  WiFi.scanDelete();
  int total = WiFi.scanNetworks();
  if (total > maxCount) total = maxCount;

  for (int i = 0; i < total; i++) {
    const int rssi = WiFi.RSSI(i);

    snprintf(out[i].label, sizeof(out[i].label),
             "[%d] %s", rssi, WiFi.SSID(i).c_str());
    strncpy(out[i].bssid, WiFi.BSSIDstr(i).c_str(), sizeof(out[i].bssid));
    strncpy(out[i].ssid,  WiFi.SSID(i).c_str(),     sizeof(out[i].ssid));
    out[i].rssi = (int16_t)rssi;
  }

  return (uint8_t)total;
}

// ── Connect ───────────────────────────────────────────────────────────────

bool WifiUtility::connect(const char* ssid, const char* password)
{
  // Preserve AP if running, otherwise just STA
  if (WiFi.getMode() & WIFI_MODE_AP) {
    WiFi.mode(WIFI_MODE_APSTA);
  } else {
    WiFi.mode(WIFI_MODE_STA);
  }

  WiFi.setAutoReconnect(false);
  WiFi.begin(ssid, password);
  bool ok = WiFi.waitForConnectResult(10000) == WL_CONNECTED;
  if (!ok) {
    WiFi.disconnect(false);
    delay(200);
  }
  return ok;
}

// ── Connect with prompt ──────────────────────────────────────────────────

WifiUtility::ConnectResult WifiUtility::connectWithPrompt(const char* bssid, const char* ssid)
{
  // Try saved password first
  String saved = readPassword(bssid, ssid);
  if (saved.length() > 0) {
    ShowStatusAction::show(("Connecting to " + String(ssid) + "...").c_str(), 0);
    if (connect(ssid, saved.c_str())) {
      savePassword(bssid, ssid, saved.c_str());
      return CONNECT_OK;
    }
  }

  // Ask for password
  String password = InputTextAction::popup(ssid);
  if (password.length() == 0) return CONNECT_CANCELLED;

  ShowStatusAction::show(("Connecting to " + String(ssid) + "...").c_str(), 0);
  if (!connect(ssid, password.c_str())) {
    return CONNECT_FAILED;
  }

  savePassword(bssid, ssid, password.c_str());
  return CONNECT_OK;
}

// ── Password storage ──────────────────────────────────────────────────────

String WifiUtility::_buildPasswordPath(const char* bssid, const char* ssid)
{
  String cleanBssid = bssid;
  cleanBssid.replace(":", "");
  return String(_passwordPath) + "/" + cleanBssid + "_" + ssid + ".pass";
}

String WifiUtility::readPassword(const char* bssid, const char* ssid)
{
  if (!Uni.Storage) return "";
  String path = _buildPasswordPath(bssid, ssid);
  if (!Uni.Storage->exists(path.c_str())) return "";
  return Uni.Storage->readFile(path.c_str());
}

void WifiUtility::savePassword(const char* bssid, const char* ssid, const char* password)
{
  if (!Uni.Storage) return;
  String path = _buildPasswordPath(bssid, ssid);
  Uni.Storage->makeDir(_passwordPath);
  Uni.Storage->writeFile(path.c_str(), password);
}

// ── Internet check ───────────────────────────────────────────────────────

bool WifiUtility::checkInternet()
{
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClient client;
  // Try TCP connect to Google DNS on port 53
  bool ok = client.connect("8.8.8.8", 53, 3000);
  client.stop();
  return ok;
}


