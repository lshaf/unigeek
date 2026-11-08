#include "TcpClientScreen.h"

#include <WiFi.h>
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/terminal/TcpTransport.h"

uint8_t TcpClientScreen::_transportItems(ListItem* out, uint8_t max) {
  if (max < 2) return 0;

  if (_host.length() == 0) snprintf(_hostLabel, sizeof(_hostLabel), "-");
  else                     _host.toCharArray(_hostLabel, sizeof(_hostLabel));

  if (_port <= 0) snprintf(_portLabel, sizeof(_portLabel), "-");
  else            snprintf(_portLabel, sizeof(_portLabel), "%d", _port);

  out[0] = {"Host", _hostLabel};
  out[1] = {"Port", _portLabel};
  return 2;
}

void TcpClientScreen::_onTransportItem(uint8_t index) {
  if      (index == 0) _configHost();
  else if (index == 1) _configPort();
  _refreshItems();
}

void TcpClientScreen::_configHost() {
  String initial = _host;
  if (initial.length() == 0 && WiFi.status() == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    initial = String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + ".";
  }

#ifdef DEVICE_HAS_KEYBOARD
  const auto hostInputMode = InputTextAction::INPUT_TEXT;
#else
  const auto hostInputMode = InputTextAction::INPUT_IP_ADDRESS;
#endif

  String value = InputTextAction::popup("Host", initial, hostInputMode);
  if (!InputTextAction::wasCancelled() && value.length() > 0) _host = value;
}

void TcpClientScreen::_configPort() {
  int value = InputNumberAction::popup("Port", 1, 65535, _port > 0 ? _port : 0);
  if (!InputNumberAction::wasCancelled()) _port = value;
}

TerminalTransport* TcpClientScreen::_createTransport() {
  if (_host.length() == 0) {
    ShowStatusAction::show("Host not set", 1200);
    return nullptr;
  }
  if (_port < 1 || _port > 65535) {
    ShowStatusAction::show("Port not set", 1200);
    return nullptr;
  }
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Not connected", 1500);
    return nullptr;
  }

  ShowStatusAction::show("Connecting...", 0);
  return new TcpTransport(_host, (uint16_t)_port);
}
