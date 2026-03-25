#include "utils/network/WebFileManager.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include "core/Device.h"
#include "core/ConfigManager.h"

bool WebFileManager::begin() {
  wifi_mode_t mode = WiFi.getMode();
  if (WiFi.status() != WL_CONNECTED && mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA) {
    _lastError = "WiFi not connected";
    return false;
  }
  if (Uni.Storage && Uni.Storage->isAvailable()) {
    _fs = &Uni.Storage->getFS();
  } else {
    _lastError = "No storage available";
    return false;
  }
  const String indexPath = String(WEB_PATH) + "/index.htm";
  if (!_fs->exists(indexPath)) {
    _lastError = "Web page not installed\nWiFi > Network > Download\n> Web File Manager";
    return false;
  }

  MDNS.begin("unigeek");
  _prepareServer();
  if (!_started) {
    _server.begin();
    _started = true;
  }
  return true;
}

void WebFileManager::end() {
  if (_fsUpload) _fsUpload.close();
  _server.reset();
  _fs = nullptr;
  MDNS.end();
}

bool WebFileManager::_isAuthenticated(AsyncWebServerRequest* request, bool logout) {
  if (!request->hasHeader("Cookie")) return false;
  String cookies = request->header("cookie");
  int start = cookies.indexOf("session=");
  if (start == -1) return false;
  start += 8;
  int end = cookies.indexOf(';', start);
  String token = (end == -1) ? cookies.substring(start) : cookies.substring(start, end);
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (_activeSessions[i] == token) {
      if (logout) _activeSessions[i] = "";
      return true;
    }
  }
  return false;
}

bool WebFileManager::_removeDirectory(const String& path) {
  fs::File dir = _fs->open(path);
  if (!dir || !dir.isDirectory()) return false;
  fs::File f = dir.openNextFile();
  while (f) {
    String fp = String(f.path());
    if (f.isDirectory()) _removeDirectory(fp);
    else _fs->remove(fp);
    f = dir.openNextFile();
  }
  dir.close();
  return _fs->rmdir(path);
}

String WebFileManager::_getContentType(const String& filename) {
  if (filename.endsWith(".htm"))  return "text/html";
  if (filename.endsWith(".html")) return "text/html";
  if (filename.endsWith(".css"))  return "text/css";
  if (filename.endsWith(".js"))   return "application/javascript";
  if (filename.endsWith(".png"))  return "image/png";
  if (filename.endsWith(".gif"))  return "image/gif";
  if (filename.endsWith(".jpg"))  return "image/jpeg";
  if (filename.endsWith(".ico"))  return "image/x-icon";
  if (filename.endsWith(".xml"))  return "text/xml";
  if (filename.endsWith(".pdf"))  return "application/x-pdf";
  if (filename.endsWith(".zip"))  return "application/x-zip";
  if (filename.endsWith(".gz"))   return "application/x-gzip";
  return "text/plain";
}

String WebFileManager::_color565ToWebHex(uint16_t color) {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%02X%02X%02X",
           (uint8_t)((color >> 11) & 0x1F) * 255 / 31,
           (uint8_t)((color >> 5)  & 0x3F) * 255 / 63,
           (uint8_t)(color & 0x1F)          * 255 / 31);
  return String(buf);
}

void WebFileManager::_prepareServer() {
  _server.on("/download", HTTP_GET, [this](AsyncWebServerRequest* request) {
    if (!_isAuthenticated(request)) {
      request->send(401, "text/plain", "not authenticated.");
      return;
    }
    if (!request->hasArg("file")) {
      request->send(400, "text/plain", "No file specified.");
      return;
    }
    const String path = request->arg("file");
    if (!_fs->exists(path)) {
      request->send(404, "text/plain", "File not found.");
      return;
    }
    request->send(*_fs, path, _getContentType(path), true);
  });

  _server.on("/upload", HTTP_POST,
    [this](AsyncWebServerRequest* request) {
      if (!_isAuthenticated(request)) {
        request->send(401, "text/plain", "not authenticated.");
        return;
      }
      request->send(200, "text/plain", "ok.");
    },
    [this](AsyncWebServerRequest* request, String filename, size_t index,
           uint8_t* data, size_t len, bool final) {
      if (!_isAuthenticated(request)) {
        request->send(401, "text/plain", "not authenticated.");
        return;
      }
      if (!index) {
        String path = "/";
        if (request->hasArg("folder")) {
          path = request->arg("folder");
          if (!path.startsWith("/")) path = "/" + path;
          if (!path.endsWith("/"))   path += "/";
        }
        const String fullPath = path + filename;
        const int lastSlash = fullPath.lastIndexOf('/');
        if (lastSlash > 0) {
          String dir = fullPath.substring(0, lastSlash);
          for (int i = 1; i < (int)dir.length(); i++) {
            if (dir[i] == '/') {
              String sub = dir.substring(0, i);
              if (!_fs->exists(sub)) _fs->mkdir(sub);
            }
          }
          if (!_fs->exists(dir)) _fs->mkdir(dir);
        }
        _fsUpload = _fs->open(fullPath, FILE_WRITE);
      }
      if (len && _fsUpload) _fsUpload.write(data, len);
      if (final && _fsUpload) _fsUpload.close();
    }
  );

  const String password = Config.get(APP_CONFIG_WEB_PASSWORD, APP_CONFIG_WEB_PASSWORD_DEFAULT);
  _server.on("/", HTTP_POST, [this, password](AsyncWebServerRequest* request) {
    if (!request->hasParam("command", true)) {
      request->send(404, "text/plain", "404");
      return;
    }
    const String command = request->getParam("command", true)->value();
    if (command != "sudo" && !_isAuthenticated(request)) {
      request->send(401, "text/plain", "not authenticated.");
      return;
    }

    if (command == "ls") {
      String currentPath = request->hasParam("path", true)
        ? request->getParam("path", true)->value() : "/";
      if (currentPath == "") currentPath = "/";
      fs::File dir = _fs->open(currentPath);
      if (!dir || !dir.isDirectory()) {
        request->send(403, "text/plain", "Not a directory.");
        return;
      }
      String resp = "";
      fs::File f = dir.openNextFile();
      while (f) {
        resp += (f.isDirectory() ? "DIR:" : "FILE:") +
                String(f.name()) + ":" + String(f.size()) + "\n";
        f = dir.openNextFile();
      }
      request->send(200, "text/plain", resp);

    } else if (command == "sysinfo") {
      uint64_t total = Uni.Storage ? Uni.Storage->totalBytes() : 0;
      uint64_t used  = Uni.Storage ? Uni.Storage->usedBytes()  : 0;
      String resp = "UniGeek File Manager\n";
      resp += "FS:" + String(total - used) + "\n";
      resp += "US:" + String(used) + "\n";
      resp += "TS:" + String(total) + "\n";
      request->send(200, "text/plain", resp);

    } else if (command == "sudo") {
      const String pw = request->hasParam("param", true)
        ? request->getParam("param", true)->value() : "";
      if (pw == password) {
        const String token = String(random(0x7FFFFFFF), HEX);
        _sessionCounter = (_sessionCounter + 1) % MAX_SESSIONS;
        _activeSessions[_sessionCounter] = token;
        AsyncWebServerResponse* resp =
          request->beginResponse(200, "text/plain", "Login successful");
        resp->addHeader("Set-Cookie", "session=" + token + "; HttpOnly; Max-Age=3600");
        request->send(resp);
      } else {
        request->send(403, "text/plain", "forbidden");
      }

    } else if (command == "exit") {
      _isAuthenticated(request, true);
      AsyncWebServerResponse* resp =
        request->beginResponse(200, "text/plain", "Logged out");
      resp->addHeader("Set-Cookie",
        "session=; HttpOnly; Expires=Thu, 01 Jan 1970 00:00:00 GMT; Path=/");
      request->send(resp);

    } else if (command == "rm") {
      const String path = request->hasParam("path", true)
        ? request->getParam("path", true)->value() : "";
      if (path == "") { request->send(400, "text/plain", "No file specified."); return; }
      if (!_fs->exists(path)) { request->send(404, "text/plain", "File not found."); return; }
      fs::File f = _fs->open(path);
      bool isDir = f.isDirectory();
      f.close();
      bool ok = isDir ? _removeDirectory(path) : _fs->remove(path);
      request->send(ok ? 200 : 500, "text/plain",
        ok ? (isDir ? "Directory deleted." : "File deleted.") : "Failed to delete.");

    } else if (command == "mv") {
      const String src = request->hasParam("src", true)
        ? request->getParam("src", true)->value() : "";
      const String dst = request->hasParam("dst", true)
        ? request->getParam("dst", true)->value() : "";
      if (src == "" || dst == "") {
        request->send(400, "text/plain", "Source or destination not specified.");
        return;
      }
      if (!_fs->exists(src)) { request->send(404, "text/plain", "Source not found."); return; }
      bool ok = _fs->rename(src, dst);
      request->send(ok ? 200 : 500, "text/plain", ok ? "Moved." : "Failed to move.");

    } else if (command == "mkdir") {
      const String path = request->hasParam("path", true)
        ? request->getParam("path", true)->value() : "";
      if (path == "") { request->send(400, "text/plain", "No directory specified."); return; }
      bool ok = _fs->mkdir(path);
      request->send(ok ? 200 : 500, "text/plain",
        ok ? "Directory created." : "Failed to create directory.");

    } else if (command == "touch") {
      const String path = request->hasParam("path", true)
        ? request->getParam("path", true)->value() : "";
      if (path == "") { request->send(400, "text/plain", "No file specified."); return; }
      fs::File f = _fs->open(path, FILE_WRITE);
      if (f) { f.close(); request->send(200, "text/plain", "File created."); }
      else   { request->send(500, "text/plain", "Failed to create file."); }

    } else if (command == "cat") {
      const String path = request->hasParam("path", true)
        ? request->getParam("path", true)->value() : "";
      if (path == "") { request->send(400, "text/plain", "No file specified."); return; }
      if (!_fs->exists(path)) { request->send(404, "text/plain", "File not found."); return; }
      fs::File f = _fs->open(path);
      String resp = "";
      while (f.available()) resp += char(f.read());
      f.close();
      request->send(200, "text/plain", resp);

    } else if (command == "echo") {
      const String path = request->hasParam("path", true)
        ? request->getParam("path", true)->value() : "";
      const String content = request->hasParam("content", true)
        ? request->getParam("content", true)->value() : "";
      if (path == "" || content == "") {
        request->send(400, "text/plain", "File or content not specified.");
        return;
      }
      fs::File f = _fs->open(path, FILE_WRITE);
      if (!f) { request->send(500, "text/plain", "Failed to open file."); return; }
      f.print(content);
      f.close();
      request->send(200, "text/plain", "Content written.");

    } else {
      request->send(404, "text/plain", "command not found");
    }
  });

  _server.onNotFound([](AsyncWebServerRequest* request) {
    request->send(404, "text/plain", "404");
  });

  _server.on("/theme.css", HTTP_GET, [this](AsyncWebServerRequest* request) {
    const String css = ":root{--color:" + _color565ToWebHex(Config.getThemeColor()) + ";}";
    AsyncWebServerResponse* resp = request->beginResponse(200, "text/css", css);
    request->send(resp);
  });

  // Map .html requests to .htm files
  _server.on("*.html", HTTP_GET, [this](AsyncWebServerRequest* request) {
    String url = request->url();
    url = url.substring(0, url.length() - 5) + ".htm";
    String filePath = String(WEB_PATH) + url;
    if (_fs->exists(filePath)) {
      request->send(*_fs, filePath, "text/html");
    } else {
      request->send(404, "text/plain", "Not Found");
    }
  });

  _server.serveStatic("/", *_fs, (String(WEB_PATH) + "/").c_str());
}
