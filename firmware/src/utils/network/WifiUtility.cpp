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
  _applyIpConfig(ssid);  // static settings saved for this network, or DHCP
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
  if (InputTextAction::wasCancelled()) return CONNECT_CANCELLED;

  // An explicitly saved empty value is distinct from EXIT. This also lets
  // connectWithPrompt() try open networks without treating them as cancelled.
  if (Screen.current()) Screen.current()->render();
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

// ── IP settings ───────────────────────────────────────────────────────────

String WifiUtility::_buildIpConfigPath(const char* ssid)
{
  String name = ssid;
  name.replace("/", "_");  // keep the file inside the folder
  return String(_passwordPath) + "/" + name + ".ip";
}

IPAddress WifiUtility::prefixToMask(uint8_t prefix)
{
  if (prefix > 32) prefix = 32;
  const uint32_t mask = prefix ? 0xFFFFFFFFu << (32 - prefix) : 0;
  return IPAddress((mask >> 24) & 0xFF, (mask >> 16) & 0xFF, (mask >> 8) & 0xFF, mask & 0xFF);
}

uint8_t WifiUtility::maskToPrefix(const IPAddress& mask)
{
  uint8_t n = 0;
  for (int i = 0; i < 4; ++i)
    for (int b = 7; b >= 0; --b)
      if (mask[i] & (1 << b)) ++n;
  return n;
}

bool WifiUtility::readIpConfig(const char* ssid, IpConfig& out)
{
  out = IpConfig();
  if (!Uni.Storage || !ssid || !ssid[0]) return false;
  const String path = _buildIpConfigPath(ssid);
  if (!Uni.Storage->exists(path.c_str())) return false;
  const String text = Uni.Storage->readFile(path.c_str());
  int start = 0;
  while (start < (int)text.length()) {
    int end = text.indexOf('\n', start);
    if (end < 0) end = text.length();
    String line = text.substring(start, end);
    start = end + 1;
    line.trim();
    const int eq = line.indexOf('=');
    if (eq <= 0) continue;
    const String key = line.substring(0, eq);
    const String value = line.substring(eq + 1);
    if (key == "mode")         out.isStatic = value == "static";
    else if (key == "ip")      out.ip.fromString(value);
    else if (key == "gateway") out.gateway.fromString(value);
    else if (key == "prefix")  out.prefix = (uint8_t)constrain(value.toInt(), 1, 30);
    else if (key == "dns1")    out.dns1.fromString(value);
    else if (key == "dns2")    out.dns2.fromString(value);
  }
  return true;
}

bool WifiUtility::saveIpConfig(const char* ssid, const IpConfig& c)
{
  if (!Uni.Storage || !ssid || !ssid[0]) return false;
  auto addr = [](const IPAddress& a) {
    return a == IPAddress(0, 0, 0, 0) ? String("") : a.toString();
  };
  String text = String("mode=") + (c.isStatic ? "static" : "dhcp") + "\n";
  text += "ip=" + addr(c.ip) + "\n";
  text += "gateway=" + addr(c.gateway) + "\n";
  text += "prefix=" + String(c.prefix) + "\n";
  text += "dns1=" + addr(c.dns1) + "\n";
  text += "dns2=" + addr(c.dns2) + "\n";
  Uni.Storage->makeDir(_passwordPath);
  return Uni.Storage->writeFile(_buildIpConfigPath(ssid).c_str(), text.c_str());
}

// Before WiFi.begin(): the network's static settings, or back to DHCP (which
// a previous network's static settings would otherwise keep disabled).
void WifiUtility::_applyIpConfig(const char* ssid)
{
  IpConfig c;
  if (readIpConfig(ssid, c) && c.isStatic && c.ip != IPAddress(0, 0, 0, 0)) {
    const IPAddress unset(0, 0, 0, 0);
    const IPAddress dns1 = c.dns1 != unset ? c.dns1 : c.gateway;
    WiFi.config(c.ip, c.gateway, prefixToMask(c.prefix), dns1, c.dns2);
  } else {
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  }
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
