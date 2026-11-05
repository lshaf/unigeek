#include "TerminalLog.h"
#include "core/Device.h"

bool TerminalLog::begin(Mode mode, const char* dir, const String& name, InfoSink info, void* context) {
  end();
  _mode = mode;
  bool ok = true;
  if (mode == MODE_FILE)            ok = _beginFile(dir, name, info, context);
  else if (mode == MODE_STREAM_AP)  ok = _beginStream(true, name, info, context);
  else if (mode == MODE_STREAM_NET) ok = _beginStream(false, name, info, context);
  if (!ok) _mode = MODE_OFF;
  return ok;
}

void TerminalLog::end() {
  if (_file) _file.close();

  if (_serverUp) {
    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
      if (_clients[i]) _clients[i].stop();
    }
    _server.stop();
    if (_apStarted) WiFi.softAPdisconnect(true);
    else            WiFi.disconnect();
    _serverUp  = false;
    _apStarted = false;
  }
  _mode = MODE_OFF;
}

void TerminalLog::poll() {
  if (!_serverUp || !_server.hasClient()) return;
  for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
    if (!_clients[i] || !_clients[i].connected()) {
      _clients[i] = _server.available();
      return;
    }
  }
  _server.available().stop();  // no free slot
}

void TerminalLog::line(const String& text) {
  if (_file) _file.println(text.c_str());
  if (!_serverUp) return;
  for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
    if (_clients[i] && _clients[i].connected()) _clients[i].println(text.c_str());
  }
}

bool TerminalLog::_beginFile(const char* dir, const String& name, InfoSink info, void* context) {
  if (!Uni.Storage || !dir) {
    if (info) info(context, "Failed to open log");
    return false;
  }
  Uni.Storage->makeDir(dir);
  const String path = String(dir) + "/" + name + ".log";
  _file = Uni.Storage->open(path.c_str(), "w");
  if (!_file) {
    if (info) info(context, "Failed to open log");
    return false;
  }
  if (info) info(context, "Filename: " + name + ".log");
  return true;
}

bool TerminalLog::_beginStream(bool ap, const String& name, InfoSink info, void* context) {
  IPAddress ip;
  if (ap) {
    WiFi.softAP(name.c_str());
    delay(100);
    _apStarted = true;
    ip = WiFi.softAPIP();
    if (info) info(context, "AP: " + name);
  } else {
    if (WiFi.status() != WL_CONNECTED) {
      if (info) info(context, "Not connected");
      return false;
    }
    ip = WiFi.localIP();
    if (info) info(context, "Network: " + name);
  }

  _server.begin();
  _serverUp = true;
  if (info) info(context, "Stream: nc " + ip.toString() + ":" + String(TCP_PORT));
  return true;
}
