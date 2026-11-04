#include "NetworkSettingsScreen.h"

#include <WiFi.h>
#include "core/ScreenManager.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"

namespace {

const IPAddress kUnset(0, 0, 0, 0);

String addressLabel(const IPAddress& a) { return a == kUnset ? String("-") : a.toString(); }

bool sameSubnet(const IPAddress& a, const IPAddress& b, const IPAddress& mask) {
  for (int i = 0; i < 4; ++i)
    if ((a[i] & mask[i]) != (b[i] & mask[i])) return false;
  return true;
}

}  // namespace

void NetworkSettingsScreen::onInit() {
  _ssid = WiFi.SSID();
  // Saved settings for this network; without an address saved, the ones the
  // network gave by DHCP as a starting point for Static.
  WifiUtility::readIpConfig(_ssid.c_str(), _config);
  if (_config.ip == kUnset) {
    _config.ip      = WiFi.localIP();
    _config.gateway = WiFi.gatewayIP();
    _config.prefix  = WifiUtility::maskToPrefix(WiFi.subnetMask());
    if (_config.prefix < 1 || _config.prefix > 30) _config.prefix = 24;
    _config.dns1 = WiFi.dnsIP(0);
    _config.dns2 = WiFi.dnsIP(1);
  }
  _rebuild(0);
}

void NetworkSettingsScreen::_rebuild(uint8_t selected) {
  _count = 0;
  auto add = [&](const char* label, const String* value, Row row) {
    _items[_count] = {label, value ? value->c_str() : nullptr};
    _rows[_count++] = row;
  };
  _values[0] = _config.isStatic ? "Static" : "DHCP";
  add("IP Assignment", &_values[0], ROW_MODE);
  if (_config.isStatic) {
    _values[1] = addressLabel(_config.ip);
    _values[2] = addressLabel(_config.gateway);
    _values[3] = "/" + String(_config.prefix) + " (" + WifiUtility::prefixToMask(_config.prefix).toString() + ")";
    _values[4] = addressLabel(_config.dns1);
    _values[5] = addressLabel(_config.dns2);
    add("IP Address", &_values[1], ROW_IP);
    add("Gateway", &_values[2], ROW_GATEWAY);
    add("Netmask", &_values[3], ROW_NETMASK);
    add("DNS 1", &_values[4], ROW_DNS1);
    add("DNS 2", &_values[5], ROW_DNS2);
  }
  add("Save", nullptr, ROW_SAVE);
  // Clear the whole body first: a popup (IP keyboard) or a status message drew
  // above the list, and setItems() only repaints the list.
  Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  setItems(_items, _count, selected < _count ? selected : _count - 1);
}

void NetworkSettingsScreen::onItemSelected(uint8_t index) {
  if (index >= _count) return;
  switch (_rows[index]) {
    case ROW_MODE:
      _config.isStatic = !_config.isStatic;
      break;
    case ROW_IP:      _editAddress("IP Address", _config.ip, false); break;
    case ROW_GATEWAY: _editAddress("Gateway", _config.gateway, false); break;
    case ROW_NETMASK: {
      const int v = InputNumberAction::popup("Prefix (1-30)", 1, 30, _config.prefix);
      if (!InputNumberAction::wasCancelled()) _config.prefix = (uint8_t)v;
      break;
    }
    case ROW_DNS1: _editAddress("DNS 1", _config.dns1, true); break;
    case ROW_DNS2: _editAddress("DNS 2", _config.dns2, true); break;
    case ROW_SAVE: _save(); return;
  }
  _rebuild(index);
}

// IP keyboard; an optional field can be cleared by leaving it empty.
void NetworkSettingsScreen::_editAddress(const char* title, IPAddress& field, bool optional) {
  const String initial = field == kUnset ? String("") : field.toString();
  String value = InputTextAction::popup(title, initial, InputTextAction::INPUT_IP_ADDRESS);
  if (InputTextAction::wasCancelled()) return;
  value.trim();
  if (value.length() == 0) {
    if (optional) field = kUnset;
    return;
  }
  IPAddress parsed;
  if (!parsed.fromString(value)) {
    ShowStatusAction::show("Invalid address", 1200);
    return;  // onItemSelected() repaints via _rebuild()
  }
  field = parsed;
}

void NetworkSettingsScreen::_save() {
  if (_ssid.length() == 0) {
    ShowStatusAction::show("Not connected", 1500);
    render();
    return;
  }
  if (_config.isStatic) {
    if (_config.ip == kUnset)      { ShowStatusAction::show("IP Address not set", 1200); render(); return; }
    if (_config.gateway == kUnset) { ShowStatusAction::show("Gateway not set", 1200); render(); return; }
    if (!sameSubnet(_config.ip, _config.gateway, WifiUtility::prefixToMask(_config.prefix))) {
      ShowStatusAction::show("Gateway outside subnet", 1500);
      render();
      return;
    }
  }
  if (!WifiUtility::saveIpConfig(_ssid.c_str(), _config)) {
    ShowStatusAction::show("Could not save", 1500);
    render();
    return;
  }

  // Reconnect so the settings apply now (connect() applies them first).
  const String bssid = WiFi.BSSIDstr();
  const String password = WifiUtility::readPassword(bssid.c_str(), _ssid.c_str());
  ShowStatusAction::show(("Reconnecting to " + _ssid + "...").c_str(), 0);
  WiFi.disconnect(false);
  delay(200);
  const bool ok = WifiUtility::connect(_ssid.c_str(), password.c_str());
  if (ok) {
    ShowStatusAction::show(("Saved. IP " + WiFi.localIP().toString()).c_str(), 1800);
    Screen.goBack();  // back to Information, which reloads the details
    return;
  }
  render();
  ShowStatusAction::show("Saved, but reconnect failed", 2000);
  render();
}
