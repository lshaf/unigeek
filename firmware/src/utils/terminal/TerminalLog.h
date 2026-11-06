#pragma once

#include <Arduino.h>
#include <FS.h>
#include <WiFi.h>
#include <WiFiServer.h>
#include <WiFiClient.h>

// Where a terminal session's lines go besides the screen ("Log Mode"):
//   File            <dir>/<name>.log on the active storage (SD, else LittleFS)
//   Stream AP       starts a soft AP and serves the lines on TCP 23 (nc ip 23)
//   Stream Network  same TCP 23 server on the already-joined Wi-Fi network
//
// Status messages ("Filename: ...", "Stream: nc ...") are reported through an
// info callback so the owner can show them in its transcript.
class TerminalLog {
public:
  enum Mode : uint8_t { MODE_OFF, MODE_FILE, MODE_STREAM_AP, MODE_STREAM_NET };

  using InfoSink = void (*)(void* context, const String& message);

  static constexpr int MAX_TCP_CLIENTS = 4;
  static constexpr int TCP_PORT        = 23;

  ~TerminalLog() { end(); }

  // dir:     log folder (MODE_FILE), e.g. "/unigeek/utility/uart"
  // name:    file name without extension (MODE_FILE) / AP name (MODE_STREAM_AP)
  //          / network SSID, shown only (MODE_STREAM_NET)
  // Returns false if the sink could not be started (already reported via info).
  bool begin(Mode mode, const char* dir, const String& name, InfoSink info, void* context);
  void end();

  // Accept stream clients. Call regularly while the session runs.
  void poll();

  void line(const String& text);

  Mode mode() const { return _mode; }

private:
  Mode       _mode        = MODE_OFF;
  fs::File   _file;
  WiFiServer _server{TCP_PORT};
  WiFiClient _clients[MAX_TCP_CLIENTS];
  bool       _serverUp    = false;
  bool       _apStarted   = false;

  bool _beginFile(const char* dir, const String& name, InfoSink info, void* context);
  bool _beginStream(bool ap, const String& name, InfoSink info, void* context);
};
