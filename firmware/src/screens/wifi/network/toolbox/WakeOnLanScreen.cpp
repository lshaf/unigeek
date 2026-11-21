#include "WakeOnLanScreen.h"

#include <WiFi.h>
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "core/ScreenManager.h"

String WakeOnLanScreen::_macText() const {
  if (!_hasMac) return "-";
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           _mac[0], _mac[1], _mac[2], _mac[3], _mac[4], _mac[5]);
  return buf;
}

// Accepts 12 hex digits, with or without ':' / '-' / spaces.
bool WakeOnLanScreen::_parseMac(const String& text, uint8_t out[6]) {
  uint8_t digits[12];
  uint8_t n = 0;
  for (size_t i = 0; i < text.length(); ++i) {
    const char c = text[i];
    if (c == ':' || c == '-' || c == ' ' || c == '.') continue;
    uint8_t v;
    if (c >= '0' && c <= '9')      v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
    else return false;
    if (n >= 12) return false;
    digits[n++] = v;
  }
  if (n != 12) return false;
  for (uint8_t i = 0; i < 6; ++i) out[i] = (uint8_t)(digits[2 * i] << 4 | digits[2 * i + 1]);
  return true;
}

void WakeOnLanScreen::_buildRows() {
  _addRow("MAC", _macText(), ROW_MAC);
  _addRow("Destination", _toHost ? "Host" : "Broadcast", ROW_DESTINATION);
  if (_toHost) _addRow("Host", _host.length() ? _host : String("-"), ROW_HOST);
  _addRow("Port", String(_port), ROW_PORT);
  _addRow(_origName.length() ? "Save Changes" : "Save", _save ? "Yes" : "No", ROW_SAVE);
  if (_save) {
    snprintf(_nameLabel, sizeof(_nameLabel), "%s", _name.length() ? _name.c_str() : "-");
    _addRow("Name", _nameLabel, ROW_NAME);
  }
}

void WakeOnLanScreen::_editRow(uint8_t id) {
  switch (id) {
    case ROW_MAC: {
#ifdef DEVICE_HAS_KEYBOARD
      const auto mode = InputTextAction::INPUT_TEXT;
      const String initial = _hasMac ? _macText() : String("");
#else
      // The hex keyboard has no ':', so edit the 12 digits only.
      const auto mode = InputTextAction::INPUT_HEX;
      String initial = _hasMac ? _macText() : String("");
      initial.replace(":", "");
#endif
      const String v = InputTextAction::popup("MAC (12 hex)", initial, mode);
      if (InputTextAction::wasCancelled() || v.length() == 0) break;
      uint8_t mac[6];
      if (_parseMac(v, mac)) {
        memcpy(_mac, mac, 6);
        _hasMac = true;
      } else {
        render(); ShowStatusAction::show("Invalid MAC", 1500);
      }
      break;
    }
    case ROW_DESTINATION:
      _toHost = !_toHost;
      break;
    case ROW_HOST:
      _host = _editHost("Host", _host);
      break;
    case ROW_PORT: {
      const int v = InputNumberAction::popup("Port", 1, 65535, _port);
      if (!InputNumberAction::wasCancelled()) _port = v;
      break;
    }
    case ROW_SAVE:
      _save = !_save;
      break;
    case ROW_NAME: {
      String v = InputTextAction::popup("Name", _name, InputTextAction::INPUT_TEXT);
      if (!InputTextAction::wasCancelled()) { v.trim(); if (v.length()) _name = v; }
      break;
    }
  }
}

void WakeOnLanScreen::loadSaved(const SavedHost& host) {
  _parseMac(host.get("mac"), _mac);
  _hasMac = host.has("mac");
  _toHost = host.get("dest") == "host";
  _host   = host.get("host");
  _port   = host.getInt("port", 9);
  _name = _origName = host.name;
}

// Saved when the packet is sent. The magic packet needs a MAC; a host target
// needs its address; a name is required to save.
void WakeOnLanScreen::_persist() {
  if (!_save) return;
  if (_name.length() == 0) { ShowStatusAction::show("Name not set", 1200); return; }
  SavedHost host;
  host.name = _name;
  host.set("mac", _macText());
  host.set("dest", _toHost ? "host" : "broadcast");
  if (_toHost) host.set("host", _host);
  host.set("port", String(_port));
  if (SavedHosts::upsert(SAVED_DIR, host, _origName)) {
    _origName = _name;
    _save = false;
  } else {
    ShowStatusAction::show("Could not save", 1500);
  }
}

bool WakeOnLanScreen::_begin() {
  if (!_hasMac) {
    ShowStatusAction::show("MAC not set");
    return false;
  }
  if (_toHost) {
    if (!_resolve(_host, _dest)) return false;
  } else {
    if (WiFi.status() != WL_CONNECTED) {
      ShowStatusAction::show("Not connected", 1500);
      return false;
    }
    const IPAddress ip = WiFi.localIP(), mask = WiFi.subnetMask();
    _dest = IPAddress((uint8_t)(ip[0] | ~mask[0]), (uint8_t)(ip[1] | ~mask[1]),
                      (uint8_t)(ip[2] | ~mask[2]), (uint8_t)(ip[3] | ~mask[3]));
  }
  if (!_udp.begin(0)) {
    ShowStatusAction::show("UDP error", 1500);
    return false;
  }
  _udpOpen = true;
  _sent = 0;
  _nextAt = millis();
  _print("Waking " + _macText());
  _print("to " + _dest.toString() + ":" + String(_port) + (_toHost ? "" : " (broadcast)"));
  return true;
}

bool WakeOnLanScreen::_step() {
  if ((long)(millis() - _nextAt) < 0) return true;

  uint8_t packet[102];
  memset(packet, 0xFF, 6);
  for (uint8_t i = 0; i < 16; ++i) memcpy(packet + 6 + i * 6, _mac, 6);
  _udp.beginPacket(_dest, (uint16_t)_port);
  _udp.write(packet, sizeof(packet));
  const bool ok = _udp.endPacket() == 1;
  ++_sent;
  _print("Magic packet " + String((int)_sent) + "/" + String((int)SENDS) + (ok ? " sent" : " failed"));
  _nextAt = millis() + 300;

  if (_sent < SENDS) return true;
  if (_save) _persist();
  return false;
}

void WakeOnLanScreen::_end() {
  if (_udpOpen) _udp.stop();
  _udpOpen = false;
}
