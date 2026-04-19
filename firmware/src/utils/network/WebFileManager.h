#pragma once

#include <ESPAsyncWebServer.h>
#include <FS.h>

class WebFileManager {
public:
  static constexpr const char* WEB_PATH = "/unigeek/web/file_manager";

  bool   begin();
  void   end();
  String getError() const { return _lastError; }

private:
  AsyncWebServer _server{8080};
  fs::File       _fsUpload;
  String         _uploadTempPath;
  int            _sessionCounter = 0;
  bool           _started        = false;

  static constexpr int MAX_SESSIONS = 10;
  String _activeSessions[MAX_SESSIONS];
  String _lastError;

  void   _prepareServer();
  bool   _isAuthenticated(AsyncWebServerRequest* request, bool logout = false);
  bool   _removeDirectory(const String& path);
  String _getContentType(const String& filename);
  String _color565ToWebHex(uint16_t color);
};
